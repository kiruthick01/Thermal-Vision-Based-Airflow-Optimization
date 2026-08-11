# Physical build guide

Hardware assembly for the AMG8833 build. Follow the order below — power
comes last, deliberately.

## 0. Before you touch anything

Three things to verify first. Each one wastes an hour if you get it wrong.

| Check | How | If wrong |
|---|---|---|
| TFT driver chip | Look at the controller IC on the back of the module, or the seller listing. Must be **ILI9341**. | If it says ST7789/ILI9486, tell me — `HeatmapDisplay.cpp` needs a different Adafruit library. |
| TFT supply voltage | Look near the VCC pin. **3-pin regulator present (AMS1117)** → feed 5V. **No regulator** → feed 3.3V. | Wrong choice = white screen or dead backlight. |
| ESP32 antenna | The `-32U` variant has a **u.FL socket, no PCB antenna**. Check the box for a small antenna. | No antenna → WiFi/MQTT won't work. Firmware now runs fine offline, but the full MQTT demo needs one (~₹60, Ritchie Street). |

Also confirm your USB cable does **data**, not just charging — plug the bare
ESP32 in and check a serial port appears.

## 1. Solder headers

The AMG8833 ships with a loose 6-pin header strip. Solder it before anything
else — you can't breadboard without it. (Check the TFT too; those often come
pre-soldered.)

**Use the breadboard as a jig.** This is the trick that makes a beginner's
first solder job come out straight:

1. Push the header's **long** pins down into the breadboard. The black
   plastic spacer rests on the breadboard surface, short pins pointing up.
2. Drop the PCB over the short pins, **label side facing up**. The board now
   sits flat and perfectly square with no hands needed.
3. Solder the six short pins to the pads on the top face.

Technique: tin the tip, touch the iron to the pad *and* pin together for
about two seconds, then feed solder into the **joint** — not onto the iron.
Remove solder first, then the iron.

A good joint is a small shiny cone that wets both pad and pin. A dull grey
blob means it moved while cooling — reheat it. A ball sitting on top of the
pad without spreading means the pad wasn't hot enough.

Don't linger more than ~4 seconds per joint.

## 2. Seat the ESP32

The 38-pin board is wide. Straddle it across the breadboard's centre channel
and confirm **at least one free hole column remains on each side**. If it
leaves zero, you need a second breadboard side-by-side, or run female-to-male
jumpers directly onto the pins instead.

**Never use GPIO 6, 7, 8, 9, 10, 11** (labelled SD0/SD1/SD2/SD3/CMD/CLK).
They're wired to the internal flash chip and using them will crash the boot.

## 3. Power rails

Set the rails up before signal wiring — it makes the rest obvious.

```
ESP32 3V3  ->  breadboard red rail   (sensor + TFT logic)
ESP32 GND  ->  breadboard blue rail
```

**Servos do not run off the ESP32.** Two SG90s can pull ~700 mA stalled;
laptop USB gives 500 mA total, and the brownout will reset your board
mid-demo. Use a separate 5V source:

- Cut a spare USB cable, strip it: **red = 5V, black = GND** (ignore green/white).
- Red → a second breadboard rail. Black → the **same blue rail as the ESP32**.

> The grounds *must* be tied together. Servo signal is referenced to the
> ESP32's ground — separate grounds means the servo sees garbage and jitters.

Verify with the multimeter before proceeding: black probe on blue rail, red
probe on each supply rail. Expect ~3.3V and ~5.0V. Then check continuity
(beep mode) between ESP32 GND and the servo supply's ground.

## 4. Wiring

### AMG8833 thermal sensor (I2C)

The breakout has **six** pins. Physical order on the silkscreen, left to
right, is:

```
VIN   GND   SCL   SDA   INT   AD0
```

**SCL comes before SDA.** Counting pins left-to-right and assuming the usual
"SDA then SCL" ordering is the most common way to get this wrong, and the
symptom is `Sensor init failed` with no other clue.

| Sensor pin | ESP32 pin | |
|---|---|---|
| VIN | 3V3 | not 5V |
| GND | GND rail | |
| SCL | **GPIO22** | 3rd pin |
| SDA | **GPIO21** | 4th pin |
| INT | *leave unconnected* | interrupt output, unused |
| AD0 | *leave unconnected* | I2C address select |

GPIO21/22 are the ESP32's default `Wire` pins, which is what
`Adafruit_AMG88xx` uses — no pin config needed in firmware.

`AD0` selects the I2C address. Left floating it gives 0x69, which is the
library default that `AMG8833Sensor::begin()` relies on. If the sensor
refuses to initialise and the wiring checks out, the other candidate address
is 0x68.

