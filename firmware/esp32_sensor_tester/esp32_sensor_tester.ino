#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_timer.h>
#include <esp_attr.h>
#include <esp_adc/adc_continuous.h>
#include <hal/adc_types.h>
#include <soc/soc_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/stream_buffer.h>

#if !CONFIG_IDF_TARGET_ESP32
#error "This sketch targets the classic ESP32: GPIO32/33, ADC1 channels 4/5."
#endif

// ESP32 creates this access point for the optional Wi-Fi connection.
const char* WIFI_SSID = "ESP32_Actuator_Control";
const char* WIFI_PASSWORD = "password123";

// Existing motor wiring. Motor pulse generation remains hardware-timer driven.
const int ENA_PIN = 15;
const int DIR_PIN = 2;
const int PUL_PIN = 16;

// FSR1 = GPIO32 = ADC1 channel 4. FSR2 = GPIO33 = ADC1 channel 5.
const int FSR1_PIN = 32;
const int FSR2_PIN = 33;
constexpr adc_channel_t FSR1_CHANNEL = ADC_CHANNEL_4;
constexpr adc_channel_t FSR2_CHANNEL = ADC_CHANNEL_5;

// This board's USB-UART bridge lost bytes at 2 Mbaud and also failed flashing at
// 921600. Use 460800 for verified, lossless transport; ADC acquisition remains
// 10 kHz/channel.
constexpr uint32_t USB_BAUD = 460800;
constexpr size_t USB_TX_BUFFER_BYTES = 1024;
constexpr size_t STREAM_BUFFER_BYTES = 16384;
constexpr size_t USB_DUMP_CHUNK_BYTES = 512;

// adc_continuous sample_freq_hz is the TOTAL conversion rate across the pattern.
// Two alternating channels at 20 kconversions/s therefore produce 10,000
// sample pairs/s: one new 12-bit value per sensor every 100 microseconds.
constexpr uint32_t SAMPLE_PAIRS_PER_SECOND = 10000;
// To prevent ADC crosstalk (ghosting) between high-impedance FSR channels,
// we oversample by scheduling 8 consecutive conversions per channel and keeping
// only the final stabilized reading.
constexpr uint32_t OVERSAMPLE_COUNT = 8;
constexpr uint32_t ADC_CONVERSIONS_PER_SECOND = SAMPLE_PAIRS_PER_SECOND * OVERSAMPLE_COUNT * 2;

// The classic ESP32 DMA result is 2 bytes. A 256-byte conversion frame arrives
// every ~6.4 ms at 20 kconversions/s. The 8 KB driver pool gives the acquisition
// task ample scheduling margin without delaying motor or Wi-Fi work.
constexpr size_t DMA_CONVERSION_FRAME_BYTES = 256;
constexpr size_t DMA_DRIVER_POOL_BYTES = 8192;
constexpr size_t DMA_READ_BUFFER_BYTES = 1024;

// Swap these two levels if the physical actuator moves opposite to the UI.
const int UP_DIRECTION_LEVEL = HIGH;
const int DOWN_DIRECTION_LEVEL = LOW;

volatile long stepsRemaining = 0;
volatile bool isMoving = false;
volatile int pulseDelayUs = 1000;
hw_timer_t* timer = nullptr;
WebServer server(80);

String serialLine;

// Each capture entry is exactly four bytes in little-endian order:
//   uint16_t fsr1, uint16_t fsr2
// The USB payload is an indefinite stream of these 4-byte structs.
struct FsrSamplePair {
  uint16_t fsr1;
  uint16_t fsr2;
};
static_assert(sizeof(FsrSamplePair) == 4, "FSR pair must be exactly four bytes");
static_assert(sizeof(adc_digi_output_data_t) == SOC_ADC_DIGI_RESULT_BYTES,
              "Unexpected ADC DMA result size");

DRAM_ATTR uint8_t dmaReadBuffer[DMA_READ_BUFFER_BYTES] __attribute__((aligned(4)));

volatile bool isRecording = false;
volatile bool isAutoTesting = false;
volatile bool autoTestTriggered = false;
volatile uint16_t autoTestThreshold = 4000;
long autoTestTotalSteps = 1000;
long autoTestReturnSteps = 1000;
int autoTestReturnSpeed = 500;
volatile uint16_t latestFsr1 = 0;
volatile uint16_t latestFsr2 = 0;
volatile uint32_t latestSampleTimeUs = 0;
volatile uint32_t adcReadErrors = 0;

adc_continuous_handle_t adcHandle = nullptr;
TaskHandle_t adcTaskHandle = nullptr;
TaskHandle_t usbDumpTaskHandle = nullptr;
StreamBufferHandle_t binaryStream = nullptr;

