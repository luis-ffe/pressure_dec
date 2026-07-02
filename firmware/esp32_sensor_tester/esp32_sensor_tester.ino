#include <Arduino.h>
#include "FastAccelStepper.h"
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_timer.h>
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/stream_buffer.h>

#if !CONFIG_IDF_TARGET_ESP32
#error "This sketch targets the classic ESP32 pinout used by the actuator controller."
#endif

// ESP32 creates this access point for the optional Wi-Fi connection.
const char* WIFI_SSID = "ESP32_Actuator_Control";
const char* WIFI_PASSWORD = "password123";

// Existing motor wiring. FastAccelStepper generates the pulse train.
const int ENA_PIN = 15;
const int DIR_PIN = 2;
const int PUL_PIN = 16;

// ADS1256 external ADC wiring.
// FSR1 = ADS1256 AIN1, FSR2 = ADS1256 AIN2, both read single-ended vs AINCOM/GND.
constexpr int ADS_CS_PIN = 5;
constexpr int ADS_DRDY_PIN = 27;
constexpr int ADS_SCK_PIN = 18;
constexpr int ADS_MOSI_PIN = 23;
constexpr int ADS_MISO_PIN = 19;

// FSR2 resistor-selection analog mux control.
// Channel order:
// C0 330R, C1 1k, C2 2.2k, C3 4.7k, C4 10k, C5 20k, C6 47k, C7 68k,
// C8 100k, C9 220k, C10 300k, C11 470k, C12 680k, C13 1M, C14 4.7M, C15 5.6M.
constexpr int FSR2_MUX_S0_PIN = 14;
constexpr int FSR2_MUX_S1_PIN = 26;
constexpr int FSR2_MUX_S2_PIN = 25;
constexpr int FSR2_MUX_S3_PIN = 33;
constexpr uint8_t FSR2_DEFAULT_RESISTANCE_CHANNEL = 4;  // C4 = 10 kΩ.

// Use a conservative baud while validating the C++ app <-> ESP32 command path.
// The ADS1256 stream is about 2 KB/s at 500 pairs/s, so 115200 is sufficient.
constexpr uint32_t USB_BAUD = 115200;
constexpr size_t USB_TX_BUFFER_BYTES = 1024;
constexpr size_t STREAM_BUFFER_BYTES = 16384;
constexpr size_t USB_DUMP_CHUNK_BYTES = 512;
constexpr uint32_t USB_PREVIEW_PERIOD_MS = 200;

constexpr uint32_t SAMPLE_PAIRS_PER_SECOND = 500;
constexpr uint32_t SAMPLE_PAIR_PERIOD_US = 1000000UL / SAMPLE_PAIRS_PER_SECOND;
constexpr uint32_t ADS_SPI_CLOCK_HZ = 1000000;
constexpr uint32_t ADS_DRDY_TIMEOUT_US = 50000;

constexpr uint8_t ADS_CMD_WAKEUP = 0x00;
constexpr uint8_t ADS_CMD_RDATA = 0x01;
constexpr uint8_t ADS_CMD_SDATAC = 0x0F;
constexpr uint8_t ADS_CMD_RREG = 0x10;
constexpr uint8_t ADS_CMD_WREG = 0x50;
constexpr uint8_t ADS_CMD_SELFCAL = 0xF0;
constexpr uint8_t ADS_CMD_SYNC = 0xFC;

constexpr uint8_t ADS_REG_STATUS = 0x00;
constexpr uint8_t ADS_REG_MUX = 0x01;
constexpr uint8_t ADS_REG_ADCON = 0x02;
constexpr uint8_t ADS_REG_DRATE = 0x03;

