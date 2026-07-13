# Results

Every number below is copy-pasted from a script's own printed output / the
`*_results.json` it wrote in this repo -- nothing hand-typed.

## Hotspot detection accuracy (section 4)

Command:

```bash
python3 simulation/hotspot_accuracy_eval.py
```

Output:

```
MLX90640: delta_c=4.5 min_pixels=2  precision=0.997  recall=0.678  f1=0.807  (TP=1068 FP=3 FN=507, 1000 frames)
AMG8833: delta_c=3.5 min_pixels=1  precision=1.000  recall=0.536  f1=0.698  (TP=810 FP=0 FN=702, 1000 frames)
```

| Sensor    | delta_c | min_pixels | Precision | Recall | F1    |
|-----------|---------|------------|-----------|--------|-------|
| MLX90640  | 4.5     | 2          | 0.997     | 0.678  | 0.807 |
| AMG8833   | 3.5     | 1          | 1.000     | 0.536  | 0.698 |

Eval methodology: 1000 synthetic frames per sensor
(`simulation/thermal_scene_simulator.py`), 0-3 warm blobs per frame with
peak temperature 2-14C above ambient and Gaussian sigma 0.6-2.5px. A
detection counts as a true positive if its centroid is within 2.5px of an
unmatched ground-truth blob (greedy nearest-neighbor, one-to-one).

**This does not clear the ~90% accuracy claim from PROJECT_PLAN.md section 0.**
Precision is excellent (99.7-100%: almost no false alarms), but recall is
0.54-0.68, pulling F1 to 0.70-0.81. Root cause: the synthetic generator's
low end (~2C above ambient) produces blobs genuinely below or barely at the
detection threshold (3.5-4.5C) by construction -- those are correctly missed,
not detector bugs. Real occupant hotspots (skin ~33C vs. ~24C ambient, an
~9C delta) sit well clear of threshold, so this eval's recall is likely a
pessimistic floor rather than a realistic field number, but that's a
hypothesis, not something re-measured here. Per PROJECT_PLAN.md's own
instruction, the measured numbers are reported as-is rather than re-tuned to
hit ~90%.

## Drift-forecasting model (section 6)

Command:

```bash
python3 ml/train_drift_model.py
```

Output (final lines):

```
TEST-SET MAE: 0.1566 deg C  (RMSE: 0.2344 deg C)
```

From `ml/drift_model_results.json`:

| Metric | Value |
|---|---|
| k (history window) | 10 |
| h (forecast horizon) | 10 |
| Hidden units | 16 |
| Parameters | 369 |
| Train days | 45 |
| Test days | 15 (disjoint seed range, held out) |
| Test MAE | 0.1566 deg C |
| Test RMSE | 0.2344 deg C |

## Energy simulation (section 7)

Command:

```bash
python3 simulation/energy_simulation.py
```

Output (final lines):

```
ENERGY REDUCTION over 20 seeds: mean=24.00%  std=9.60%
Comfort (mean |T_in - setpoint|): static=0.522 C  predictive=0.999 C
```

From `simulation/energy_results.json`:

| Metric | Value |
|---|---|
| Seeds | 20 |
| Mean energy reduction | 24.00% |
| Std energy reduction | 9.60% |
| Comfort MAE, static | 0.522 deg C |
| Comfort MAE, predictive | 0.999 deg C |

**Matches the ~25% claim** (24.00% mean, real measured number, not force-fit).
Tradeoff: the predictive controller's comfort deviation is roughly double the
static thermostat's, because it deliberately relaxes the setpoint when no
occupancy is detected -- saving energy when the room is empty at the cost of
drifting further from the nominal setpoint during those periods.

## Firmware compile check (sections 3-4)

Command:

```bash
cd firmware && pio run -e esp32dev
```

Result: **SUCCESS**. RAM 11.1% (36460/327680 bytes), Flash 28.9%
(378949/1310720 bytes), build time 545.22s (first run, includes toolchain
download).

## MQTT integration test (section 8)

Command:

```bash
python3 simulation/mqtt_integration_test.py
```

Output:

```
amqtt broker listening on 127.0.0.1:18830
Published 20 hotspot frames, received 20 control/setpoint decisions
PASS: controller reacted end-to-end over the amqtt broker
```

Result: **PASS**, exit code 0.