void IRAM_ATTR onTimer() {
  static bool pulseState = false;
  if (isMoving && stepsRemaining > 0) {
    pulseState = !pulseState;
    digitalWrite(PUL_PIN, pulseState);
    if (!pulseState) {
      stepsRemaining--;
      if (stepsRemaining <= 0) {
        isMoving = false;
      }
    }
  } else if (pulseState) {
    pulseState = false;
    digitalWrite(PUL_PIN, LOW);
  }
}

void startMove(bool up, long steps, int requestedDelayUs) {
  if (steps < 1) return;
  pulseDelayUs = constrain(requestedDelayUs, 20, 5000);
  timerAlarm(timer, pulseDelayUs, true, 0);
  timerStart(timer);
  timerRestart(timer);
  digitalWrite(DIR_PIN, up ? UP_DIRECTION_LEVEL : DOWN_DIRECTION_LEVEL);
  digitalWrite(ENA_PIN, LOW);
  delayMicroseconds(10);
  stepsRemaining = steps;
  isMoving = true;
}

void stopMove() {
  stepsRemaining = 0;
  isMoving = false;
  digitalWrite(PUL_PIN, LOW);
  digitalWrite(ENA_PIN, HIGH);
}

String sensorJson() {
  // HTTP returns only the latest cached DMA pair; it never touches the ADC.
  uint16_t fsr1 = latestFsr1;
  uint16_t fsr2 = latestFsr2;
  uint32_t timeUs = latestSampleTimeUs;

  String json = "{\"time_ms\":" + String((uint32_t)(timeUs / 1000));
  json += ",\"fsr1\":" + String(fsr1);
  json += ",\"fsr2\":" + String(fsr2);
  json += "}";
  return json;
}

void adcDmaTask(void* parameter) {
  uint16_t bestFsr1 = 0;
  uint16_t bestFsr2 = 0;
  uint8_t last_channel = 255;

  while (true) {
    uint32_t bytesRead = 0;
    esp_err_t result = adc_continuous_read(
        adcHandle,
        dmaReadBuffer,
        sizeof(dmaReadBuffer),
        &bytesRead,
        100);

    if (result == ESP_ERR_TIMEOUT) {
      continue;
    }
    if (result != ESP_OK) {
      adcReadErrors++;
      taskYIELD();
      continue;
    }

    for (uint32_t offset = 0;
         offset + SOC_ADC_DIGI_RESULT_BYTES <= bytesRead;
         offset += SOC_ADC_DIGI_RESULT_BYTES) {
      const adc_digi_output_data_t* sample =
          reinterpret_cast<const adc_digi_output_data_t*>(dmaReadBuffer + offset);
      const uint16_t raw = sample->type1.data;
      const uint8_t channel = sample->type1.channel;

      if (channel == FSR1_CHANNEL) {
        if (last_channel == FSR2_CHANNEL) {
          // Channel just switched from FSR2 to FSR1. A full pattern cycle completed.
          // Emit the pair using the final, fully-stabilized reading of each channel.
          const FsrSamplePair pair = {bestFsr1, bestFsr2};
          
          latestFsr1 = pair.fsr1;
          latestFsr2 = pair.fsr2;
          latestSampleTimeUs = (uint32_t)esp_timer_get_time();

          if (isRecording) {
            xStreamBufferSend(binaryStream, &pair, sizeof(pair), 0);
          }
        }
        bestFsr1 = raw;
        last_channel = channel;
      } else if (channel == FSR2_CHANNEL) {
        bestFsr2 = raw;
        last_channel = channel;
        if (isAutoTesting) {
          static uint16_t consecutiveOverThreshold = 0;
          if (bestFsr1 >= autoTestThreshold || bestFsr2 >= autoTestThreshold) {
            consecutiveOverThreshold++;
            if (consecutiveOverThreshold > 100) { // Require ~10ms of sustained force to trigger
              autoTestReturnSteps = autoTestTotalSteps - stepsRemaining;
              autoTestTriggered = true;
              isAutoTesting = false;
              stepsRemaining = 0; // stop immediately via ISR
              consecutiveOverThreshold = 0;
            }
          } else {
            consecutiveOverThreshold = 0;
          }
        }
      }
    }
  }
}