constexpr uint8_t ADS_MUX_AINCOM = 0x08;
// ADS1256 sensor mapping.
constexpr uint8_t ADS_FSR1_CHANNEL = 1;  // FSR1 signal on AIN1.
constexpr uint8_t ADS_FSR2_CHANNEL = 2;  // FSR2 signal on AIN2.
constexpr uint8_t ADS_SINGLE_ENDED_NEGATIVE = ADS_MUX_AINCOM;
constexpr uint8_t ADS_DRATE_1000SPS = 0xA1;
constexpr uint8_t ADS_ADCON_CLOCK_OUT_OFF = 0x00;
constexpr uint8_t ADS_ADCON_SENSOR_DETECT_OFF = 0x00;
constexpr uint8_t ADS_ADCON_PGA_MASK = 0x07;
constexpr uint8_t ADS_ADCON_PGA_GAIN_1 = 0x00;
constexpr uint8_t ADS_ADCON_FORCE_GAIN_1 =
    ADS_ADCON_CLOCK_OUT_OFF | ADS_ADCON_SENSOR_DETECT_OFF | ADS_ADCON_PGA_GAIN_1;
constexpr uint32_t ADS_ENGINEERING_MAX = 5000;
const SPISettings ADS_SPI_SETTINGS(ADS_SPI_CLOCK_HZ, MSBFIRST, SPI_MODE1);

// Swap these two levels if the physical actuator moves opposite to the UI.
const int UP_DIRECTION_LEVEL = HIGH;
const int DOWN_DIRECTION_LEVEL = LOW;

WebServer server(80);
FastAccelStepperEngine stepperEngine = FastAccelStepperEngine();
FastAccelStepper* stepper = nullptr;

String serialLine;

// Each capture entry is exactly four bytes in little-endian order:
//   uint16_t fsr1, uint16_t fsr2
// The USB payload is an indefinite stream of these 4-byte structs.
struct FsrSamplePair {
  uint16_t fsr1;
  uint16_t fsr2;
};
static_assert(sizeof(FsrSamplePair) == 4, "FSR pair must be exactly four bytes");

volatile bool isRecording = false;
volatile bool isAutoTesting = false;
volatile bool autoTestTriggered = false;
volatile uint16_t autoTestThreshold = 4000;
long autoTestTotalSteps = 1000;
long autoTestReturnSteps = 1000;
int autoTestReturnSpeed = 500;
volatile uint16_t latestFsr1 = 0;
volatile uint16_t latestFsr2 = 0;
volatile int32_t latestAdsRaw = 0;
volatile uint32_t latestSampleTimeUs = 0;
volatile uint32_t adcReadErrors = 0;
volatile bool adsReady = false;
volatile uint8_t latestAdsAdcon = 0xFF;
volatile uint32_t adsPgaCorrections = 0;
volatile uint8_t fsr2ResistanceChannel = FSR2_DEFAULT_RESISTANCE_CHANNEL;

TaskHandle_t adcTaskHandle = nullptr;
TaskHandle_t usbDumpTaskHandle = nullptr;
StreamBufferHandle_t binaryStream = nullptr;
bool stepperReady = false;
volatile int pulseDelayUs = 1000;

bool motorIsRunning() {
  return stepperReady && stepper && stepper->isRunning();
}

uint8_t adsPgaGainFromAdcon(uint8_t adcon) {
  switch (adcon & ADS_ADCON_PGA_MASK) {
    case 0x00: return 1;
    case 0x01: return 2;
    case 0x02: return 4;
    case 0x03: return 8;
    case 0x04: return 16;
    case 0x05: return 32;
    case 0x06: return 64;
    default: return 0;
  }
}

long motorStepsRemaining() {
  if (!stepperReady || !stepper) {
    return 0;
  }
  return labs(stepper->targetPos() - stepper->getCurrentPosition());
}

uint32_t speedHzFromPulseDelay(int delayUs) {
  const uint32_t safeDelayUs = constrain(delayUs, 20, 5000);
  // The desktop app's "pulse delay" value is the historical delay between
  // steps from the original bit-banged motor code. FastAccelStepper wants
  // steps/second, so preserve that UI meaning as 1 step every delayUs.
  return max(1UL, 1000000UL / safeDelayUs);
}

