#include <Arduino.h>
#include <ESP32Servo.h>
#include <WiFi.h>

#include "HeatmapDisplay.h"
#include "HotspotDetector.h"
#include "MqttManager.h"
#include "ThermalSensor.h"
#include "config.h"

#if defined(SENSOR_MODE_MLX90640)
#include "MLX90640Sensor.h"
#elif defined(SENSOR_MODE_AMG8833)
#include "AMG8833Sensor.h"
#else
#include "SimulatedSensor.h"
#endif

namespace {

#if defined(SENSOR_MODE_MLX90640)
constexpr int kRows = 24;
constexpr int kCols = 32;
MLX90640Sensor sensor;
#elif defined(SENSOR_MODE_AMG8833)
constexpr int kRows = 8;
constexpr int kCols = 8;
AMG8833Sensor sensor;
#else
constexpr int kRows = 24;
constexpr int kCols = 32;
SimulatedSensor sensor(kRows, kCols);
#endif

float frame[kRows * kCols];
HotspotDetector detector(kHotspotDeltaC, kHotspotMinPixels);
Hotspot hotspots[kMaxHotspots];
MqttManager mqtt(kMqttBroker, kMqttPort, kMqttZone, kMqttClientId);
Servo airflowServo;
HeatmapDisplay heatmapDisplay;

unsigned long lastFrameMs = 0;
bool heartbeatState = false;

// Picks the strongest hotspot (highest peak temp -- most likely a real
// occupant rather than a marginal blob) and maps its column across the
// sensor's field of view to a servo angle, so the vent physically points
// toward whoever is warmest in frame. Returns kServoCenterAngleDeg (park
// position) when nothing is detected.
int angleForHotspots(const Hotspot* spots, int count, int cols) {
  if (count <= 0 || cols <= 1) return kServoCenterAngleDeg;

  int strongest = 0;
  for (int i = 1; i < count; ++i) {
    if (spots[i].peak_temp_c > spots[strongest].peak_temp_c) strongest = i;
  }

  const float colFrac = spots[strongest].col / static_cast<float>(cols - 1);
  const float angle = kServoMinAngleDeg + colFrac * (kServoMaxAngleDeg - kServoMinAngleDeg);
  return constrain(static_cast<int>(angle), kServoMinAngleDeg, kServoMaxAngleDeg);
}

void onSetpoint(float setpointC) {
  Serial.printf("Received setpoint: %.2f C\n", setpointC);
  // Temperature-level actuation (cycling/modulating the HVAC unit itself)
  // is hardware-specific (docs/hardware-bom.md) and wired up once a
  // relay/compressor interface is chosen; not modeled here. Airflow
  // *direction* is handled separately and immediately in loop() via
  // airflowServo -- it doesn't wait on this MQTT round-trip.
}

void connectWifi() {
#if !defined(SENSOR_MODE_SIM)
  WiFi.begin(kWifiSsid, kWifiPassword);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi connected");
#endif
}

void publishHotspots(const Hotspot* spots, int count) {
  char json[512];
  int offset = snprintf(json, sizeof(json), "[");
  for (int i = 0; i < count; ++i) {
    offset += snprintf(json + offset, sizeof(json) - offset,
                        "%s{\"row\":%.1f,\"col\":%.1f,\"peak_temp_c\":%.1f}",
                        i == 0 ? "" : ",", spots[i].row, spots[i].col, spots[i].peak_temp_c);
  }
  snprintf(json + offset, sizeof(json) - offset, "]");
  mqtt.publishHotspotsJson(json);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(kHeartbeatLedPin, OUTPUT);
  pinMode(kHotspotLedPin, OUTPUT);

  airflowServo.setPeriodHertz(50);
  airflowServo.attach(kServoPin, 500, 2400);
  airflowServo.write(kServoCenterAngleDeg);

  if (!heatmapDisplay.begin()) {
    Serial.println("Heatmap display init failed");
  }

  connectWifi();
  mqtt.setSetpointCallback(onSetpoint);
  mqtt.begin();

  if (!sensor.begin()) {
    Serial.println("Sensor init failed");
  }
}

void loop() {
  mqtt.loop();

  const unsigned long now = millis();
  if (now - lastFrameMs < kFramePeriodMs) return;
  lastFrameMs = now;

  if (!sensor.readFrame(frame, sensor.rows(), sensor.cols())) {
    Serial.println("Frame read failed");
    return;
  }

  const int count = detector.detect(frame, sensor.rows(), sensor.cols(), hotspots, kMaxHotspots);
  publishHotspots(hotspots, count);
  mqtt.publishStatus("online");

  heartbeatState = !heartbeatState;
  digitalWrite(kHeartbeatLedPin, heartbeatState ? HIGH : LOW);
  digitalWrite(kHotspotLedPin, count > 0 ? HIGH : LOW);

  const int angle = angleForHotspots(hotspots, count, sensor.cols());
  airflowServo.write(angle);

  heatmapDisplay.render(frame, sensor.rows(), sensor.cols(), hotspots, count);

  Serial.printf("Frame processed: %d hotspot(s), servo angle %d\n", count, angle);
}
