#include <Arduino.h>
#include <WiFi.h>

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

unsigned long lastFrameMs = 0;

void onSetpoint(float setpointC) {
  Serial.printf("Received setpoint: %.2f C\n", setpointC);
  // Relay/damper actuation is hardware-specific (docs/hardware-bom.md) and
  // wired up once that hardware is purchased; not modeled here.
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
}