void startMove(bool up, long steps, int requestedDelayUs) {
  if (steps < 1 || !stepperReady || !stepper) return;
  pulseDelayUs = constrain(requestedDelayUs, 20, 5000);
  stepper->setSpeedInHz(speedHzFromPulseDelay(pulseDelayUs));
  stepper->setCurrentPosition(0);
  stepper->move(up ? steps : -steps);
}

void stopMove() {
  if (stepperReady && stepper) {
    stepper->forceStop();
    stepper->disableOutputs();
  }
}

bool setFsr2ResistanceChannel(uint8_t channel) {
  if (channel > 15) {
    return false;
  }
  fsr2ResistanceChannel = channel;
  digitalWrite(FSR2_MUX_S0_PIN, (channel & 0x01) ? HIGH : LOW);
  digitalWrite(FSR2_MUX_S1_PIN, (channel & 0x02) ? HIGH : LOW);
  digitalWrite(FSR2_MUX_S2_PIN, (channel & 0x04) ? HIGH : LOW);
  digitalWrite(FSR2_MUX_S3_PIN, (channel & 0x08) ? HIGH : LOW);
  return true;
}

void setupFsr2ResistanceMux() {
  pinMode(FSR2_MUX_S0_PIN, OUTPUT);
  pinMode(FSR2_MUX_S1_PIN, OUTPUT);
  pinMode(FSR2_MUX_S2_PIN, OUTPUT);
  pinMode(FSR2_MUX_S3_PIN, OUTPUT);
  setFsr2ResistanceChannel(FSR2_DEFAULT_RESISTANCE_CHANNEL);
}

String sensorJson() {
  // HTTP returns only the latest cached ADS1256 pair; it never blocks on SPI.
  uint16_t fsr1 = latestFsr1;
  uint16_t fsr2 = latestFsr2;
  uint32_t timeUs = latestSampleTimeUs;

  String json = "{\"time_ms\":" + String((uint32_t)(timeUs / 1000));
  json += ",\"fsr1\":" + String(fsr1);
  json += ",\"fsr2\":" + String(fsr2);
  json += ",\"ads_ready\":" + String(adsReady ? "true" : "false");
  json += ",\"stepper_ready\":" + String(stepperReady ? "true" : "false");
  json += ",\"moving\":" + String(motorIsRunning() ? "true" : "false");
  json += ",\"steps_remaining\":" + String(motorStepsRemaining());
  json += ",\"adc_errors\":" + String(adcReadErrors);
  json += ",\"ads_adcon\":" + String(latestAdsAdcon);
  json += ",\"ads_pga_gain\":" + String(adsPgaGainFromAdcon(latestAdsAdcon));
  json += ",\"ads_pga_corrections\":" + String(adsPgaCorrections);
  json += ",\"fsr2_resistance_channel\":" + String(fsr2ResistanceChannel);
  json += "}";
  return json;
}

void adsSelect() {
  SPI.beginTransaction(ADS_SPI_SETTINGS);
  digitalWrite(ADS_CS_PIN, LOW);
}

void adsDeselect() {
  digitalWrite(ADS_CS_PIN, HIGH);
  SPI.endTransaction();
}

void adsCommand(uint8_t command) {
  adsSelect();
  SPI.transfer(command);
  adsDeselect();
  delayMicroseconds(4);
}

void adsWriteRegister(uint8_t reg, uint8_t value) {
  adsSelect();
  SPI.transfer(ADS_CMD_WREG | reg);
  SPI.transfer(0x00);  // Write one register.
  SPI.transfer(value);
  adsDeselect();
  delayMicroseconds(4);
}

uint8_t adsReadRegister(uint8_t reg) {
  adsSelect();
  SPI.transfer(ADS_CMD_RREG | reg);
  SPI.transfer(0x00);  // Read one register.
  delayMicroseconds(10);
  const uint8_t value = SPI.transfer(0xFF);
  adsDeselect();
  delayMicroseconds(4);
  return value;
}

