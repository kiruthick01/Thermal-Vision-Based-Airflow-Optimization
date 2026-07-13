# Thermal Vision-Based Intelligent Airflow Optimization — Project Plan

Single source of truth for this project. Claude Code should read this file once
and treat it as authoritative — do not ask the user to re-explain anything
covered here; ask only about things this file leaves open.

## 0. Goal

Software-complete implementation of an ESP32 + thermal-camera (MLX90640 or
AMG8833) HVAC controller, built and validated entirely in simulation before
any hardware is purchased. Hardware swap-in later must require zero logic
changes — only swapping the sensor implementation behind one interface.

Resume claims this project must support with real evidence, not assertion:
- ~90% hotspot-detection accuracy
- ~25% lower simulated energy use vs. static thermostat control

Every number that ends up in docs/results.md must come from a script's actual
output (echoed in that script's own run), never hand-typed.

## 1. Repo layout

thermal-airflow-optimization/
├── PROJECT_PLAN.md
├── README.md
├── requirements.txt
├── .gitignore
├── docs/
│   ├── architecture.md
│   ├── setup.md
│   ├── results.md               (numbers pulled verbatim from *_results.json)
│   └── hardware-bom.md          (components to buy later + est. cost)
├── simulation/
│   ├── thermal_scene_simulator.py
│   ├── hotspot_detector.py
│   ├── hotspot_accuracy_eval.py
│   ├── thermal_dynamics_sim.py
│   ├── energy_simulation.py
│   └── mqtt_integration_test.py
├── ml/
│   ├── thermal_drift_model.py
│   ├── train_drift_model.py
│   └── export_weights_c.py      (optional stretch)
├── controller/
│   └── airflow_controller.py
├── firmware/
│   ├── platformio.ini
│   ├── include/config.h
│   └── src/
│       ├── main.cpp
│       ├── ThermalSensor.h            (abstract interface)
│       ├── MLX90640Sensor.h/.cpp
│       ├── AMG8833Sensor.h/.cpp
│       ├── SimulatedSensor.h/.cpp
│       ├── HotspotDetector.h/.cpp     (C++ port of simulation/hotspot_detector.py)
│       └── MqttManager.h/.cpp
└── tests/
    ├── test_hotspot_detector.py
    └── test_thermal_dynamics.py

## 2. Environment constraints (learned the hard way — don't rediscover these)

- No root/sudo in a typical dev sandbox → cannot `apt-get install mosquitto`.
  Use `amqtt` (pure-Python MQTT broker, pip-installable, no root) for the
  local integration test instead of a real Mosquitto binary.
- `pip install scikit-learn` can time out in constrained sandboxes. Prefer no
  sklearn dependency. Implement the drift model as a small pure-NumPy MLP (2
  layers, <200 params) trained with plain gradient descent — it's also a
  better fit for "lightweight ML model" since it doubles as something
  portable to TFLite-Micro / raw C arrays later.
- Confirmed working already: `numpy`, `matplotlib`, `paho-mqtt`, `amqtt`.
- PlatformIO is not installed by default; install with
  `pip install --break-system-packages platformio` and compile with
  `pio run -e esp32dev` as a lint/compile check (not a flash — no hardware).
  If PlatformIO can't be installed, do a careful manual code review instead
  and say so explicitly in the summary.

## 3. Sensor abstraction (firmware)

```cpp
class ThermalSensor {
public:
  virtual bool begin() = 0;
  virtual bool readFrame(float* out, int rows, int cols) = 0; // row-major, deg C
  virtual int rows() const = 0;
  virtual int cols() const = 0;
};
```

Three implementations: `MLX90640Sensor` (24x32, Adafruit_MLX90640 lib),
`AMG8833Sensor` (8x8, Adafruit_AMG88xx lib), `SimulatedSensor` (generates the
same kind of synthetic hotspot blobs as `simulation/thermal_scene_simulator.py`,
entirely in C++, no network dependency — this is what runs until hardware
arrives). Select via a `SENSOR_MODE` build flag in `platformio.ini`
(`SIM`, `MLX90640`, `AMG8833`). `main.cpp` and everything downstream only
ever talks to the `ThermalSensor` interface.

## 4. Hotspot detection algorithm

1. `ambient = median(frame)`
2. `hot_mask[i] = frame[i] > ambient + delta_c`
3. 4-connected iterative flood fill (BFS with an explicit queue, not
   recursion) to group hot pixels into blobs
4. Discard blobs smaller than `min_pixels`
5. Report each blob's temperature-weighted centroid + peak temp

**Validated thresholds (1000-frame synthetic eval — behind the "~90%" claim):**

| Sensor    | delta_c | min_pixels | Precision | Recall | F1    |
|-----------|---------|------------|-----------|--------|-------|
| MLX90640  | 4.5     | 2          | 0.997     | 0.803  | 0.889 |
| AMG8833   | 3.5     | 1          | 0.972     | 0.680  | 0.800 |

If re-running the eval gives materially different numbers, trust the new run
and update docs/results.md — don't silently keep the old numbers.

## 5. Thermal dynamics model (feeds both ML training and energy sim)

dT/dt = (T_outside - T_inside) / tau_env      # heat exchange with outside
+ occupant_heat_gain(t)                  # from occupancy schedule / hotspot count
- hvac_cooling_power * hvac_on(t)         # compressor effect
+ noise 
Parameters: `tau_env` ~ 2-4 hours (room thermal mass), outside temp as a
diurnal sine wave (e.g. 22-34°C over 24h), occupant heat gain ~100W/person
mapped to a °C/s contribution, HVAC cooling power sized to overcome peak
outside heat load within a reasonable duty cycle. Put this in
`simulation/thermal_dynamics_sim.py` and reuse from both
`ml/train_drift_model.py` and `simulation/energy_simulation.py` — don't
duplicate it.

## 6. ML drift-forecasting model

- Input: last `k` (e.g. 10) temperature readings + HVAC on/off history +
  recent hotspot/occupancy count.
- Output: predicted temperature `h` steps ahead (drift forecast).
- Model: NumPy MLP, one hidden layer (e.g. 16 units), manual forward pass +
  backprop, trained on data from the Section 5 dynamics model across many
  randomized days/seeds. Report test-set MAE (°C) — honest metric for a
  regression task, don't claim an accuracy %.
- Save weights as JSON (optionally export a C header for future on-device
  inference — stretch goal).

## 7. Energy simulation (produces the ~25% number)

Two controllers over the *same* dynamics model, occupancy schedule, and
outside-temperature profile, across ≥10 random seeds (report mean ± std, not
one cherry-picked run):

- **Static thermostat**: bang-bang hysteresis (`hvac_on` toggles at
  setpoint ± 0.5°C).
- **Predictive/airflow-optimized**: uses the drift model's forecast to
  pre-empt overshoot (modulate cooling proportionally to predicted drift
  instead of binary on/off) and only ramps airflow when hotspot detection
  indicates actual occupancy.

`% reduction = (static_energy - predictive_energy) / static_energy * 100`.
If the measured number isn't ~25%, report the real number and note it —
don't force-fit it.

## 8. MQTT topic design

site/<zone>/thermal/frame       # optional: downsampled frame or stats
site/<zone>/thermal/hotspots    # JSON: [{row, col, peak_temp_c}]
site/<zone>/control/setpoint    # controller -> actuator
site/<zone>/status              # heartbeat / LWT
`controller/airflow_controller.py` subscribes to `.../thermal/hotspots`, runs
the drift model + Section 7 logic, publishes to `.../control/setpoint`.
Integration test: spin up `amqtt` broker in-process, simulated publisher
sends frames, confirm the controller reacts end-to-end.

## 9. Documentation deliverables

- `README.md`: what this is, architecture diagram (mermaid ok), how to run
  everything in simulation mode today, how to switch to hardware later.
- `docs/architecture.md`: data flow diagram + component responsibilities.
- `docs/setup.md`: exact commands to run each script/test, PlatformIO build,
  local MQTT broker setup.
- `docs/results.md`: hotspot accuracy table (Section 4) + energy results
  (Section 7), each with the exact command used to generate them.
- `docs/hardware-bom.md`: MLX90640/AMG8833 breakout, ESP32 board,
  relay/damper actuator, PSU, rough costs.

## 10. Git / GitHub

`git init`, sensible `.gitignore` (venv, __pycache__, .pio — but DO track
`*_results.json` since docs/results.md cites them), commit per stage. Claude
Code cannot authenticate as the user — after committing, print the exact
`gh repo create` / `git remote add` / `git push` commands and stop.

## 11. Definition of "done"

A stage isn't done until its script/test actually runs and prints real
output. "Should work" is not done. If PlatformIO compilation isn't possible,
say so explicitly rather than silently skipping verification.