void usbDumpTask(void* parameter) {
  uint8_t buf[USB_DUMP_CHUNK_BYTES];
  while (true) {
    while (!isRecording) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Tell the Python host that a continuous binary stream will follow
    Serial.print("BINARY_CAPTURE_START\n");
    Serial.flush();
    
    // Discard any old data that was sitting in the buffer
    xStreamBufferReset(binaryStream);
    
    // Stream data continuously while recording
    while (isRecording) {
      size_t bytesRead = xStreamBufferReceive(binaryStream, buf, sizeof(buf), pdMS_TO_TICKS(10));
      if (bytesRead > 0) {
        Serial.write(buf, bytesRead);
      }
    }
    
    // We stopped recording. Give the DMA task a moment to notice `isRecording == false`
    vTaskDelay(pdMS_TO_TICKS(5));
    
    // Drain whatever was left in the stream buffer
    size_t remaining;
    do {
      remaining = xStreamBufferReceive(binaryStream, buf, sizeof(buf), 0);
      if (remaining > 0) {
        Serial.write(buf, remaining);
      }
    } while (remaining > 0);

    Serial.print("BINARY_CAPTURE_STOP\n");
    Serial.flush();
  }
}

bool setupContinuousAdc() {
  adc_continuous_handle_cfg_t handleConfig = {};
  handleConfig.max_store_buf_size = DMA_DRIVER_POOL_BYTES;
  handleConfig.conv_frame_size = DMA_CONVERSION_FRAME_BYTES;
  handleConfig.flags.flush_pool = 0;

  if (adc_continuous_new_handle(&handleConfig, &adcHandle) != ESP_OK) {
    return false;
  }

  adc_digi_pattern_config_t patterns[OVERSAMPLE_COUNT * 2] = {};
  for (uint32_t i = 0; i < OVERSAMPLE_COUNT; i++) {
    patterns[i].atten = ADC_ATTEN_DB_12;
    patterns[i].channel = FSR1_CHANNEL;
    patterns[i].unit = ADC_UNIT_1;
    patterns[i].bit_width = ADC_BITWIDTH_12;
  }
  for (uint32_t i = OVERSAMPLE_COUNT; i < OVERSAMPLE_COUNT * 2; i++) {
    patterns[i].atten = ADC_ATTEN_DB_12;
    patterns[i].channel = FSR2_CHANNEL;
    patterns[i].unit = ADC_UNIT_1;
    patterns[i].bit_width = ADC_BITWIDTH_12;
  }

  adc_continuous_config_t adcConfig = {};
  adcConfig.pattern_num = OVERSAMPLE_COUNT * 2;
  adcConfig.adc_pattern = patterns;
  adcConfig.sample_freq_hz = ADC_CONVERSIONS_PER_SECOND;
  adcConfig.conv_mode = ADC_CONV_SINGLE_UNIT_1;
  adcConfig.format = ADC_DIGI_OUTPUT_FORMAT_TYPE1;

  if (adc_continuous_config(adcHandle, &adcConfig) != ESP_OK) {
    adc_continuous_deinit(adcHandle);
    adcHandle = nullptr;
    return false;
  }

  return adc_continuous_start(adcHandle) == ESP_OK;
}

void processSerialCommand(String line) {
  line.trim();

  // No acknowledgements are transmitted because any ASCII mixed into the
  // outbound stream would corrupt the continuous raw capture protocol.
  if (line == "PING") {
    return;
  }
  if (line == "STOP") {
    stopMove();
    return;
  }
  if (line == "RECORD,1") {
    isRecording = true;
    return;
  }
  if (line == "RECORD,0") {
    isRecording = false;
    return;
  }
  if (line.startsWith("MOVE,")) {
    int firstComma = line.indexOf(',');
    int secondComma = line.indexOf(',', firstComma + 1);
    int thirdComma = line.indexOf(',', secondComma + 1);
    if (secondComma < 0 || thirdComma < 0) {
      return;
    }
    String direction = line.substring(firstComma + 1, secondComma);
    long steps = line.substring(secondComma + 1, thirdComma).toInt();
    int delayUs = line.substring(thirdComma + 1).toInt();
    bool directionValid = direction == "UP" || direction == "DOWN";
    if (!directionValid || steps < 1 || delayUs < 20 || delayUs > 5000) {
      return;
    }
    startMove(direction == "UP", steps, delayUs);
    return;
  }
  if (line.startsWith("AUTOTEST,")) {
    int c1 = line.indexOf(',');
    int c2 = line.indexOf(',', c1 + 1);
    int c3 = line.indexOf(',', c2 + 1);
    int c4 = line.indexOf(',', c3 + 1);
    if (c1 > 0 && c2 > 0 && c3 > 0 && c4 > 0) {
      autoTestThreshold = line.substring(c1 + 1, c2).toInt();
      int downSpeed = line.substring(c2 + 1, c3).toInt();
      autoTestReturnSpeed = line.substring(c3 + 1, c4).toInt();
      autoTestTotalSteps = line.substring(c4 + 1).toInt();
      autoTestReturnSteps = autoTestTotalSteps;
      
      isAutoTesting = true;
      autoTestTriggered = false;
      startMove(false, autoTestTotalSteps, downSpeed);
    }
    return;
  }
}