bool adsForcePgaGain1() {
  adsWriteRegister(ADS_REG_ADCON, ADS_ADCON_FORCE_GAIN_1);
  const uint8_t adcon = adsReadRegister(ADS_REG_ADCON);
  latestAdsAdcon = adcon;
  const bool isGain1 = (adcon & ADS_ADCON_PGA_MASK) == ADS_ADCON_PGA_GAIN_1;
  if (!isGain1) {
    adcReadErrors++;
  }
  return isGain1;
}

void adsEnsurePgaGain1() {
  const uint8_t adcon = adsReadRegister(ADS_REG_ADCON);
  latestAdsAdcon = adcon;
  if ((adcon & ADS_ADCON_PGA_MASK) != ADS_ADCON_PGA_GAIN_1) {
    adsPgaCorrections++;
    adsForcePgaGain1();
  }
}

bool adsWaitForDrdy(uint32_t timeoutUs = ADS_DRDY_TIMEOUT_US) {
  const uint32_t start = micros();
  while (digitalRead(ADS_DRDY_PIN) == HIGH) {
    if ((uint32_t)(micros() - start) >= timeoutUs) {
      adcReadErrors++;
      return false;
    }
    if ((uint32_t)(micros() - start) > 1000) {
      vTaskDelay(pdMS_TO_TICKS(1));
    } else {
      delayMicroseconds(10);
    }
  }
  return true;
}

bool adsWaitForDrdyCycle(uint32_t timeoutUs = ADS_DRDY_TIMEOUT_US) {
  const uint32_t start = micros();
  while (digitalRead(ADS_DRDY_PIN) == LOW) {
    if ((uint32_t)(micros() - start) >= timeoutUs) {
      adcReadErrors++;
      return false;
    }
    if ((uint32_t)(micros() - start) > 1000) {
      vTaskDelay(pdMS_TO_TICKS(1));
    } else {
      delayMicroseconds(5);
    }
  }
  return adsWaitForDrdy(timeoutUs);
}

int32_t adsReadDataRaw() {
  adsSelect();
  SPI.transfer(ADS_CMD_RDATA);
  delayMicroseconds(10);
  const uint8_t b0 = SPI.transfer(0xFF);
  const uint8_t b1 = SPI.transfer(0xFF);
  const uint8_t b2 = SPI.transfer(0xFF);
  adsDeselect();

  int32_t value = ((int32_t)b0 << 16) | ((int32_t)b1 << 8) | b2;
  if (value & 0x800000) {
    value |= 0xFF000000;
  }
  return value;
}

bool adsSetMux(uint8_t positiveChannel, uint8_t negativeChannel) {
  adsWriteRegister(ADS_REG_MUX, (positiveChannel << 4) | negativeChannel);
  adsCommand(ADS_CMD_SYNC);
  adsCommand(ADS_CMD_WAKEUP);
  return adsWaitForDrdyCycle();
}

int32_t adsReadFixedChannelRaw() {
  if (!adsWaitForDrdy()) {
    return 0;
  }
  return adsReadDataRaw();
}

int32_t adsReadSingleEndedRaw(uint8_t positiveChannel) {
  if (!adsSetMux(positiveChannel, ADS_SINGLE_ENDED_NEGATIVE)) {
    return 0;
  }
  (void)adsReadDataRaw();  // Discard first conversion after mux switch.

  if (!adsWaitForDrdyCycle()) {
    return 0;
  }
  return adsReadDataRaw();
}

uint16_t adsRawToUint16(int32_t raw) {
  if (raw <= 0) {
    return 0;
  }
  if (raw > 0x7FFFFF) {
    raw = 0x7FFFFF;
  }
  return static_cast<uint16_t>(
      (static_cast<uint64_t>(raw) * static_cast<uint64_t>(ADS_ENGINEERING_MAX)) / 0x7FFFFFULL);
}

