#include <WiFi.h>
#include <WebServer.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// ESP32 creates this access point for the optional Wi-Fi connection.
const char* WIFI_SSID = "ESP32_Actuator_Control";
const char* WIFI_PASSWORD = "password123";

const int ENA_PIN = 15;
const int DIR_PIN = 2;
const int PUL_PIN = 16;
const int FSR1_PIN = 32;
const int FSR2_PIN = 33;

// High-rate acquisition is USB-only. This board's USB interface is reliable at
// 115200 baud; the compact stream fits at 250 Hz with comfortable headroom.
const uint32_t USB_BAUD = 115200;
const uint32_t SENSOR_SAMPLE_RATE_HZ = 250;
const TickType_t RECORDING_SAMPLE_PERIOD_TICKS = pdMS_TO_TICKS(4);
const TickType_t IDLE_SAMPLE_PERIOD_TICKS = pdMS_TO_TICKS(200);
const size_t SENSOR_QUEUE_DEPTH = 256;

// Swap these two levels if the physical actuator moves opposite to the UI.
const int UP_DIRECTION_LEVEL = HIGH;
const int DOWN_DIRECTION_LEVEL = LOW;

volatile long stepsRemaining = 0;
volatile bool isMoving = false;
volatile int pulseDelayUs = 500;
hw_timer_t* timer = nullptr;
WebServer server(80);

String serialLine;
QueueHandle_t sensorQueue = nullptr;
volatile uint16_t latestFsr1 = 0;
volatile uint16_t latestFsr2 = 0;
volatile uint32_t latestSampleTimeUs = 0;
volatile uint32_t droppedSensorSamples = 0;
volatile bool highRateRecording = false;

struct SensorSample {
  uint32_t timeMs;
  uint16_t fsr1;
  uint16_t fsr2;
};

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
  // Return the latest cached high-rate sample; HTTP never touches the ADC.
  int fsr1 = latestFsr1;
  int fsr2 = latestFsr2;
  uint32_t timeUs = latestSampleTimeUs;

  String json = "{\"time_ms\":" + String((uint32_t)(timeUs / 1000));
  json += ",\"fsr1\":" + String(fsr1);
  json += ",\"fsr2\":" + String(fsr2);
  json += "}";
  return json;
}

// This task only samples and queues. It never prints, serves Wi-Fi, or controls
// the motor. The hardware timer ISR can pre-empt it at any point.
void sensorSamplingTask(void* parameter) {
  TickType_t lastWake = xTaskGetTickCount();

  while (true) {
    TickType_t period = highRateRecording
      ? RECORDING_SAMPLE_PERIOD_TICKS
      : IDLE_SAMPLE_PERIOD_TICKS;
    vTaskDelayUntil(&lastWake, period);

    SensorSample sample = {};
    sample.timeMs = (uint32_t)(esp_timer_get_time() / 1000);

    // A dummy conversion after changing channels improves ADC settling.
    analogRead(FSR1_PIN);
    sample.fsr1 = analogRead(FSR1_PIN);

    analogRead(FSR2_PIN);
    sample.fsr2 = analogRead(FSR2_PIN);

    latestFsr1 = sample.fsr1;
    latestFsr2 = sample.fsr2;
    latestSampleTimeUs = sample.timeMs * 1000UL;

    if (xQueueSend(sensorQueue, &sample, 0) != pdTRUE) {
      droppedSensorSamples++;
    }
  }
}

void streamQueuedSamples() {
  SensorSample sample;
  char line[128];
  // Bound work per loop pass so HTTP and incoming commands remain responsive.
  for (int sent = 0; sent < 16; sent++) {
    if (xQueueReceive(sensorQueue, &sample, 0) != pdTRUE) return;

    // Minimal compact protocol: S,time_ms,fsr1,fsr2
    int length = snprintf(
      line,
      sizeof(line),
      "S,%lu,%u,%u\n",
      (unsigned long)sample.timeMs,
      (unsigned int)sample.fsr1,
      (unsigned int)sample.fsr2
    );
    if (length > 0 && length < (int)sizeof(line)) {
      Serial.write((const uint8_t*)line, length);
    }
  }
}

void acknowledge(const String& command, bool ok = true) {
  Serial.print("{\"type\":\"ack\",\"ok\":");
  Serial.print(ok ? "true" : "false");
  Serial.print(",\"command\":\"");
  Serial.print(command);
  Serial.println("\"}");
}

void processSerialCommand(String line) {
  line.trim();
  if (line == "PING") {
    acknowledge("PING");
    return;
  }
  if (line == "STOP") {
    stopMove();
    acknowledge("STOP");
    return;
  }
  if (line == "RECORD,1") {
    highRateRecording = true;
    acknowledge("RECORD_START");
    return;
  }
  if (line == "RECORD,0") {
    highRateRecording = false;
    acknowledge("RECORD_STOP");
    return;
  }
  if (line.startsWith("MOVE,")) {
    int firstComma = line.indexOf(',');
    int secondComma = line.indexOf(',', firstComma + 1);
    int thirdComma = line.indexOf(',', secondComma + 1);
    if (secondComma < 0 || thirdComma < 0) {
      acknowledge("MOVE", false);
      return;
    }
    String direction = line.substring(firstComma + 1, secondComma);
    long steps = line.substring(secondComma + 1, thirdComma).toInt();
    int delayUs = line.substring(thirdComma + 1).toInt();
    bool directionValid = direction == "UP" || direction == "DOWN";
    if (!directionValid || steps < 1 || delayUs < 20 || delayUs > 5000) {
      acknowledge("MOVE", false);
      return;
    }
    startMove(direction == "UP", steps, delayUs);
    acknowledge("MOVE");
    return;
  }
  acknowledge("UNKNOWN", false);
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
  if (!server.hasArg("dir") || !server.hasArg("steps") || !server.hasArg("speed")) {
    server.send(400, "text/plain", "Missing dir, steps, or speed");
    return;
  }
  String direction = server.arg("dir");
  long steps = server.arg("steps").toInt();
  int delayUs = server.arg("speed").toInt();
  if ((direction != "forward" && direction != "backward") || steps < 1 || delayUs < 20 || delayUs > 5000) {
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

void setup() {
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

  // ESP32 Arduino Core 3.x timer API: 1 MHz gives one tick per microsecond.
  timer = timerBegin(1000000);
  timerAttachInterrupt(timer, &onTimer);
  timerAlarm(timer, pulseDelayUs, true, 0);

  sensorQueue = xQueueCreate(SENSOR_QUEUE_DEPTH, sizeof(SensorSample));
  if (sensorQueue == nullptr) {
    Serial.println("{\"type\":\"error\",\"message\":\"sensor queue allocation failed\"}");
    while (true) delay(1000);
  }

  // Arduino loop normally runs on core 1 at priority 1. The sampling task runs
  // briefly at priority 2, then sleeps until the next 1 ms tick.
  xTaskCreatePinnedToCore(
    sensorSamplingTask,
    "sensor-250hz",
    4096,
    nullptr,
    2,
    nullptr,
    1
  );

  WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
  server.on("/", handleRoot);
  server.on("/move", handleMove);
  server.on("/stop", handleStop);
  server.on("/sensors", handleSensors);
  server.begin();
}

void loop() {
  server.handleClient();
  readSerialCommands();
  streamQueuedSamples();
}
