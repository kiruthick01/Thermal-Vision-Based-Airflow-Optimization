# Hardware BOM (to buy later)

Not purchased yet -- software-complete-in-simulation first, per
PROJECT_PLAN.md section 0. Prices are rough street estimates, not quotes.

Two tiers below: a minimum-budget build (what's actually being purchased)
and the higher-resolution alternative, for reference.

| Component | Example part | Est. cost (USD) | Notes |
|---|---|---|---|
| Thermal camera | **AMG8833 breakout (8x8)** -- budget pick | $35-40 | Cheaper, simpler I2C wiring. This repo's own eval (`docs/results.md`) shows it's actually higher-precision than MLX90640 (1.000 vs 0.997), just lower recall (0.536 vs 0.678) -- fewer false alarms, misses more faint blobs. Good enough for the demo/budget build. |
| Thermal camera (alt., higher-res) | Adafruit MLX90640 breakout (24x32, 55 deg FOV) | $60-70 | Better recall (0.678). Swap in later for the same $ delta if budget allows. |
| MCU board | ESP32 DevKitC (generic clone is fine) | $8-12 | Matches `firmware/platformio.ini` `board = esp32dev`. |
| **Airflow-direction actuator** | **SG90 micro servo** (plain, not metal-gear) on a louvre/vent flap | $3-5 | Drives `firmware/include/config.h` `kServoPin` (GPIO18). Firmware maps the strongest detected hotspot's column straight to a servo angle every frame (`main.cpp::angleForHotspots`) -- implemented and wired, no relay needed for this. |
| Louvre/flap hardware | DIY: cardboard/foamboard flap + hot-glued servo horn | $0-2 | Skip 3D printing/bought parts for the prototype. |
| Power supply | Reuse an existing 5V phone charger + cable | $0 | Only buy a new 5V/2A USB adapter ($6-10) if neither of you has a spare -- servo stall current can spike, so don't run it off the ESP32's onboard 5V rail alone under load. |
| Wiring/misc | Dupont jumpers, small breadboard | $5-8 | Enclosure/perfboard deferred until the design is final. |

**Rough total: ~$51-67** for the budget build above (AMG8833 + SG90, reusing
a charger). Swapping in MLX90640 and/or a bought enclosure pushes this back
toward the original ~$95-135 estimate.

Optional, not required for the airflow-direction feature: a 5V single-channel
relay module (~$3-6) if you also want to hard-switch the HVAC unit's power
separately from redirecting airflow.

## Wiring notes (for when hardware arrives)

- Both MLX90640 and AMG8833 are I2C (SDA/SCL + 3.3V/GND to the ESP32).
- No hardware changes needed in `HotspotDetector`, `MqttManager`, or
  `main.cpp`'s sensing path -- only build with `pio run -e esp32dev_mlx90640`
  or `pio run -e esp32dev_amg8833` instead of the default SIM environment,
  and fill in real WiFi/MQTT settings in `firmware/include/config.h`.
- Servo: signal to GPIO18 (`kServoPin`), V+ to 5V, GND to common ground.
  `ESP32Servo` (added to `platformio.ini` `lib_deps` for every environment)
  drives it; no code changes needed once the servo is physically wired --
  `main.cpp` already calls `airflowServo.write()` every frame.
- Temperature-level actuation (cycling/modulating the HVAC unit itself, as
  opposed to airflow direction) is still hardware-specific and intentionally
  left unimplemented in `onSetpoint()` until a relay/compressor interface is
  chosen.
