# Hardware BOM

Purchased and built for the AMG8833 budget tier -- sensing, hotspot
detection, pan+tilt servo tracking, and the TFT heatmap all confirmed
working on real hardware (`docs/build-guide.md`). Prices below are rough
street estimates from before the purchase, not final receipts.

Two tiers below: a minimum-budget build (what's actually being purchased)
and the higher-resolution alternative, for reference.

| Component | Example part | Est. cost (USD) | Notes |
|---|---|---|---|
| Thermal camera | **AMG8833 breakout (8x8)** -- budget pick | $35-40 | Cheaper, simpler I2C wiring. This repo's own eval (`docs/results.md`) shows it's actually higher-precision than MLX90640 (1.000 vs 0.997), just lower recall (0.536 vs 0.678) -- fewer false alarms, misses more faint blobs. Good enough for the demo/budget build. |
| Thermal camera (alt., higher-res) | Adafruit MLX90640 breakout (24x32, 55 deg FOV) | $60-70 | Better recall (0.678). Swap in later for the same $ delta if budget allows. |
| MCU board | ESP32 DevKitC (generic clone is fine) | $8-12 | Matches `firmware/platformio.ini` `board = esp32dev`. |
| **Airflow-direction actuator** | **SG90 micro servo** (plain, not metal-gear) on a louvre/vent flap | $3-5 | Drives `firmware/include/config.h` `kServoPin` (GPIO13 -- moved off GPIO18 to free the TFT's SPI clock line). Firmware maps the strongest detected hotspot's column straight to a servo angle every frame (`main.cpp::angleForHotspots`) -- implemented and wired, no relay needed for this. |
| Tilt servo (2nd axis) | Second SG90 on a 2-axis pan/tilt bracket | $3-5 | Drives `kServoTiltPin` (GPIO25). Maps the strongest hotspot's *row* to a vertical vane angle over a deliberately narrow 60-120 deg sweep (`main.cpp::tiltAngleForHotspots`). |
| Pan/tilt bracket | 2-axis bracket for SG90/MG90 | $1-2 | Carries the thermal sensor so it aims where the airflow aims. |
| Louvre/flap hardware | DIY: cardboard/foamboard flap + hot-glued servo horn | $0-2 | Skip 3D printing/bought parts for the prototype. |
| **Live heatmap display** | **2.4" ILI9341 SPI TFT (320x240)** breakout | $6-9 | Drives `HeatmapDisplay` (`firmware/src/HeatmapDisplay.{h,cpp}`) -- renders the current frame as a false-color heatmap with hotspot markers overlaid, every `kFramePeriodMs`. Wired to the ESP32's hardware VSPI bus (`config.h` `kTftSckPin`/`kTftMisoPin`/`kTftMosiPin` = GPIO18/19/23) plus `kTftCsPin`/`kTftDcPin`/`kTftRstPin` = GPIO15/27/26. Optional -- purely a visualization, doesn't feed back into detection or control. |
| Power supply | Reuse an existing 5V phone charger + cable | $0 | Only buy a new 5V/2A USB adapter ($6-10) if neither of you has a spare -- servo stall current can spike, so don't run it off the ESP32's onboard 5V rail alone under load. |
| Wiring/misc | Dupont jumpers, small breadboard | $5-8 | Enclosure/perfboard deferred until the design is final. |

**Rough total: ~$57-76** for the budget build above (AMG8833 + SG90 + ILI9341,
reusing a charger). Swapping in MLX90640 and/or a bought enclosure pushes this
back toward the original ~$95-135 estimate.

Optional, not required for the airflow-direction feature: a 5V single-channel
relay module (~$3-6) if you also want to hard-switch the HVAC unit's power
separately from redirecting airflow.

## Wiring notes (for when hardware arrives)

- Both MLX90640 and AMG8833 are I2C (SDA/SCL + 3.3V/GND to the ESP32).
- No hardware changes needed in `HotspotDetector`, `MqttManager`, or
  `main.cpp`'s sensing path -- only build with `pio run -e esp32dev_mlx90640`
  or `pio run -e esp32dev_amg8833` instead of the default SIM environment,
  and fill in real WiFi/MQTT settings in `firmware/include/config.h`.
Step-by-step assembly, bring-up order and failure modes: **`docs/build-guide.md`**.

- Pan servo: signal to GPIO13 (`kServoPin`), V+ to 5V, GND to common ground.
  Tilt servo: signal to GPIO25 (`kServoTiltPin`), same supply/ground. Both
  must run off a **separate 5V supply**, not the ESP32 — two SG90s stalling
  exceed what USB can deliver and will brown the board out.
- AMG8833 is I2C on the ESP32's default `Wire` pins: SDA GPIO21, SCL GPIO22,
  VIN 3V3 (not 5V).
- On an ESP32-WROOM-**32U** there is no PCB antenna, only a u.FL socket --
  WiFi/MQTT needs an external 2.4 GHz antenna plugged in. The firmware
  degrades to offline operation after `kWifiConnectTimeoutMs` if absent.
- 38-pin dev boards expose GPIO 6-11 (SD0/SD1/SD2/SD3/CMD/CLK). These are
  wired to the internal SPI flash -- do not use them for anything.
  `ESP32Servo` (added to `platformio.ini` `lib_deps` for every environment)
  drives it; no code changes needed once the servo is physically wired --
  `main.cpp` already calls `airflowServo.write()` every frame.
- ILI9341 TFT: SCK/MISO/MOSI to GPIO18/19/23 (ESP32's fixed hardware VSPI
  pins -- this is why the servo moved off GPIO18), CS/DC/RST to
  GPIO15/27/26, LED (backlight) and VCC to 3.3V, GND to common ground. No
  code changes needed once wired -- `HeatmapDisplay::begin()`/`render()`
  are already called from `main.cpp`'s `setup()`/`loop()`.
- Temperature-level actuation (cycling/modulating the HVAC unit itself, as
  opposed to airflow direction) is still hardware-specific and intentionally
  left unimplemented in `onSetpoint()` until a relay/compressor interface is
  chosen.
