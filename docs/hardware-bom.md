# Hardware BOM (to buy later)

Not purchased yet -- software-complete-in-simulation first, per
PROJECT_PLAN.md section 0. Prices are rough street estimates, not quotes.

| Component | Example part | Est. cost (USD) | Notes |
|---|---|---|---|
| Thermal camera | Adafruit MLX90640 breakout (24x32, 55 deg FOV) | $60-70 | Higher resolution, better recall in this repo's eval (0.678 vs 0.536). Preferred default. |
| Thermal camera (alt., cheaper/lower-res) | Adafruit AMG8833 breakout (8x8) | $35-40 | Lower resolution, higher precision but lower recall here. Cheaper, simpler wiring. |
| MCU board | ESP32 DevKitC (or any esp32dev-compatible board) | $8-12 | Matches `firmware/platformio.ini` `board = esp32dev`. |
| Actuator | 5V/relay module (single-channel) or servo-driven damper | $3-15 | Drives HVAC on/off or damper position from `control/setpoint`. Exact interface is hardware-specific, not modeled in firmware yet. |
| Power supply | 5V/2A USB wall adapter | $6-10 | Sensor + ESP32 + relay coil draw. |
| Wiring/misc | Dupont jumpers, breadboard or perfboard, enclosure | $10-15 | |

**Rough total: ~$120-165** for one zone (MLX90640 variant) or **~$95-135**
(AMG8833 variant).

## Wiring notes (for when hardware arrives)

- Both MLX90640 and AMG8833 are I2C (SDA/SCL + 3.3V/GND to the ESP32).
- No hardware changes needed in `HotspotDetector`, `MqttManager`, or
  `main.cpp` -- only build with `pio run -e esp32dev_mlx90640` or
  `pio run -e esp32dev_amg8833` instead of the default SIM environment, and
  fill in real WiFi/MQTT settings in `firmware/include/config.h`.
- Relay/damper actuation wiring and the `onSetpoint()` -> GPIO mapping in
  `main.cpp` is intentionally left unimplemented until the actual actuator
  hardware is chosen.
