#pragma once

// Exactly one of these is set via platformio.ini build_flags.
#if !defined(SENSOR_MODE_SIM) && !defined(SENSOR_MODE_MLX90640) && !defined(SENSOR_MODE_AMG8833)
#error "Define SENSOR_MODE via platformio.ini build_flags: SIM, MLX90640, or AMG8833"
#endif

// Validated hotspot-detection thresholds (PROJECT_PLAN.md section 4,
// 1000-frame synthetic eval). SIM reuses the MLX90640 thresholds since
// SimulatedSensor generates 24x32 frames.
#if defined(SENSOR_MODE_AMG8833)
constexpr float kHotspotDeltaC = 3.5f;
constexpr int kHotspotMinPixels = 1;
#else
constexpr float kHotspotDeltaC = 4.5f;
constexpr int kHotspotMinPixels = 2;
#endif

constexpr int kMaxHotspots = 16;

// Demo/Wokwi indicators only -- not part of the documented control
// architecture (that's controller/airflow_controller.py's job). These just
// give the simulation something visible: kHeartbeatLedPin toggles once per
// frame cycle (proof the main loop is alive), kHotspotLedPin lights while
// the current frame has at least one detected hotspot (visual proxy for
// "occupancy detected").
constexpr int kHeartbeatLedPin = 2;
constexpr int kHotspotLedPin = 4;

// Airflow-direction actuator (docs/hardware-bom.md: SG90/MG90S micro servo
// on a louvre/vent flap). This is an on-device reflex, separate from the
// MQTT control/setpoint path: main.cpp maps the strongest detected
// hotspot's column directly to a servo angle every frame, so the vent
// tracks occupants without waiting on the host controller round-trip.
// setpoint_c/cooling_level (temperature-level decisions from the drift
// model) still come from controller/airflow_controller.py as documented.
// GPIO18 moved off this pin (was the servo's home) to make room for the
// TFT's hardware VSPI bus, which claims 18/19/23 on the ESP32.
constexpr int kServoPin = 13;
constexpr int kServoMinAngleDeg = 0;
constexpr int kServoMaxAngleDeg = 180;
constexpr int kServoCenterAngleDeg = 90;  // no hotspot: park at center

// Second axis (tilt) on the pan/tilt bracket: maps the strongest hotspot's
// *row* to a vertical vane angle, the same way kServoPin maps its column.
// Deliberately a narrower sweep than the pan axis -- a vent flap tilting a
// full 180 deg would just point at the ceiling or the floor.
constexpr int kServoTiltPin = 25;
constexpr int kTiltMinAngleDeg = 60;
constexpr int kTiltMaxAngleDeg = 120;
constexpr int kTiltCenterAngleDeg = 90;

// ILI9341 2.4" SPI TFT -- live color heatmap of the current thermal frame
// plus detected-hotspot markers (HeatmapDisplay.h/.cpp). Uses the ESP32's
// hardware VSPI bus: SCK/MISO/MOSI are fixed by the peripheral, CS/DC/RST
// are freely chosen GPIOs. Backlight (LED pin) is wired straight to 3V3 on
// the physical board -- no GPIO spent on backlight dimming.
constexpr int kTftSckPin = 18;   // VSPI SCK (fixed)
constexpr int kTftMisoPin = 19;  // VSPI MISO (fixed, unused by the display)
constexpr int kTftMosiPin = 23;  // VSPI MOSI (fixed)
constexpr int kTftCsPin = 15;
constexpr int kTftDcPin = 27;
constexpr int kTftRstPin = 26;

// MQTT (PROJECT_PLAN.md section 8)
constexpr char kMqttBroker[] = "192.168.1.10";
constexpr int kMqttPort = 1883;
constexpr char kMqttZone[] = "zone1";
constexpr char kMqttClientId[] = "esp32-thermal-1";

// WiFi -- fill in for real deployment; unused when SENSOR_MODE_SIM runs
// fully offline.
constexpr char kWifiSsid[] = "CHANGE_ME";
constexpr char kWifiPassword[] = "CHANGE_ME";

// Hard cap on the WiFi join attempt. On expiry the firmware logs and
// carries on *offline* -- sensing, hotspot detection, both servos, the LEDs
// and the TFT heatmap are all on-device and must never be held hostage by a
// missing AP, wrong credentials, or (on an ESP32-WROOM-32U) a missing
// external antenna. MQTT reconnects opportunistically in the background.
constexpr unsigned long kWifiConnectTimeoutMs = 10000;
constexpr unsigned long kMqttReconnectIntervalMs = 5000;

constexpr unsigned long kFramePeriodMs = 2000;