void readSerialCommands() {
  while (Serial.available()) {
    char character = static_cast<char>(Serial.read());
    if (character == '\n') {
      processSerialCommand(serialLine);
      serialLine = "";
    } else if (character != '\r' && serialLine.length() < 100) {
      serialLine += character;
    }
  }
}

void handleMove() {
  if (!server.hasArg("dir") || !server.hasArg("steps") ||
      !server.hasArg("speed")) {
    server.send(400, "text/plain", "Missing dir, steps, or speed");
    return;
  }
  String direction = server.arg("dir");
  long steps = server.arg("steps").toInt();
  int delayUs = server.arg("speed").toInt();
  if ((direction != "forward" && direction != "backward") || steps < 1 ||
      delayUs < 20 || delayUs > 5000) {
    server.send(400, "text/plain", "Invalid command values");
    return;
  }
  startMove(direction == "forward", steps, delayUs);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleStop() {
  stopMove();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleSensors() {
  server.send(200, "application/json", sensorJson());
}

void handleRoot() {
  server.send(200, "text/plain", "ESP32 Sensor Tester is ready");
}

void fatalStartupFailure() {
  stopMove();
  while (true) {
    delay(1000);
  }
}

void setup() {
  // TX buffer must be configured before Serial.begin().
  Serial.setTxBufferSize(USB_TX_BUFFER_BYTES);
  Serial.begin(USB_BAUD);
  serialLine.reserve(100);

  pinMode(ENA_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(PUL_PIN, OUTPUT);
  pinMode(FSR1_PIN, INPUT);
  pinMode(FSR2_PIN, INPUT);
  digitalWrite(ENA_PIN, HIGH);
  digitalWrite(PUL_PIN, LOW);
  digitalWrite(DIR_PIN, DOWN_DIRECTION_LEVEL);

  // Existing Arduino-ESP32 3.x motor timer: one tick per microsecond.
  timer = timerBegin(1000000);
  if (timer == nullptr) {
    fatalStartupFailure();
  }
  timerAttachInterrupt(timer, &onTimer);
  timerAlarm(timer, pulseDelayUs, true, 0);
  timerStart(timer);

  binaryStream = xStreamBufferCreate(STREAM_BUFFER_BYTES, 512);
  if (binaryStream == nullptr) {
    fatalStartupFailure();
  }

  // Create consumers before starting DMA so a completed capture can always
  // notify a valid dump task. Both application tasks stay on core 1, leaving
  // the ESP32 Wi-Fi system work on core 0 undisturbed.
  if (xTaskCreatePinnedToCore(
          usbDumpTask,
          "usb-binary-dump",
          3072,
          nullptr,
          1,
          &usbDumpTaskHandle,
          1) != pdPASS) {
    fatalStartupFailure();
  }

  if (!setupContinuousAdc()) {
    fatalStartupFailure();
  }

  if (xTaskCreatePinnedToCore(
          adcDmaTask,
          "adc-dma-20k",
          4096,
          nullptr,
          3,
          &adcTaskHandle,
          1) != pdPASS) {
    fatalStartupFailure();
  }

  // Existing Wi-Fi AP and HTTP routes are unchanged.
  WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
  server.on("/", handleRoot);
  server.on("/move", handleMove);
  server.on("/stop", handleStop);
  server.on("/sensors", handleSensors);
  server.begin();
}

void loop() {
  // Motor pulses run in the hardware timer ISR. DMA acquisition and USB dump
  // run in their own tasks, so this loop remains available for Wi-Fi and input.
  server.handleClient();
  readSerialCommands();

  if (autoTestTriggered) {
    autoTestTriggered = false;
    stopMove();
    startMove(true, autoTestReturnSteps, autoTestReturnSpeed);
  } else if (isAutoTesting && !isMoving) {
    // Reached the end of the downward travel without hitting the threshold.
    isAutoTesting = false;
    startMove(true, autoTestReturnSteps, autoTestReturnSpeed);
  }

  static uint32_t lastLiveTx = 0;
  if (!isRecording && millis() - lastLiveTx >= 30) {
    lastLiveTx = millis();
    Serial.printf("S,%lu,%u,%u\n", (unsigned long)millis(), latestFsr1, latestFsr2);
  }
}