uint16_t median5(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint16_t e) {
  uint16_t values[5] = {a, b, c, d, e};
  for (uint8_t i = 1; i < 5; ++i) {
    const uint16_t key = values[i];
    int8_t j = i - 1;
    while (j >= 0 && values[j] > key) {
      values[j + 1] = values[j];
      --j;
    }
    values[j + 1] = key;
  }
  return values[2];
}

uint16_t medianFilteredValue(uint16_t rawValue, uint16_t history[5], uint8_t& index, bool& filled) {
  history[index] = rawValue;
  index = (index + 1) % 5;
  if (index == 0) {
    filled = true;
  }
  if (!filled) {
    return rawValue;
  }
  return median5(history[0], history[1], history[2], history[3], history[4]);
}

void handleAutoTestThreshold(uint16_t fsr1, uint16_t fsr2) {
  static uint16_t consecutiveOverThreshold = 0;
  if (!isAutoTesting) {
    consecutiveOverThreshold = 0;
    return;
  }

  if (fsr1 >= autoTestThreshold || fsr2 >= autoTestThreshold) {
    consecutiveOverThreshold++;
    if (consecutiveOverThreshold > 10) {  // ~10 ms at 1 k sample-pairs/s.
      if (stepperReady && stepper) {
        autoTestReturnSteps = labs(stepper->getCurrentPosition());
        stepper->forceStop();
      } else {
        autoTestReturnSteps = autoTestTotalSteps;
      }
      autoTestTriggered = true;
      isAutoTesting = false;
      consecutiveOverThreshold = 0;
    }
  } else {
    consecutiveOverThreshold = 0;
  }
}

