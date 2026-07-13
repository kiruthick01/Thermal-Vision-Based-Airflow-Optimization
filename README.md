# Thermal Vision-Based Intelligent Airflow Optimization

ESP32 + thermal-camera (MLX90640 or AMG8833) HVAC controller. Software-complete
and validated entirely in simulation before any hardware purchase. Swapping in
real hardware later requires zero logic changes -- only swapping the sensor
implementation behind one interface (`ThermalSensor`).

See `PROJECT_PLAN.md` for the full spec this repo implements. Every number
below comes from a script's own printed output -- see `docs/results.md` for
exact commands.

## Architecture

```mermaid
flowchart LR
    subgraph Firmware [ESP32 firmware]
        TS[ThermalSensor interface] --> HD[HotspotDetector]
        HD --> MM[MqttManager]
    end
    TS -.->|SENSOR_MODE build flag| Sim[SimulatedSensor]
    TS -.-> MLX[MLX90640Sensor]
    TS -.-> AMG[AMG8833Sensor]

    MM -- "thermal/hotspots" --> Broker[(MQTT broker)]
    Broker -- "thermal/hotspots" --> AC[controller/airflow_controller.py]
    AC -- drift model + Section 7 logic --> AC
    AC -- "control/setpoint" --> Broker
    Broker -- "control/setpoint" --> MM
```

Data flow and component responsibilities are detailed in
`docs/architecture.md`.

## Repo layout

```
simulation/    thermal dynamics, hotspot detection, energy sim, MQTT integration test
ml/            NumPy drift-forecasting MLP + training
controller/    airflow_controller.py: MQTT -> drift model -> setpoint
firmware/      PlatformIO ESP32 project (SENSOR_MODE = SIM | MLX90640 | AMG8833)
docs/          architecture, setup, results, hardware BOM
```

## Running everything in simulation today

```bash
pip install -r requirements.txt

python3 simulation/thermal_dynamics_sim.py      # sanity-check the dynamics model
python3 simulation/hotspot_accuracy_eval.py     # hotspot precision/recall/F1
python3 ml/train_drift_model.py                 # train the drift MLP, prints test MAE
python3 simulation/energy_simulation.py         # static vs predictive energy comparison
python3 simulation/mqtt_integration_test.py     # in-process amqtt broker, end-to-end check

cd firmware && pio run -e esp32dev              # compile-check firmware (SIM build)
```

Full command reference and expected output: `docs/setup.md`.

## Switching to real hardware later

1. Wire up the MLX90640 or AMG8833 breakout (I2C) to the ESP32 -- see
   `docs/hardware-bom.md`.
2. Build with `pio run -e esp32dev_mlx90640` or `pio run -e esp32dev_amg8833`
   instead of the default `esp32dev` (SIM) environment.
3. Fill in real WiFi/MQTT broker settings in `firmware/include/config.h`.

No change to `HotspotDetector`, `MqttManager`, `main.cpp`, or anything on the
Python side -- they only ever talk to the `ThermalSensor` interface.

## Status vs. resume claims

- ~90% hotspot-detection accuracy: measured precision is 0.997-1.000, but
  measured recall (0.54-0.68) and F1 (0.70-0.81) come in below ~90% in this
  eval -- see `docs/results.md` for the real numbers and why.
- ~25% lower simulated energy use: measured 24.00% mean (std 9.60%) across
  20 seeds -- matches.