### ILI9341 TFT (SPI)

| TFT pin | ESP32 pin | Notes |
|---|---|---|
| VCC | 3V3 **or** 5V | Per the regulator check in step 0 |
| GND | GND | |
| CS | GPIO15 | `kTftCsPin` |
| RESET | GPIO26 | `kTftRstPin` |
| DC / RS | GPIO27 | `kTftDcPin` |
| SDI / MOSI | GPIO23 | VSPI MOSI, fixed |
| SCK | GPIO18 | VSPI SCK, fixed |
| LED | 3V3 | Backlight, always on |
| SDO / MISO | GPIO19 | Optional — display never reads back |

Ignore the SD card pins on the module if it has them.

### Servos

| Servo | Signal (orange) | V+ (red) | GND (brown) |
|---|---|---|---|
| Pan (horizontal) | **GPIO13** | 5V rail | Common GND |
| Tilt (vertical) | **GPIO25** | 5V rail | Common GND |

SG90 wire colours: orange = signal, red = +5V, **brown = ground**.

### Indicator LEDs

| LED | ESP32 pin | Wiring |
|---|---|---|
| Green "heartbeat" | GPIO2 | GPIO → 220Ω → LED anode → LED cathode → GND |
| Red "hotspot" | GPIO4 | Same |

Anode = long leg. Cathode = short leg, flat spot on the rim. Backwards = no
light (harmless, just reseat it).

## 5. Pre-power checklist

Run through this before plugging anything in.

- [ ] No bare wire touching another bare wire
- [ ] 3V3 rail and 5V rail are **not** bridged anywhere
- [ ] Multimeter continuity check: no beep between any supply rail and GND
- [ ] Both servo grounds tied to ESP32 ground
- [ ] AMG8833 on 3V3 (it is **not** 5V tolerant on the logic pins)
- [ ] Nothing wired to GPIO 6–11
- [ ] ESP32 not yet connected to USB

## 6. Bring-up, one stage at a time

Do not wire everything and hope. Add one subsystem, flash, confirm, move on.

Use the **`esp32dev_amg8833_demo`** environment for all of this. It's the
real sensor build with the radio switched off — instant boot, no WiFi, no
broker, nothing to fail on demo day:

```bash
cd firmware
pio run -e esp32dev_amg8833_demo -t upload
pio device monitor -b 115200
```

1. **Bare board** — flash `-e esp32dev -t upload` (the SIM build), open the
   serial monitor at 115200. Frames should process. Nothing attached yet.
   Do this step *first and early* — the initial run downloads the ESP32
   toolchain (~9 min).
2. **LEDs only** — green toggles every 2 s, red follows hotspot count.
3. **Sensor** — switch to `-e esp32dev_amg8833_demo`. Serial should report
   plausible hotspot counts; wave your hand in front of the sensor.
   If `Sensor init failed`, it's I2C: check SDA/SCL aren't swapped.
4. **Pan servo** — expect movement as the hotspot shifts left/right. Random
   twitching = grounds not common, or supply sagging.
5. **TFT** — heatmap should appear. White screen = wrong VCC voltage or
   wrong driver chip. Garbage pixels = loose SCK/MOSI.
6. **Tilt servo** (optional) — only once 1-5 are solid. Needs the pan/tilt
   bracket mechanically centred at 90°.

Networking is deliberately *not* part of demo bring-up. If you later want
the full MQTT path, fill in `kWifiSsid` / `kWifiPassword` / `kMqttBroker` in
`firmware/include/config.h` and build `-e esp32dev_amg8833` instead — that
environment keeps the radio on and reconnects in the background. On an
ESP32-WROOM-**32U** it also needs a u.FL antenna physically attached.

## 7. Mounting

The AMG8833 goes on the pan/tilt bracket, pointing where the vent points, so
the sensor and the airflow track together. Servo horns should be attached
with both servos at 90° (their power-on position) so the mechanical centre
matches the firmware's centre.

Cardboard or foamboard is fine for the vent flap. Hot-glue it to the tilt
servo's horn.

## Common failure modes

| Symptom | Cause |
|---|---|
| Board reboots when servo moves | Servos drawing from ESP32's 5V. Separate supply. |
| Serial prints boot loop / brownout | Same. |
| White TFT screen | Wrong VCC voltage, or driver isn't ILI9341 |
| TFT shows noise | Loose SCK or MOSI |
| `Sensor init failed` | SDA/SCL swapped, or sensor on 5V not 3V3 |
| Servo jitters constantly | Grounds not tied together |
| Nothing on serial | Charge-only USB cable |
| Board hangs at boot | Something on GPIO 6–11 |