void adsAcquisitionTask(void* parameter) {
  uint32_t nextPairTimeUs = micros();
  uint16_t fsr1History[5] = {};
  uint16_t fsr2History[5] = {};
  uint8_t fsr1HistoryIndex = 0;
  uint8_t fsr2HistoryIndex = 0;
  bool fsr1HistoryFilled = false;
  bool fsr2HistoryFilled = false;

  while (true) {
    if (!adsReady) {
      adsReady = setupAds1256();
      if (!adsReady) {
        latestSampleTimeUs = (uint32_t)esp_timer_get_time();
        vTaskDelay(pdMS_TO_TICKS(1000));
        nextPairTimeUs = micros();
        continue;
      }
    }

    static uint16_t pgaVerifyCounter = 0;
    if (++pgaVerifyCounter >= 250) {
      pgaVerifyCounter = 0;
      adsEnsurePgaGain1();
    }

    const int32_t fsr1AdsRaw = adsReadSingleEndedRaw(ADS_FSR1_CHANNEL);
    const int32_t fsr2AdsRaw = adsReadSingleEndedRaw(ADS_FSR2_CHANNEL);
    latestAdsRaw = fsr1AdsRaw;
    const uint16_t fsr1Raw = adsRawToUint16(fsr1AdsRaw);
    const uint16_t fsr2Raw = adsRawToUint16(fsr2AdsRaw);
    const uint16_t fsr1 = medianFilteredValue(fsr1Raw, fsr1History, fsr1HistoryIndex, fsr1HistoryFilled);
    const uint16_t fsr2 = medianFilteredValue(fsr2Raw, fsr2History, fsr2HistoryIndex, fsr2HistoryFilled);
    const FsrSamplePair pair = {fsr1, fsr2};

    latestFsr1 = pair.fsr1;
    latestFsr2 = pair.fsr2;
    latestSampleTimeUs = (uint32_t)esp_timer_get_time();

    handleAutoTestThreshold(pair.fsr1, pair.fsr2);

    if (isRecording) {
      xStreamBufferSend(binaryStream, &pair, sizeof(pair), 0);
    }

    nextPairTimeUs += SAMPLE_PAIR_PERIOD_US;
    const int32_t waitUs = (int32_t)(nextPairTimeUs - micros());
    if (waitUs > 1000) {
      vTaskDelay(pdMS_TO_TICKS(waitUs / 1000));
    } else if (waitUs > 0) {
      delayMicroseconds(waitUs);
    } else {
      nextPairTimeUs = micros();
      taskYIELD();
    }
    // Always give the Arduino loop time to process USB motor commands.
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void usbDumpTask(void* parameter) {
  uint8_t buf[USB_DUMP_CHUNK_BYTES];
  while (true) {
    while (!isRecording) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Tell the desktop host that a continuous binary stream will follow.
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
    
    // We stopped recording. Give the acquisition task a moment to notice `isRecording == false`.
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

bool setupAds1256() {
  pinMode(ADS_CS_PIN, OUTPUT);
  digitalWrite(ADS_CS_PIN, HIGH);
  pinMode(ADS_DRDY_PIN, INPUT);

  SPI.begin(ADS_SCK_PIN, ADS_MISO_PIN, ADS_MOSI_PIN, ADS_CS_PIN);
  delay(100);

  adsCommand(ADS_CMD_SDATAC);
  adsWriteRegister(ADS_REG_STATUS, 0x00);          // MSB first, no input buffer.
  const bool pgaOk = adsForcePgaGain1();           // Clock out off, sensor detect off, PGA gain = 1.
  adsWriteRegister(ADS_REG_DRATE, ADS_DRATE_1000SPS);
  adsCommand(ADS_CMD_SELFCAL);
  const bool calibrated = adsWaitForDrdy(1000000);
  const bool muxOk = adsSetMux(ADS_FSR1_CHANNEL, ADS_SINGLE_ENDED_NEGATIVE);

  return calibrated && pgaOk && muxOk;
}

void processSerialCommand(String line) {
  line.trim();
  const bool canSendAsciiReply = !isRecording;

  if (line == "PING") {
    if (canSendAsciiReply) {
      Serial.print("PONG,ESP32_SENSOR_TESTER_ADS1256,");
      Serial.print(stepperReady ? "MOTOR_READY" : "MOTOR_NOT_READY");
      Serial.print(",");
      Serial.print(adsReady ? "ADS_READY" : "ADS_NOT_READY");
      Serial.print(",ADCON=0x");
      if (latestAdsAdcon < 0x10) {
        Serial.print("0");
      }
      Serial.print(latestAdsAdcon, HEX);
      Serial.print(",PGA=");
      Serial.print(adsPgaGainFromAdcon(latestAdsAdcon));
      Serial.print(",MAP=FSR1_AIN");
      Serial.print(ADS_FSR1_CHANNEL);
      Serial.print("_FSR2_AIN");
      Serial.print(ADS_FSR2_CHANNEL);
      Serial.print(",FSR2_RES=C");
      Serial.println(fsr2ResistanceChannel);
    }
    return;
  }
  if (line.startsWith("FSR2_RES,")) {
    const int channel = line.substring(line.indexOf(',') + 1).toInt();
    if (channel < 0 || channel > 15 || !setFsr2ResistanceChannel(static_cast<uint8_t>(channel))) {
      if (canSendAsciiReply) {
        Serial.println("ERR,FSR2_RES_BAD_CHANNEL");
      }
      return;
    }
    if (canSendAsciiReply) {
      Serial.printf("OK,FSR2_RES,C%u\n", fsr2ResistanceChannel);
    }
    return;
  }
  if (line == "RAW") {
    if (canSendAsciiReply) {
      const int32_t raw = latestAdsRaw;
      const uint32_t raw24 = static_cast<uint32_t>(raw) & 0x00FFFFFFUL;
      Serial.printf(
          "RAW,%lu,%ld,0x%06lX,%u,%u,0x%02X\n",
          (unsigned long)millis(),
          (long)raw,
          (unsigned long)raw24,
          latestFsr1,
          latestFsr2,
          latestAdsAdcon);
    }
    return;
  }
  if (line == "STOP") {
    stopMove();
    if (canSendAsciiReply) {
      Serial.println("OK,STOP");
    }
    return;
  }
  if (line == "RECORD,1") {
    isRecording = true;
    return;
  }
  if (line == "RECORD,0") {
    isRecording = false;
    // Do not print here. The desktop app is still inside the binary capture
    // parser until it receives BINARY_CAPTURE_STOP from usbDumpTask.
    return;
  }
  if (line.startsWith("F,") || line.startsWith("B,")) {
    int firstComma = line.indexOf(',');
    int secondComma = line.indexOf(',', firstComma + 1);
    if (firstComma < 0 || secondComma < 0) {
      return;
    }
    long steps = line.substring(firstComma + 1, secondComma).toInt();
    int delayUs = line.substring(secondComma + 1).toInt();
    if (steps < 1 || delayUs < 20 || delayUs > 5000) {
      if (canSendAsciiReply) {
        Serial.println("ERR,CLOSED_LOOP_BAD_VALUES");
      }
      return;
    }
    // Closed-loop profile commands:
    //   F = press forward/increase pressure, same physical direction as AUTOTEST down
    //   B = back off/decrease pressure
    startMove(line[0] == 'B', steps, delayUs);
    if (canSendAsciiReply) {
      Serial.printf("OK,%c,%ld,%d\n", line[0], steps, delayUs);
    }
    return;
  }
  if (line.startsWith("MOVE,")) {
    int firstComma = line.indexOf(',');
    int secondComma = line.indexOf(',', firstComma + 1);
    int thirdComma = line.indexOf(',', secondComma + 1);
    if (secondComma < 0 || thirdComma < 0) {
      if (canSendAsciiReply) {
        Serial.println("ERR,MOVE_PARSE");
      }
      return;
    }
    String direction = line.substring(firstComma + 1, secondComma);
    long steps = line.substring(secondComma + 1, thirdComma).toInt();
    int delayUs = line.substring(thirdComma + 1).toInt();
    bool directionValid = direction == "UP" || direction == "DOWN";
    if (!directionValid || steps < 1 || delayUs < 20 || delayUs > 5000) {
      if (canSendAsciiReply) {
        Serial.println("ERR,MOVE_BAD_VALUES");
      }
      return;
    }
    startMove(direction == "UP", steps, delayUs);
    if (canSendAsciiReply) {
      Serial.printf("OK,MOVE,%s,%ld,%d\n", direction.c_str(), steps, delayUs);
    }
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
      if (canSendAsciiReply) {
        Serial.printf("OK,AUTOTEST,%u,%d,%d,%ld\n",
                      autoTestThreshold,
                      downSpeed,
                      autoTestReturnSpeed,
                      autoTestTotalSteps);
      }
    }
    return;
  }

  if (canSendAsciiReply && line.length() > 0) {
    Serial.print("ERR,UNKNOWN_COMMAND,");
    Serial.println(line);
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

void handleFsr2Resistance() {
  if (!server.hasArg("channel")) {
    server.send(400, "text/plain", "Missing channel");
    return;
  }
  const int channel = server.arg("channel").toInt();
  if (channel < 0 || channel > 15 || !setFsr2ResistanceChannel(static_cast<uint8_t>(channel))) {
    server.send(400, "text/plain", "Invalid channel");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true,\"channel\":" + String(fsr2ResistanceChannel) + "}");
}

void handleRoot() {
  server.send(200, "text/plain", "ESP32 Sensor Tester is ready");
}

void fatalStartupFailure() {
  stopMove();
  Serial.println("ERR,FATAL_STARTUP");
  Serial.flush();
  while (true) {
    Serial.println("ERR,FATAL_STARTUP");
    Serial.flush();
    delay(1000);
  }
}

void setup() {
  // TX buffer must be configured before Serial.begin().
  Serial.setTxBufferSize(USB_TX_BUFFER_BYTES);
  Serial.begin(USB_BAUD);
  serialLine.reserve(100);
  Serial.println("BOOT,ESP32_SENSOR_TESTER_ADS1256");
  Serial.flush();

  pinMode(ENA_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(PUL_PIN, OUTPUT);
  digitalWrite(ENA_PIN, HIGH);
  digitalWrite(PUL_PIN, LOW);
  digitalWrite(DIR_PIN, DOWN_DIRECTION_LEVEL);
  setupFsr2ResistanceMux();
  Serial.printf("BOOT,FSR2_RES,C%u\n", fsr2ResistanceChannel);

  stepperEngine.init();
  stepper = stepperEngine.stepperConnectToPin(PUL_PIN);
  if (stepper) {
    stepper->setDirectionPin(DIR_PIN, UP_DIRECTION_LEVEL == HIGH);
    stepper->setEnablePin(ENA_PIN, true);  // ENA low enables the common driver wiring.
    stepper->setAutoEnable(true);
    stepper->setDelayToEnable(50);
    stepper->setDelayToDisable(100);
    stepper->setSpeedInHz(speedHzFromPulseDelay(pulseDelayUs));
    // Keep manual button moves responsive. The previous bit-banged pulse code
    // effectively changed speed immediately; a low acceleration here makes the
    // actuator feel much slower even when the same 62 µs pulse delay is used.
    stepper->setAcceleration(200000);
    stepper->disableOutputs();
    stepperReady = true;
    Serial.println("BOOT,FASTACCEL_READY");
  } else {
    stepperReady = false;
    Serial.println("ERR,FASTACCEL_INIT_FAILED");
  }

  binaryStream = xStreamBufferCreate(STREAM_BUFFER_BYTES, 512);
  if (binaryStream == nullptr) {
    fatalStartupFailure();
  }

  // Create the USB dump consumer before recording can start.
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

  // Do not initialize ADS1256 in setup. If the ADC is disconnected, held in
  // reset, or wired incorrectly, setup must still reach the motor/USB command
  // loop. The acquisition task initializes and retries the ADS1256 in the
  // background.
  adsReady = false;
  Serial.println("BOOT,ADS1256_BACKGROUND_INIT");

  if (xTaskCreatePinnedToCore(
          adsAcquisitionTask,
          "ads1256-acq",
          4096,
          nullptr,
          1,
          &adcTaskHandle,
          0) != pdPASS) {
    fatalStartupFailure();
  }

  // Existing Wi-Fi AP and HTTP routes are unchanged.
  WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
  server.on("/", handleRoot);
  server.on("/move", handleMove);
  server.on("/stop", handleStop);
  server.on("/sensors", handleSensors);
  server.on("/fsr2_res", handleFsr2Resistance);
  server.begin();
  Serial.println("BOOT,READY");
  Serial.flush();
}

void loop() {
  // Motor pulses run inside FastAccelStepper. ADS acquisition and USB dump
  // run in their own tasks, so this loop remains available for Wi-Fi and input.
  server.handleClient();
  readSerialCommands();

  if (autoTestTriggered) {
    autoTestTriggered = false;
    startMove(true, autoTestReturnSteps, autoTestReturnSpeed);
  } else if (isAutoTesting && !motorIsRunning()) {
    // Reached the end of the downward travel without hitting the threshold.
    isAutoTesting = false;
    autoTestReturnSteps = autoTestTotalSteps;
    startMove(true, autoTestReturnSteps, autoTestReturnSpeed);
  }

  static uint32_t lastLiveTx = 0;
  if (!isRecording && millis() - lastLiveTx >= USB_PREVIEW_PERIOD_MS) {
    lastLiveTx = millis();
    Serial.printf("S,%lu,%u,%u\n", (unsigned long)millis(), latestFsr1, latestFsr2);
  }
}
