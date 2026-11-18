# CrowPanel 2.1" Rotary — hardware analysis

**Document date:** 2026-11-18  
**Intended repo path:** `docs/hardware/crowpanel-2.1.md` in [marwachs/ESP32-DualCAN](https://github.com/marwachs/ESP32-DualCAN) (fork of [opencartools/ESP32-DualCAN](https://github.com/opencartools/ESP32-DualCAN)).  
**Subject:** Elecrow CrowPanel 2.1-inch HMI ESP32 Rotary Display (SKU **DHE03921D**, hardware V1.0).  
**Use case (planned, not implemented in this document):** in-car HMI in a 2026 Tesla Model Y Juniper — UI on the 480×480 knob, BLE toward an S3XY Commander Gen2 and/or later Dorky Commander, plus optional BTHome button proxy. Power from vehicle 12 V via a 12→5 V automotive buck; no LiPo.

## How to read this document

Facts in **Verified** sections are taken from primary sources (Elecrow product page, wiki, official GitHub, schematic PDF text extract, vendor datasheets in that repo). Each fact has a source link.

Items in **Unverified / planned** sections are hypotheses, community reports, framebuffer arithmetic, or integration plans that have **not** been measured or implemented on this board in this project. Do not treat them as proven.

## Primary sources

| Source | URL |
|---|---|
| Elecrow product page | https://www.elecrow.com/crowpanel-2-1inch-hmi-esp32-rotary-display-480-480-ips-round-touch-knob-screen.html |
| Elecrow wiki | https://www.elecrow.com/wiki/CrowPanel_2.1inch-HMI_ESP32_Rotary_Display_480_IPS_Round_Touch_Knob_Screen.html |
| Elecrow ESPHome lesson | https://www.elecrow.com/wiki/2.1-ESPHOME-Lesson02-Light-Up-the-Screen.html |
| Official GitHub (examples, schematic, datasheets, 3D) | https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen |
| Schematic PDF (Main V1.0, dated 2025-08-14 in title block) | https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen/blob/master/Eagle_SCH%26PCB/ESP32_Display_2.1(K)_Main_V1.0.pdf |
| Panel datasheet (Polcd P021B002-IPS) | https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen/blob/master/Datasheet/2.1Inches%20Circular_P021B002-IPS(3.3V).pdf |
| Touch app note (CST826 / CST836U) | https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen/blob/master/Datasheet/CST826%20CST836U_V1.02.pdf |
| Encoder datasheet (EC28 series) | https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen/blob/master/Datasheet/EC28A%20C15H15P3-.pdf |
| Official pin/example header | https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen/blob/master/example/Arduino/Examples/Lesson02-Rotate/Board21.h |
| Official issues | https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen/issues |
| AliExpress listing (existence checked 2026-11-18) | https://www.aliexpress.com/item/1005009858428977.html |
| S3XY virtual button (reference firmware, not board-specific) | https://github.com/Beat-YT/s3xy-virtual-button |
| Dorky Commander upstream | https://github.com/opencartools/ESP32-DualCAN |

---

## 1. Verified — identity and headline specs

| Item | Value | Source |
|---|---|---|
| SKU | DHE03921D | [Wiki](https://www.elecrow.com/wiki/CrowPanel_2.1inch-HMI_ESP32_Rotary_Display_480_IPS_Round_Touch_Knob_Screen.html) (“Model DHE03921D”); same in product-page comparison table |
| Hardware / software revision in official repo | V1.0 / V1.0, marked “latest” | [readme.md](https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen/blob/master/readme.md) |
| MCU | **ESP32-S3R8**, dual-core Xtensa LX7, up to 240 MHz | Product page, wiki, GitHub README |
| Internal SRAM / ROM (as stated by Elecrow) | 512 KB SRAM, 384 KB ROM | Product-page comparison table |
| PSRAM | 8 MB | Product page, wiki, GitHub README. The “R8” suffix on ESP32-S3R8 is Espressif’s designation for 8 MB in-package **octal** PSRAM. Official ESPHome YAML sets `board_build.esp-idf.memory_type: qio_opi` and `psram: mode: octal` ([ESPHome lesson](https://www.elecrow.com/wiki/2.1-ESPHOME-Lesson02-Light-Up-the-Screen.html)). |
| Flash | 16 MB (Elecrow marketing / README) | Product page, wiki, GitHub README. See also the schematic note below: the flash symbol is named `W25X20CL`, which does **not** obviously match a 16 MB part — stuffing vs library name was not visually confirmed from a photo. |
| Radio | 2.4 GHz Wi-Fi (802.11 a/b/g/n as written by Elecrow) and **Bluetooth Low Energy / Bluetooth 5.0** | Product page, wiki, GitHub README. ESP32-S3 has **BLE only**, not classic BR/EDR (Espressif ESP32-S3 family; not restated as a board measurement). |
| Display | 2.1" round **IPS**, 480×480, capacitive touch | Product page, wiki |
| Brightness (Elecrow) | “up to 400” (units implied cd/m²) in the comparison table | Product page comparison table. **Conflicts** with the panel datasheet typical 300 cd/m² (see below). |
| Power input | **5 V / 1 A**; module DC 5 V, main chip 3.3 V | Product page, wiki, GitHub README |
| Operating / storage temperature (Elecrow, whole module) | **−20 to 65 °C** / **−40 to 80 °C** | Product page, wiki, GitHub README |
| Size / net weight | **79 × 79 × 30 mm**, **80 g** | Product page, wiki, GitHub README. The product page also lists a package “Weight 150g” in the shop header; treat 80 g as net, 150 g as likely packed weight. |
| Case material | **Conflict in official text.** Product-page “Other / Shell” field: “Plastic + acrylic”. Wiki, GitHub README, and the comparison table: “Aluminum alloy + plastic + acrylic”. | Do not assume metal until a unit is inspected. The 1.28" sibling is explicitly “Aluminum alloy + Plastic + Acrylic” in the same comparison table. |
| Package contents | 1× display, 1× 50 cm MX1.25-to-USB cable, 1× 4-pin Dupont cable | Product page |
| Official price (Elecrow store, 2026-11-18 fetch) | **USD 35.70**, listed in stock | Product page |
| AliExpress live EUR price | **Unknown** (page loads; price is JS-only and was not read) | Listing above |
| 3D CAD | `3D file/00-irad-21.stp` in the official repo | GitHub tree |

No official Elecrow specification lists a **buzzer** or **vibration motor**. None appears in `Board21.h` or in keyword search of the schematic PDF text. Treat haptic feedback as **absent** unless a later board revision adds it.

---

## 2. Verified — display, touch, encoder

### 2.1 Panel and display bus

The module uses a Polcd **P021B002-IPS** 2.1" circular panel ([datasheet](https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen/blob/master/Datasheet/2.1Inches%20Circular_P021B002-IPS(3.3V).pdf)):

| Item | Datasheet value |
|---|---|
| Pixels | 480 × RGB × 480, normally black IPS |
| Interface | **3-line SPI + 18-bit RGB** (`3Line_SPI_18Bit_RGB`) |
| Drive IC | **ST7701S** |
| Typical luminance | **300 cd/m²** (typ.) |
| Panel operating / storage temperature | **−20 to 70 °C** / **−30 to 80 °C** |
| VCC (analog) | 2.4–3.3 V, typ. 2.8 V |
| IOVCC (digital) | 1.65–3.3 V, typ. 1.8 V |
| LED backlight current | typ. 15 mA, max 20 mA |
| LCD outline (OD) | 55.88 ± 0.1 mm (from the mechanical drawing text) |

This is **not** a QSPI panel. Configuration is 3-wire SPI; pixel data is a parallel **RGB** bus (HSYNC, VSYNC, DE, PCLK, 5+6+5 data pins in the ESP32 wiring). Official wiki sample:

```c
Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
  16 /* CS */, 2 /* SCK */, 1 /* SDA */,
  40 /* DE */, 7 /* VSYNC */, 15 /* HSYNC */, 41 /* PCLK */,
  46 /* R0 */, 3 /* R1 */, 8 /* R2 */, 18 /* R3 */, 17 /* R4 */,
  14 /* G0 */, 13 /* G1 */, 12 /* G2 */, 11 /* G3 */, 10 /* G4 */, 9 /* G5 */,
  5 /* B0 */, 45 /* B1 */, 48 /* B2 */, 47 /* B3 */, 21 /* B4 */
);
```

Source: [wiki](https://www.elecrow.com/wiki/CrowPanel_2.1inch-HMI_ESP32_Rotary_Display_480_IPS_Round_Touch_Knob_Screen.html). The same pin set is listed in [Board21.h](https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen/blob/master/example/Arduino/Examples/Lesson02-Rotate/Board21.h) (3-wire SPI on CS=16, SCK=2, SDA=1). Backlight PWM: **GPIO 6**.

The RGB peripheral typically needs a large framebuffer (see Unverified RAM notes). Official ESPHome examples require the **ESP-IDF** framework, not Arduino, specifically because of this RGB bus ([ESPHome lesson](https://www.elecrow.com/wiki/2.1-ESPHOME-Lesson02-Light-Up-the-Screen.html); also stated in community write-ups).

LCD power and reset are **not** native GPIOs; they go through a PCF8574 (next section). The wiki ESPHome snippet comments that the boot sequence is “based on Elecrow sample code, **though colors are off**”.

### 2.2 Touch controller

- Official repo ships a **CST826 / CST836U** application note, not a CST816 datasheet ([CST826 CST836U_V1.02.pdf](https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen/blob/master/Datasheet/CST826%20CST836U_V1.02.pdf)).
- That app note states the default 7-bit I²C address is **0x15** (some projects may differ).
- Official C++ examples use `Adafruit_CST8XX` at `I2C_TOUCH_ADDR = 0x15` ([Board21.h](https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen/blob/master/example/Arduino/Examples/Lesson02-Rotate/Board21.h)).
- Community ESPHome configs often use the `cst816` platform at 0x15 on the same bus. That is a **driver family choice**, not proof the stuffed IC is literally CST816.

Touch reset is PCF8574 **P0**; touch INT is PCF8574 **P2** ([wiki](https://www.elecrow.com/wiki/CrowPanel_2.1inch-HMI_ESP32_Rotary_Display_480_IPS_Round_Touch_Knob_Screen.html) / README). I²C bus: **GPIO 38 SDA, GPIO 39 SCL**.

### 2.3 Rotary encoder and push

Schematic text and the official encoder PDF identify **EC28A-C15H15P3** (Shenzhen Yisheng EC28 series) ([encoder datasheet](https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen/blob/master/Datasheet/EC28A%20C15H15P3-.pdf); schematic PDF).

| Item | Datasheet / official pin map |
|---|---|
| Quadrature | Phase A / B, 2-channel |
| Resolution | **15 pulses / 360°** |
| Detents | **30 detents**, 12° ± 2° |
| Rated voltage | DC 5 V (encoder datasheet) |
| Encoder operating / storage temp | **−30 to +80 °C** / **−40 to +85 °C** |
| Official GPIO map | **A = GPIO 42, B = GPIO 4** ([wiki](https://www.elecrow.com/wiki/CrowPanel_2.1inch-HMI_ESP32_Rotary_Display_480_IPS_Round_Touch_Knob_Screen.html), [Board21.h](https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen/blob/master/example/Arduino/Examples/Lesson02-Rotate/Board21.h)) |
| Push / click | **PCF8574 P5**, `INPUT_PULLUP` — **not** a native GPIO |

If rotation direction is reversed in software, swap A/B in firmware (community note, not a hardware defect).

### 2.4 PCF8574 I/O expander

I²C address **0x21** ([wiki](https://www.elecrow.com/wiki/CrowPanel_2.1inch-HMI_ESP32_Rotary_Display_480_IPS_Round_Touch_Knob_Screen.html), schematic text `PCF8574RGT`):

| PCF8574 pin | Function |
|---|---|
| P0 | Touch reset (output) |
| P2 | Touch interrupt |
| P3 | LCD power (output) |
| P4 | LCD reset (output) |
| P5 | Encoder button (input, pull-up) |

The official `boardPowerOn()` sequence in [Board21.h](https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen/blob/master/example/Arduino/Examples/Lesson02-Rotate/Board21.h) is: P3 high (LCD power) → P4 low/high (LCD reset) → P0 low/high (touch reset) → P2 high, with 50–100 ms delays. Firmware that skips this often gets a black screen.

---

## 3. Verified — connectors, USB, power path, GPIOs

### 3.1 Connectors

From Elecrow specs and the schematic PDF text:

- **ZX-MX 1.25-4P** UART (specs say UART0 and UART1).
- **ZX-MX 1.25-4P** I²C (GPIO 38/39, shared with touch and PCF8574).
- **12-pin FPC, 0.5 mm pitch**, labelled as power / programming port. Schematic net names on `CON_12P_0.5` include **VCC_5V**, **GND**, **UART0_TXD0 / UART0_RXD0**, **GPIO38-SDA / GPIO39-SCL**, **USB-D+ / USB-D−**, **RESET**, **GPIO0**.
- ESP32-S3 native USB appears on **GPIO 19/20** (`USB_D−` / `USB_D+`) in the schematic text.
- The retail kit includes a **50 cm MX1.25-to-USB cable**, not a statement that a USB-C receptacle is on the knob housing. Confirm the physical connector on a real unit before designing a console loom.

### 3.2 Power path (from schematic PDF text)

- Board input rail **VCC_5V**.
- 3.3 V rail **VDD_3.3V** is generated by a **RY3420** block labelled **DC-DC** (inductor 2.2 µH, 22 µF caps) — a buck, not an LDO. Part/datasheet of RY3420 was not independently retrieved.
- LCD panel supply **VDD_LCD** is switched from 3.3 V with **PMOS-3401-4A (Q1)**, consistent with PCF8574 P3 “LCD power”.
- A green **PWR** LED is shown on the 3.3 V rail.
- Official input rating is **5 V / 1 A**. **Measured operating current is not published** and was not measured here.

There is **no** official 12 V input. In-car use requires an external 12→5 V converter rated for at least 1 A continuous, preferably with margin, automotive input range, and a fuse.

### 3.3 GPIO budget

Almost every ESP32-S3 GPIO on this board is consumed by the RGB panel, backlight, I²C, or encoder. Official README lists LCD-bus GPIOs: 1, 2, 3, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21, 40, 41, 45, 46, 47, 48.

Practical expansion is the **I²C** and **UART** MX1.25 connectors (and the 12-pin FPC). Extra physical buttons should go on I²C expanders, not “spare” RGB pins.

Note: [Board21.h](https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen/blob/master/example/Arduino/Examples/Lesson02-Rotate/Board21.h) defines `ONBOARD_LED_PIN = 43`. Official Lesson04 uses **GPIO 43/44** as UART1 TX/RX toward an *external* AT Wi-Fi module in a course sketch (the SoC already has Wi-Fi). GPIO 43 is also UART0 TXD. Treat LED vs UART0 conflict as something to verify on the bench before tying the programming cable and an LED together.

No buzzer / vibe net names were found in the schematic text extract.

---

## 4. Verified — software support

| Environment | What exists |
|---|---|
| Arduino | Official examples under `example/Arduino/` (Hello World, rotate, I²C sensor lesson, serial, USB 2.0). Factory-style sketch `RotaryScreen_2_1.ino`. Libraries include GFX_Library_for_Arduino, LVGL **8.3.11** (README), Adafruit_CST8XX, PCF8574. |
| ESP-IDF | Wiki claims support. Official ESPHome path **requires ESP-IDF** as the ESPHome framework. |
| LVGL | Factory / examples use LVGL 8.3.11. Elecrow product page also mentions SquareLine Vision / LVGL 9.5 for UI design (marketing; not the same as the shipped 8.3.11 examples). Official Arduino LVGL setup in `Board21.h` allocates two **partial** buffers: `480 × 40 × 2` bytes each in PSRAM (`LV_DISPLAY_RENDER_MODE_PARTIAL`). |
| ESPHome / Home Assistant | Official YAML and lessons (`example/esphome/`, [Lesson02](https://www.elecrow.com/wiki/2.1-ESPHOME-Lesson02-Light-Up-the-Screen.html)). PSRAM flags (`BOARD_HAS_PSRAM`, `qio_opi`, octal 80 MHz) are mandatory in that example. |
| MicroPython / Lua RTOS / PlatformIO | Claimed on the wiki; not reviewed file-by-file here. |
| CAD | `.stp` in `3D file/` for enclosure work. |

Arduino core **3.3.x** broke older `ledcSetup` / `ledcAttachPin` calls in user sketches ([issue #1](https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen/issues/1)). Current `Board21.h` already uses `ledcAttach(pin, freq, resolution)`.

---

## 5. Community reports (not reproduced here)

Official issue tracker (open as of 2026-11-18 fetch):

| Issue | Summary |
|---|---|
| [#1](https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen/issues/1) (open, 2026-03-24) | Arduino 3.3.7 compile errors (`ledcSetup` removed; also `i80_device_list`). |
| [#2](https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen/issues/2) (open, 2026-04-27) | Reflashing the published factory binary gave a **blank display** (checksums said to match). Unanswered. |
| [#3](https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen/issues/3) (open, 2026-07-28) | Encoder **push** is on PCF8574 P5, not a native GPIO. Author later posted a working ESPHome `binary_sensor` (`inverted: true` on P5). |
| [#5](https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen/issues/5) (open, 2026-10-23) | **Shipped `factory_sourcecode` does not match the factory binary** (splash never advances; graphics differ). |

Home Assistant community:

- Users failing to light the 480×480 panel until they used the official ESPHome YAML (PSRAM + init sequence + PCF8574 power-on). Older “lesson” touch external components were reported incompatible with then-current ESPHome; native `cst816` was suggested ([thread](https://community.home-assistant.io/t/lost-unable-to-make-this-crowpanel-round-480x480-to-work/1016269/6)).
- One CrowPanel 2.1 user reported **display flicker** under an ESPHome/LVGL build while factory firmware was stable ([thread](https://community.home-assistant.io/t/smart-thermostat-knob-esphome/1017885)). Cause was not proven (comments suggested bandwidth / PSRAM / backlight PWM). Treat as a risk for a full-screen car UI.

The official ESPHome lesson itself notes sample colours may be wrong.

---

## 6. Unverified / planned — fitness for the Juniper use case

Nothing in this section has been implemented or measured on a CrowPanel 2.1 in this project.

### 6.1 (a) Emulate S3XY Buttons toward Commander Gen2 — UNVERIFIED / planned

Reference: [Beat-YT/s3xy-virtual-button](https://github.com/Beat-YT/s3xy-virtual-button). That sketch is an ESP32 Arduino/NimBLE **BLE peripheral** that advertises as `ENH_BTN`, bonds to a Gen2 S3XY Commander (Just Works, Secure Connections, bonding), and sends single / double / long-press notifications. It is written and tested as **one virtual button identity per ESP32**.

**Multiple BLE identities on one ESP32** (to appear as several S3XY Buttons, up to the Commander’s accessory limit) is the hard part and is **not solved** by that repo. NimBLE bonding is tied to a single local identity; community discussion of “two devices in one firmware” describes bond-table conflicts ([h2zero/NimBLE-Arduino#959](https://github.com/h2zero/NimBLE-Arduino/issues/959)). Treat multi-identity as **high risk / research**, not a scheduled feature.

Practical first architecture (planned, not proven): **one** bonded virtual button; map the CrowPanel UI (pages, encoder, touch, click) onto that button’s single / double / long actions, and/or later bypass the official Commander button limit via Dorky (6.3).

CrowPanel hardware is **compatible in principle** (ESP32-S3, BLE 5). Porting is still work: RGB/LVGL bring-up, PCF8574 power sequence, and NimBLE on ESP-IDF or Arduino must coexist.

### 6.2 (b) Concurrent passive BTHome v2 scanner — UNVERIFIED / planned

ESP32-S3 NimBLE can be built with **peripheral + observer/central** roles, and ESPHome already has BTHome / Shelly BLU parsers on other boards. **This combination has not been demonstrated** on CrowPanel 2.1 together with a 480×480 RGB UI.

Risks: radio/CPU load next to RGB DMA; Wi-Fi on at the same time (probably disable Wi-Fi in the car); encrypted BTHome (bind key) vs open advertisements; antenna inside a console and possibly a plastic/metal shell.

### 6.3 (c) Later input device for Dorky Commander — UNVERIFIED / planned

Verified about Dorky, so the plan is not confused with other hardware:

- [opencartools/ESP32-DualCAN](https://github.com/opencartools/ESP32-DualCAN) (“Dorky Commander”), and the [marwachs/ESP32-DualCAN](https://github.com/marwachs/ESP32-DualCAN) fork, are described as **ESP32-C6**, dual CAN, BLE for **WebUI / OTA / TeslaBLE**, Wi-Fi file browser, Berry scripts. The public README does **not** document a BTHome observer or a GATT API for an external knob.
- In this fork, branch `cursor/t2can-fd-6c13` (bc7e309) targets and has been bench-verified on a [LILYGO T-2CAN FD](https://github.com/Xinyuan-LilyGO/T-2Can) (ESP32-S3, 16 MB flash, 8 MB PSRAM; Bus0 = CAN-A via MCP2518FD, Bus1 = CAN-B via TWAI, NimBLE). This is the planned in-car Dorky hardware; the upstream README's ESP32-C6 description refers to the original Dorky board.

A CrowPanel talking to Dorky via **BTHome-like advertisements or a GATT write**, then a Berry script, is a **future interface design**, not an existing feature. It may be the cleaner long-term path (no 8-button Commander cap), but it needs firmware on **both** ends.

### 6.4 RAM budget (LVGL 480×480 + NimBLE) — UNVERIFIED estimate

Arithmetic only, not a heap trace:

- One RGB565 full frame: 480 × 480 × 2 = **460,800 bytes** (~450 KiB).
- Double-buffering that FB: ~900 KiB.
- Official Arduino example **partial** LVGL buffers: 2 × 480 × 40 × 2 = **76,800 bytes**, plus whatever the RGB driver allocates internally.
- 8 MB octal PSRAM is, on paper, enough for FBs + UI + NimBLE buffers **if** PSRAM is enabled (`qio_opi` / octal) and the RGB driver places the frame buffer in PSRAM.
- Internal 512 KB SRAM is the tighter constraint (RGB bounce buffers, DMA, NimBLE, stack).

Whether a polished LVGL UI plus BLE peripheral plus BLE scanner fits without flicker or Wi-Fi/BLE coexistence issues is **unverified**. First milestone should be: RGB UI alone, then add one BLE role, then the second.

### 6.5 Power from 12 V, no LiPo — planned wiring, current unmeasured

- Feed an **automotive 12→5 V** buck (wide input, ≥2 A recommended as margin until current is measured; official board rating is 5 V / 1 A).
- Fuse the 12 V feed; use a switched / accessory-stable source, not a LiPo.
- The onboard **RY3420** 5→3.3 V buck wants a reasonably clean 5 V; a cheap open-frame module may inject noise into the RGB panel (unverified).
- **Live AliExpress / DE prices for a specific buck were not captured**; do not invent them. A previously located listing (canonical ID after redirect) is https://www.aliexpress.com/item/2012773152.html (12/24/36/48 V to 5 V 3 A USB-C style); price unknown.

### 6.6 Heat (cabin 70–80 °C) — official ratings vs cabin; suitability unverified

| Limit | Value | Source |
|---|---|---|
| Elecrow **module operating** | −20 to **65 °C** | Product page / wiki / README |
| Elecrow module storage | −40 to 80 °C | same |
| Panel **operating** (P021B002) | −20 to **70 °C** | Panel datasheet |
| Panel storage | −30 to 80 °C | Panel datasheet |
| Encoder operating | −30 to **80 °C** | EC28 datasheet |

A summer cabin peak of **70–80 °C** is **at or above** the module’s 65 °C operating rating and can exceed the panel’s 70 °C operating rating. **This board is not specified for unshaded dashboard / windshield use.** Suitability in a **shaded center console**, after soak, is unverified. Plan: keep it out of direct sun, allow airflow, consider dimming the backlight, and run a soak test with a thermocouple before calling it vehicle-ready. Acrylic/plastic housings and adhesives are additional weak points in heat.

### 6.7 Mounting in the Juniper center console (79 × 79 × 30 mm) — unverified fit

The unit is substantially larger than an M5Dial (51 × 51 mm). Whether 79 × 79 mm sits cleanly in the Juniper console (between lid edges, in a tray, or on a custom pedestal) has **not** been trial-fitted. Official `.stp` should be used to mock a mount.

Print the mount in **PETG or ASA**, not PLA (heat). Add strain relief for the 5 V lead. Do not enclose the board against glass. A cardboard or printed dimensional dummy is a cheap first step.

---

## 7. BOM (prices only as found; no invented figures)

| Item | Notes | Price as found (2026-11-18 unless dated) |
|---|---|---|
| CrowPanel 2.1" Rotary (DHE03921D) | Elecrow store | **USD 35.70**, in stock ([product page](https://www.elecrow.com/crowpanel-2-1inch-hmi-esp32-rotary-display-480-480-ips-round-touch-knob-screen.html)) |
| Same, AliExpress | https://www.aliexpress.com/item/1005009858428977.html | **Unknown** live EUR. CNX Software, 2025-09-19, quoted **USD 43.99** on AliExpress (dated, may be stale) |
| German retail (BerryBase / Reichelt) | Searched earlier in this project for the 2.1" specifically | **Not found** (the 1.28" CrowPanel was EUR 36.90 at BerryBase; do not reuse that price for the 2.1") |
| 12→5 V automotive buck | Required; board is 5 V only | **Unknown**. Candidate listing: https://www.aliexpress.com/item/2012773152.html |
| Fuse, wire, MX1.25 / 12P leads | Vehicle install | **Unknown** |
| PETG/ASA mount | Print from official STP + Juniper scan/tray references | Filament cost **unknown** |
| Optional BTHome buttons (Shelly BLU / Athom) | Scanner path | Athom site previously listed a BTHome button at **USD 7.98**; AliExpress prices not captured. Shelly BLU Button1 has been discontinued by Shelly (vendor site); remaining stock not priced here |

---

## 8. Open risks (priority)

1. **Thermal:** module operating limit 65 °C vs cabin peaks 70–80 °C. Highest hardware risk for year-round in-car use.
2. **Multi-identity S3XY Buttons on one ESP32:** likely infeasible or extremely fragile; plan around **one** bonded `ENH_BTN` or a future Dorky path.
3. **Concurrent RGB UI + BLE peripheral + BLE scanner:** unproven on this board; flicker and radio issues already reported for heavy LVGL alone.
4. **PCF8574 boot sequence and ST7701S init:** easy to get a black or wrong-colour screen; copy official init / power-on verbatim first.
5. **Factory sources:** published factory binary vs source mismatch; factory reflash blank-screen report. Keep a known-good image before experimenting.
6. **Official doc conflicts:** case material (plastic vs aluminum alloy); brightness 300 vs “up to 400”; 2.1" ambient LED mentioned in wiki/README but “Power LED” only in the product comparison table; flash symbol name vs “16 MB” claim.
7. **GPIO poverty:** almost no spare pins; expansion is I²C/UART only.
8. **No haptics.**
9. **5 V only;** buck quality and inrush not characterized.
10. **79 mm footprint** may be awkward in the Juniper console — fit unproven.
11. **Dorky BLE input API does not exist yet**; the planned Dorky hardware is the T-2CAN FD (ESP32-S3) on branch `cursor/t2can-fd-6c13`; upstream Dorky is ESP32-C6.

---

## 9. Suggested next steps

1. Buy one CrowPanel 2.1 and a fused 12→5 V buck; **do not** fit a LiPo.
2. Flash official ESPHome or Arduino bring-up (PSRAM octal, PCF8574 sequence, ST7701S init). Confirm display, touch, encoder, click.
3. **Measure 5 V current** (idle, full backlight, UI motion, BLE advertising/connected). Size the buck from data.
4. **Thermal soak** in shade and, separately, a controlled heat test approaching 65 °C with a probe on the case and PCB. Decide console-only mounting from that, not from the marketing temperature line.
5. Print a **79 × 79 × 30 mm dummy** (PETG/ASA) and check Juniper console clearance; then design a mount around the official STP.
6. Port **a single** [s3xy-virtual-button](https://github.com/Beat-YT/s3xy-virtual-button) identity; pair with a Gen2 Commander. Do not start with multi-identity.
7. Add a **BTHome observer** in a firmware build *without* a heavy UI, then merge with LVGL and re-check stability.
8. Only then sketch a Dorky input protocol (advertisement vs GATT) and a Berry script — after reading the current `docs/scripting.md` in ESP32-DualCAN, because that API is still evolving.
9. Keep Wi-Fi off in the vehicle firmware unless needed for OTA on the bench.
