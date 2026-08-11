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
Servo tiltServo;
HeatmapDisplay heatmapDisplay;

unsigned long lastFrameMs = 0;
bool heartbeatState = false;

// Picks the strongest hotspot (highest peak temp -- most likely a real
// occupant rather than a marginal blob) and maps its column across the
// sensor's field of view to a servo angle, so the vent physically points
// toward whoever is warmest in frame. Returns kServoCenterAngleDeg (park
// position) when nothing is detected.
// Index of the hotspot with the highest peak temperature, or -1 if there
// are none. Shared by both servo axes so pan and tilt always track the same
// blob -- picking independently could aim the two axes at different people.
int strongestHotspot(const Hotspot* spots, int count) {
  if (count <= 0) return -1;
  int strongest = 0;
  for (int i = 1; i < count; ++i) {
    if (spots[i].peak_temp_c > spots[strongest].peak_temp_c) strongest = i;
  }
  return strongest;
}

int angleForHotspots(const Hotspot* spots, int count, int cols) {
  const int strongest = strongestHotspot(spots, count);
  if (strongest < 0 || cols <= 1) return kServoCenterAngleDeg;

  const float colFrac = spots[strongest].col / static_cast<float>(cols - 1);
  const float angle = kServoMinAngleDeg + colFrac * (kServoMaxAngleDeg - kServoMinAngleDeg);
  return constrain(static_cast<int>(angle), kServoMinAngleDeg, kServoMaxAngleDeg);
}

// Tilt axis: hotspot row -> vertical vane angle. Row 0 is the *top* of the
// sensor's field of view, so the mapping is inverted -- a blob near row 0
// should tilt the vane up (toward kTiltMaxAngleDeg), not down.
int tiltAngleForHotspots(const Hotspot* spots, int count, int rows) {
  const int strongest = strongestHotspot(spots, count);
  if (strongest < 0 || rows <= 1) return kTiltCenterAngleDeg;

  const float rowFrac = spots[strongest].row / static_cast<float>(rows - 1);
  const float angle = kTiltMaxAngleDeg - rowFrac * (kTiltMaxAngleDeg - kTiltMinAngleDeg);
  return constrain(static_cast<int>(angle), kTiltMinAngleDeg, kTiltMaxAngleDeg);
}

void onSetpoint(float setpointC) {
  Serial.printf("Received setpoint: %.2f C\n", setpointC);
  // Temperature-level actuation (cycling/modulating the HVAC unit itself)
  // is hardware-specific (docs/hardware-bom.md) and wired up once a
  // relay/compressor interface is chosen; not modeled here. Airflow
  // *direction* is handled separately and immediately in loop() via
  // airflowServo -- it doesn't wait on this MQTT round-trip.
}

// Returns true if the link came up. Never blocks longer than
// kWifiConnectTimeoutMs: everything that makes this device *work* (sensing,
// detection, both servos, LEDs, TFT) is on-device, so a missing AP, wrong
// credentials, or an unplugged u.FL antenna must degrade to "offline", not
// to a dead board stuck in setup().
bool connectWifi() {
#if !defined(SENSOR_MODE_SIM)
  WiFi.mode(WIFI_STA);
  WiFi.begin(kWifiSsid, kWifiPassword);

  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > kWifiConnectTimeoutMs) {
      Serial.println("\nWiFi connect timed out -- running offline");
      return false;
    }
    delay(250);
    Serial.print(".");
  }
  Serial.print("\nWiFi connected, IP ");
  Serial.println(WiFi.localIP());
  return true;
#else
  return false;
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

  tiltServo.setPeriodHertz(50);
  tiltServo.attach(kServoTiltPin, 500, 2400);
  tiltServo.write(kTiltCenterAngleDeg);

  if (!heatmapDisplay.begin()) {
    Serial.println("Heatmap display init failed");
  }

  mqtt.setSetpointCallback(onSetpoint);
  if (!connectWifi()) {
    Serial.println("No WiFi -- MQTT will retry in the background");
  }
  // Called either way: begin() only configures the client when the link is
  // down, and loop() completes the connection if WiFi turns up later.
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
  const int tilt = tiltAngleForHotspots(hotspots, count, sensor.rows());
  airflowServo.write(angle);
  tiltServo.write(tilt);

  heatmapDisplay.render(frame, sensor.rows(), sensor.cols(), hotspots, count);

  Serial.printf("Frame processed: %d hotspot(s), pan %d deg, tilt %d deg\n", count, angle, tilt);
}
