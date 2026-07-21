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
// "occupancy detected"). Final actuation still happens via control/setpoint
// once real relay/damper hardware is chosen (docs/hardware-bom.md).
constexpr int kHeartbeatLedPin = 2;
constexpr int kHotspotLedPin = 4;

// MQTT (PROJECT_PLAN.md section 8)
constexpr char kMqttBroker[] = "192.168.1.10";
constexpr int kMqttPort = 1883;
constexpr char kMqttZone[] = "zone1";
constexpr char kMqttClientId[] = "esp32-thermal-1";

// WiFi -- fill in for real deployment; unused when SENSOR_MODE_SIM runs
// fully offline.
constexpr char kWifiSsid[] = "CHANGE_ME";
constexpr char kWifiPassword[] = "CHANGE_ME";

constexpr unsigned long kFramePeriodMs = 2000;
