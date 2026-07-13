# Thermal Vision-Based Intelligent Airflow Optimization

ESP32 + thermal-camera (MLX90640 or AMG8833) HVAC controller. Software-complete
and validated entirely in simulation before any hardware purchase. Swapping in
real hardware later requires zero logic changes -- only swapping the sensor
implementation behind one interface (`ThermalSensor`).

See `PROJECT_PLAN.md` for the full spec this repo implements. Every number
below comes from a script's own printed output -- see `docs/results.md` for
exact commands.

## Validated Results

Last full re-verification pass, every script/test re-run from scratch:

| Check | Result | Key numbers |
|---|---|---|
| `simulation/thermal_dynamics_sim.py` | PASS | sanity check only, no numeric claim tracked in `docs/results.md` |
| `simulation/hotspot_accuracy_eval.py` | PASS | MLX90640: precision=0.997 recall=0.678 f1=0.807; AMG8833: precision=1.000 recall=0.536 f1=0.698 |
| `ml/train_drift_model.py` | PASS | Test MAE=0.1566 deg C, RMSE=0.2344 deg C |
| `simulation/energy_simulation.py` | PASS | mean=24.00% std=9.60% reduction (20 seeds); comfort MAE static=0.522 deg C, predictive=0.999 deg C |
| `simulation/mqtt_integration_test.py` | PASS | 20/20 setpoint decisions, exit code 0 |
| `pio run -e esp32dev` (firmware) | PASS | RAM 11.1%, Flash 28.9% |

**This does not clear the ~90% accuracy claim from PROJECT_PLAN.md section 0.**
Precision is excellent (99.7-100%: almost no false alarms), but recall is
0.54-0.68, pulling F1 to 0.70-0.81. Root cause: the synthetic generator's low
end (~2C above ambient) produces blobs genuinely below or barely at the
detection threshold (3.5-4.5C) by construction -- those are correctly missed,
not detector bugs. Real occupant hotspots (skin ~33C vs. ~24C ambient, an
~9C delta) sit well clear of threshold, so this eval's recall is likely a
pessimistic floor rather than a realistic field number, but that's a
hypothesis, not something re-measured here. Per PROJECT_PLAN.md's own
instruction, the measured numbers are reported as-is rather than re-tuned to
hit ~90%.

Full detail, exact commands, and methodology: `docs/results.md`.

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
