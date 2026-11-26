# Dorky Commander — Firmware

Open-source alternative to the S3XY Commander. The production firmware targets the ESP32-C6 Dorky board (two TWAI controllers) and, as a second build, the LilyGo T-2CAN FD (ESP32-S3: MCP2518FD on bus 0, TWAI on bus 1). BLE transport, scripting VM, CAN caching, wifi file server, OTA, and TeslaBLE are the same application on both. The web UI connects over BLE and does not change.



### Berry scripting

The firmware runs a Berry VM with LittleFS (`/scripts/*.be`) managed by `script_loader` — on boot it scans the directory, defers enabling until the CAN buses are alive (or a timeout), restores the persisted enabled set, and invokes `setup()` on enable and `teardown()` on disable when present. Scripts typically register timers, and Dashboard actions in `setup()` and optionally clean up in `teardown()`. See [`../scripts/README.md`](../scripts/README.md) for the Gallery header/skeleton and [`../docs/scripting.md`](../docs/scripting.md) for the full API.

---

## What you need

- **Dorky Commander board** (ESP32-C6-SuperMini + dual TCAN1044 transceivers), or a **LilyGo T-2CAN FD** (see [`../docs/wiring-t2can-fd-s3xy.md`](../docs/wiring-t2can-fd-s3xy.md))
- **USB-C cable** for first flash
- **Chrome or Chromium** (required for Web Bluetooth on all platforms)
- A Tesla (or any vehicle with a supported DBC) for CANbus interaction

See [../README.md](../README.md) for the first-use walkthrough (flashing, BLE connection, DBC, scripts, and actions).

---

---

## Development

Clone with submodules (required — the BLE and Berry components are git submodules):

```bash
git clone https://github.com/opencartools/ESP32-DualCAN.git --recurse-submodules
```

Or update submodules later:

```bash
git submodule update --init --recursive
```

### Build & Flash

Recommended: [PlatformIO IDE extension for VS Code](https://platformio.org/install) — open the `firmware` folder in VS Code and use **Build** / **Upload** from the sidebar.

CLI alternative (requires [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html)):

```bash
cd firmware
pio run -e esp32-c6              # Dorky ESP32-C6
pio run -e esp32-c6 -t upload
pio run -e t2can-fd              # LilyGo T-2CAN FD (ESP32-S3)
pio run -e t2can-fd -t upload
```

`t2can-fd` is `-DBOARD_T2CAN_FD`. Bus 0 (vehicle CAN) is the MCP2518FD on SPI; bus 1 (chassis CAN) is the single ESP32-S3 TWAI. Swap that in `src/board/board_t2can_fd.h` (`BOARD_T2CAN_MCP_BUS`) if you need the opposite. The MCP2518FD driver is Pierre Molinaro's [ACAN2517FD](https://github.com/pierremolinaro/acan2517FD) (MIT, copyright 2018 Pierre Molinaro), vendored under `components/acan2517fd/`, run in CAN 2.0B mode at the NVS bitrate (default 500 kbit/s). CAN FD is not enabled; the data-phase timing is still programmed at 1× so turning it on later is a mode change.

A raw esptool write of a T-2CAN FD build (chip `esp32s3`, 16 MB flash). Offsets match `partitions-16mb.csv`. Prefer `pio run -e t2can-fd -t upload`, which writes the bootloader and the partition table as well.

```bash
esptool.py --chip esp32s3 -b 921600 write_flash \
  0x0     .pio/build/t2can-fd/bootloader.bin \
  0x8000  .pio/build/t2can-fd/partitions.bin \
  0xd000  .pio/build/t2can-fd/ota_data_initial.bin \
  0x10000 .pio/build/t2can-fd/firmware.bin
```

The PlatformIO board is `esp32-s3-devkitc1-n16r8` (16 MB quad flash, 8 MB octal PSRAM). `esp32-s3-devkitc-1` in this platform is the 8 MB / no-PSRAM module and is the wrong target.

Do not flash an `esp32-c6` binary onto the T-2CAN FD.

The T-2CAN FD build also talks to an Adafruit microSD Breakout (#4682) on SPI3 (U8 pads: SCLK `IO15`, MISO `IO4`, MOSI `IO5`, CS `IO16`; 3.3 V only). It is mounted at `/sdcard` when a card is present. SPI2 stays on the MCP2518FD. Console: `sd status`, `sd mount`, `sd unmount`, `sd ls`, `sd test`, `sd format yes` (wipes the card, writes MBR + FAT32). A missing card does not fail the boot.

#### CAN log to microSD (T-2CAN FD)

`sd log start [prefix]` dumps every CAN frame (both buses, RX and TX, before the script/trace RX filter) to `/sdcard/logs/<prefix>_NNNNNN.csv` (default prefix `can`). Frames go into a 16384-frame PSRAM ring from the CAN poll loop (never blocks; overflow is counted as `dropped`), a low-priority writer task formats them in a 4 KB internal DMA-capable buffer (its stack lives in PSRAM) and writes to the card, with `fflush` + `fsync` every 2 s and on rotation/stop.

- Format: SavvyCAN native CSV (`Time Stamp,ID,Extended,Dir,Bus,LEN,D1..D8`, timestamps in µs since boot, IDs as 8-digit hex) — opens directly in SavvyCAN (*Load Log File → GVRET native CSV*) or a spreadsheet.
- Markers (`sd log mark <label>`, `sd_log_mark()`) go to a sidecar `<prefix>_NNNNNN.mrk` (`Time Stamp,Label`) with the same timebase, so the CSV stays loadable unchanged.
- Rotation: a new file every 16 MB. Numbering continues from the highest number in `/sdcard/logs` (shared by all prefixes), so a reboot never overwrites a log.
- Free-space guard: before each new file, the lowest-numbered logs (and their `.mrk`) are deleted until at least 256 MB are free. The open file is never deleted.
- Console: `sd log start [prefix]`, `sd log stop`, `sd log status`, `sd log mark <label>`, `sd log ls`, `sd log limits <file_MB> [min_free_MB]` (runtime only). `sd unmount` / `sd format yes` stop the logger first.
- Berry: `sd_log_start([prefix])`, `sd_log_stop()`, `sd_log_mark(label)`, `sd_log_status()` (see [docs/scripting.md](../docs/scripting.md)). On boards without an SD slot these return `false` / `nil` and the logger compiles out (`BOARD_HAS_SD`).


### Monitor & Test

```bash
cd firmware
pio device monitor               # serial console (115200 baud)
# On the console: "debug on" / "debug off" / "debug status"
# (persistent NVS flag; see docs/ble.md, section Debug-Modus)
pio test -e tests-native         # host unit tests (gcc required), including ACAN2517FD bit timing
pio test -e esp32-c6-tests-arduino  # hardware smoke tests (board connected)
```

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| Board not showing in Bluetooth dialog | Use Chrome, check board is powered. |
| Script errors in log panel | Check the `error` field in Scripts list. |

| Device unresponsive after bad script | Power-cycle the board (or wait for the car to sleep). Bad scripts are isolated; others still run. |


See [`../README.md`](../README.md) for hardware schematic and pin assignments.
