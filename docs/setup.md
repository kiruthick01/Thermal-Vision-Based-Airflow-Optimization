# Setup

## Python environment

```bash
pip install -r requirements.txt
```

`requirements.txt`: `numpy`, `paho-mqtt`, `amqtt`. No `scikit-learn`
(the drift model is pure NumPy) and no system MQTT broker install needed
(`amqtt` is a pure-Python broker, pip-installable, no root).

## Thermal dynamics sanity check

```bash
python3 simulation/thermal_dynamics_sim.py
```

Prints one line: steps simulated, `T_inside` range, HVAC duty cycle.

## Hotspot detection accuracy eval

```bash
python3 simulation/hotspot_accuracy_eval.py
```

Runs 1000 synthetic frames per sensor (MLX90640, AMG8833) at the validated
`delta_c`/`min_pixels` thresholds, prints precision/recall/F1 per sensor, and
writes `simulation/hotspot_accuracy_results.json`. Numbers cited in
`docs/results.md` come straight from this run.

## Train the drift-forecasting model

```bash
python3 ml/train_drift_model.py
```

Generates training data from 45 randomized simulated days (disjoint 15-day
test set), trains the NumPy MLP, prints test-set MAE/RMSE, and writes
`ml/drift_model_weights.json` + `ml/drift_model_results.json`.

## Energy simulation

```bash
python3 simulation/energy_simulation.py
```

Requires `ml/drift_model_weights.json` to already exist (run the training
step first). Runs the static thermostat and predictive controller over 20
random seeds, prints per-seed and mean/std % energy reduction plus a comfort
comparison, and writes `simulation/energy_results.json`.

## MQTT integration test

```bash
python3 simulation/mqtt_integration_test.py
```

Starts an in-process `amqtt` broker on `127.0.0.1:18830`, runs a simulated
frame/hotspot publisher against `controller/airflow_controller.py`, and
asserts the controller published one `control/setpoint` decision per
hotspots message, that unoccupied cycles relax the setpoint above occupied
ones, and that at least one drift-model forecast fired. Prints `PASS` and
exits 0 on success. No mosquitto/root required.

## Firmware (PlatformIO)

Install PlatformIO if not already present:

```bash
pip install --break-system-packages platformio
```

Compile-check (no flashing, no hardware needed) the default SIM build:

```bash
cd firmware
pio run -e esp32dev
```

First run downloads the `espressif32` platform/toolchain (~9 minutes,
needs network). Subsequent runs are fast.

Other environments, once real hardware is wired up (`docs/hardware-bom.md`):

```bash
pio run -e esp32dev_mlx90640
pio run -e esp32dev_amg8833
```

These pull in the `Adafruit MLX90640` / `Adafruit AMG88xx Library` PlatformIO
packages, which the default SIM build does not need.

## Wokwi simulation (no hardware needed)

`firmware/diagram.json` + `firmware/wokwi.toml` wire the default SIM build
(`esp32dev` env, `SENSOR_MODE_SIM`) to an ESP32 DevKit V1 with:
`D2` (green LED, "heartbeat") toggling once per `kFramePeriodMs` frame cycle,
`D4` (red LED, "hotspot") lighting while `SimulatedSensor`'s current frame has
at least one detected hotspot, and a `wokwi-servo` on `D18` that physically
rotates to track the strongest detected hotspot's position (`kServoPin`,
`angleForHotspots()` in `main.cpp`) -- the same directional-airflow logic
that drives the real SG90 servo once hardware arrives. No thermal camera
part exists in Wokwi's library, so this demonstrates the on-device sensing ->
`HotspotDetector` -> indicator/servo pipeline exactly as it runs in
`SENSOR_MODE_SIM` (no WiFi/MQTT broker required either -- `connectWifi()` is
a no-op in SIM builds, and `MqttManager` degrades to silent no-op publishes
when disconnected).

Option A -- Wokwi VS Code extension:

1. Build first: `cd firmware && pio run -e esp32dev`
2. Open the `firmware/` folder in VS Code with the "Wokwi Simulator"
   extension installed, then run "Wokwi: Start Simulator" (uses
   `diagram.json` + `wokwi.toml` automatically).

Option B -- `wokwi-cli` (no VS Code):

```bash
cd firmware
pio run -e esp32dev
wokwi-cli . --timeout 15000
```

Option C -- wokwi.com web IDE: create a new ESP32 project, paste the
contents of `firmware/diagram.json` into its Diagram editor, and paste
`firmware/src/*.cpp`/`.h` + `firmware/include/config.h` into matching files
(the web IDE compiles Arduino sketches directly, so PlatformIO's
multi-file layout needs flattening into one sketch there).

## Local MQTT broker for manual testing

To run the controller against a real broker instead of the integration
test's in-process one:

```bash
python3 -c "
import asyncio
from amqtt.broker import Broker
from amqtt.contexts import BrokerConfig, ListenerConfig

async def main():
    broker = Broker(BrokerConfig(listeners={'default': ListenerConfig(bind='127.0.0.1:1883')}))
    await broker.start()
    await asyncio.Event().wait()

asyncio.run(main())
"
```

Then, in another shell:

```bash
python3 controller/airflow_controller.py --broker 127.0.0.1 --port 1883 --zone zone1
```
