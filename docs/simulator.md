# Model Y simulator without a car (T-2CAN FD)

One board, no car and no second CAN adapter: CAN-A transmits simulated Model 3/Y vehicle
data, CAN-B receives it and checks wiring, termination and bitrate.

Files:

| File | Purpose |
|---|---|
| `firmware/test_scripts/model_y_sim.be` | Berry script (transmitter + RX check), signals by DBC name |
| `firmware/test_scripts/model_y_sim.bep` | The same, preprocessed with the DBC (this is what runs on the device) |
| `tools/upload_script.py` | Upload (.be + .bep), set filters, start/stop over BLE (bleak) |
| `tools/dbc_prep/preprocess.ts` | Preprocess .be → .bep with the web UI preprocessor (Node.js) |
| `tools/can_filters.json` | RX filter profiles `bench`, `juniper`, `none` |
| `tools/sim_host/` | Host test: real Berry VM and firmware bindings, CAN driver as a loopback |

## Wiring

Terminals P2 (CAN-A) and P1 (CAN-B) each have four poles: `CANH`, `CANL`, `SGND`, `DGND`.
According to the schematic the order is **not** printed on the board, so measure first.

- CAN-A `CANH` to CAN-B `CANH`
- CAN-A `CANL` to CAN-B `CANL`
- CAN-A `SGND` to CAN-B `SGND`. This is recommended: the `TD501MCAN` are **isolated**
  transceivers, their CAN side is not on board ground, only via 1 MΩ / 1 nF.
- Do not connect `DGND`.

Short, twisted wires are enough.

## Termination (60 Ω)

According to schematic T-2Can-Fd_V1.0, `RZ2` (CAN-A) and `RZ1` (CAN-B) are 120 Ω each and
**fixed** between H and L, with no switch or jumper. When you connect both buses they are in
parallel, which automatically gives ~60 Ω.

Check without power, multimeter between H and L:

- **~60 Ω**: fine.
- **~120 Ω**: one wire has no contact, or a resistor is missing.
- **~40 Ω or less**: an additional terminator is attached.

**The 2-way DIP switch is not the termination.** It switches GPIO3 of the ESP32-S3:
pole "RX" to `INT1#` (RX interrupt) of the MCP2518FD, pole "IO3" to pad 26 of expansion header U8.
The Dorky firmware only uses GPIO3 as an input with pull-up; the position does not matter for
the simulator.

## Power

`VCC_5V` (transceiver supply) comes through a diode from USB (~4.7 V) **or** from the
12–24 V input. If frames are sent but nothing is received, power the board with 12 V via the
screw terminal.

## Starting

**From a PC (recommended):** disconnect the web UI first, otherwise the browser holds the BLE session.

```
pip install bleak
python tools/upload_script.py firmware/test_scripts/model_y_sim.be --filters bench --log 120
```

The tool sets the RX filters (`bus.set_filter`), uploads source and `.bep` in one
`script.write`, enables the script via `script.enable` and shows the log for 120 s. If
`model_y_sim.bep` sits next to the `.be`, the PC does not need Node.js. If `script.enable`
does not answer within 30 s, the tool queries `script.list` and reports "enabled" if the
script is running. On connection errors (GATT timeout) it retries 3 times. The script runs
immediately and starts automatically after every reboot, as soon as the bus is alive or after
5 s at the latest.

- Stop without autostart: `python tools/upload_script.py --stop model_y_sim.be`
- Delete: `python tools/upload_script.py --delete model_y_sim.be`
- Only capture the log: `python tools/upload_script.py --log 300`
- Overview: `python tools/upload_script.py --list`

**Via the web UI:** select vehicle "Tesla Model Y" (loads the DBCs), Automations / Scripts,
create a new script `model_y_sim.be`, paste the content, save (the preprocessor resolves the
signal names) and enable it. Without a loaded DBC the script cannot be saved. Two tiles appear
on the Events page: "Sim Start/Stop" (pause) and "Sim Report".

## Settings

At the top of the script in `mys_cfg`:

| Key | Default | Meaning |
|---|---|---|
| `enabled` | `true` | `false`: loads only, does not transmit |
| `tx_bus` | `0` | TX bus: 0 = CAN-A (MCP2518FD), 1 = CAN-B (TWAI) |
| `rx_bus` | `1` | check bus, `-1` = no check |
| `speed` | `1.0` | time scale of the drive cycle |
| `tick_ms` | `20` | base tick. Not below 20, the firmware allows at most 100 Hz per ID. |
| `report_s` | `10` | interval of the summary |
| `log_phase` | `true` | log phase changes |
| `throttle` | `true` | If nothing arrives for 3 s, the script sends only 1 frame/s per ID so the TX queue does not block the firmware. |

## What is simulated

A 55 s drive cycle, running in a loop:

1. Park: P, driver door open from 3 to 6 s
2. Ready: brake, P → D
3. Accelerate: 0 → 100 km/h, accelerator 40 %, 2.8 m/s², left indicator
4. Cruise: 100 km/h, lane change with right indicator
5. Brake: 100 → 0 km/h, −3.5 m/s²
6. Reverse parking: R, up to 5 km/h, steering angle up to 400°
7. Park: P, trunk open

The frames are on the vehicle bus. Bit layouts come from `dbc/Model3_VEH.dbc`, the periods
from joshwardell/model3dbc (`GenMsgCycleTime`) or are estimated:

| ID | Name | Signals | Period |
|---|---|---|---|
| 0x118 | DI_systemStatus | gear, brake pedal, accelerator, acceleration, systemState | 20 ms (real 10) |
| 0x129 | SCCM_steeringAngleSensor | steering angle, steering rate | 20 ms (real 10) |
| 0x257 | DI_speed | speed (both bit positions 12\|12 and 43\|13), UI speed km/h | 20 ms |
| 0x39D | IBST_status | brake applied, pedal travel | 40 ms (estimated) |
| 0x292 | BMS_socStatus | SoC min/UI/max/avg, drops while driving | 100 ms |
| 0x3F5 | VCFRONT_lighting | indicators left/right (~1.5 Hz), lights, indicator stalk | 100 ms (estimated) |
| 0x102 | VCLEFT_doorStatus | left doors (status and latch) | 100 ms (estimated) |
| 0x103 | VCRIGHT_doorStatus | right doors, trunk latch | 100 ms (estimated) |

0x118, 0x129, 0x257 and 0x39D carry a counter (bit 8\|4) and the Tesla sum checksum
in byte 0, like `firmware/src/can/tesla.c`. Whether 0x129 ("Crc") really uses this checksum
is not confirmed.

## Reading the log

```
SIM cruise v=100 gear=D soc=79.3 | tx=2150 rx=1764
SIM rx/tx (counter IDs) / ok 118:500/500 129:500/500 257:500/500 39D:250/250 292:ok 3F5:ok 102:ok 103:ok
SIM all 8 IDs received
```

- For counter IDs, `rx/tx` is a real count. If the values differ by more than 10 %,
  `SIM losses` is printed (then check termination or error frames).
- `SIM MISSING on bus1: …` means nothing arrives: H/L swapped, contact, bitrate, power.
- `SIM no RX … throttling to 1 Hz` and later `RX back` appear when unplugging and
  re-plugging.

## Host test

```
FW=firmware tools/sim_host/build.sh
mkdir -p /tmp/s/berry_test_scripts && cp firmware/test_scripts/model_y_sim.be* /tmp/s/berry_test_scripts/
cd /tmp/s && SIM_DUMP=/tmp/frames.txt <repo>/tools/sim_host/run_sim 60 22 41   # 60 s, cable off at 22 s, back on at 41 s
python3 <repo>/tools/sim_host/decode_check.py <repo>/dbc/Model3_VEH.dbc /tmp/frames.txt   # pip install cantools
```

The host test uses the real Berry VM and the real files `berry_bindings.c`,
`berry_registry.c`, `script_loader.c`, `can.c` and `rate_limit.c`. Only the CAN driver is
replaced: TX on bus 0 arrives as RX on bus 1.

Regenerate the `.bep` after changing the `.be`:

```
node tools/dbc_prep/preprocess.ts firmware/test_scripts/model_y_sim.be \
     firmware/test_scripts/model_y_sim.bep dbc/Model3_VEH.dbc dbc/Model3_CH.dbc
```

## DBC and Model Y Juniper

The firmware has no DBC on board. As in the web UI, message and signal names are translated
on save against the per-bus DBC into start bit, length, factor and offset (`.bep`).
The bundled Tesla DBCs are used: `dbc/Model3_VEH.dbc` (bus 0, vehicle CAN) and
`dbc/Model3_CH.dbc` (bus 1). Cross-checked against joshwardell/model3dbc and
commaai/opendbc (`tesla_model3_vehicle.dbc`, `tesla_model3_party.dbc`).

| Item | Status |
|---|---|
| Buses on the right A-pillar port, Juniper from SOP10 (Berlin from 2026-04-01) | Party, vehicle and body CAN. Chassis CAN is on the new left port X177 (Tesla schematic, community report) |
| CAN-B in the car | therefore party CAN instead of chassis CAN. For bus 1, `Model3_PARTY.dbc` or opendbc `tesla_model3_party.dbc` fits |
| IDs 0x118, 0x129, 0x257, 0x39D, 0x292, 0x3F5, 0x102, 0x103 | taken from Model 3/Y, **not verified on Juniper** |
| `DI_speed` 0x257 | DBCs differ: VEH `DI_vehicleSpeed` 43\|13, opendbc 12\|12 (VEH name `DI_opdVehicleModelSpeedRef`). The simulator sets both. `DI_uiSpeedUnits` VEH 33\|1, opendbc 32\|1 |
| `IBST_status` 0x39D | VEH 8 bytes, opendbc party 5 bytes |
| Checksum byte 0, counter 8\|4 | Model 3/Y method (`tesla.c`), not verified on Juniper |

Before the first drive with your own scripts: capture 1–2 minutes with `trace.start` (web UI
Trace) and check the values against the instrument cluster.

## RX filters

Only the required IDs are passed to scripts and to BLE streaming.
`bus.set_filter {bus, ids}` stores the list in NVS (`busconfig/f0`, `f1`) and applies immediately.

- CAN-A (MCP2518FD): up to 32 IDs as exact hardware filters.
- CAN-B (TWAI): one combined mask in hardware, the rest is discarded in software.
- Bus health and RX rate still count all accepted frames.
- Transmitting is never filtered.

| Profile | Bus 0 (vehicle) | Bus 1 |
|---|---|---|
| `bench` | same as `juniper` | the 8 simulator IDs |
| `juniper` | 102 103 118 129 257 273 292 294 2C2 2C3 2E1 313 334 39B 39D 3E9 3F5 | Party: 118 129 145 257 2B9 311 39D |
| `none` | everything | everything |

Console: `filter` shows the active lists.

## BLE streaming

- Notifications are only sent once the PC/browser has subscribed to TX, at the latest 2.5 s after connecting. This leaves the BLE buffers to service discovery.
- Trace on subscription only: `trace.start {buses, ids, max_fps}`, default 200 frames/s, maximum 1000. Disconnecting ends the subscription.
- Trace, bus status, CPU info, log mirror and Berry `print` are dropped when the BLE buffer is full instead of blocking the firmware.
- Command responses are never lost.
- Trace is batched: at most 40 notifications/s, each with as many frames as fit into MTU−3 bytes (every 25 ms or when full).
  - Default: legacy `{"type":"trace",…}` frames back to back in one notification (understood by the old web UI).
  - `trace.start {…, batch:true}`: compact `{"type":"trace_batch","f":[[bus,id,ts,"HEX"],…]}`, about 18 instead of 7 frames per notification. The new web UI requests this.
- Trace is only sent when enough NimBLE buffers (msys) are free and internal RAM is above the trace threshold: free ≥ 10 KB and largest block ≥ 4 KB, with 4 KB hysteresis. Otherwise frames wait in a ring (128). If the ring overflows, the oldest are dropped.
  - Change thresholds: `trace.start {…, iram_floor:8192, iram_big_floor:4096}` or USB console `trace gate 8192 4096`.
- Status pushes (bus status, CPU, script status, logs) only check the msys buffers, never the heap. Command responses always wait.
- USB console: `trace status` shows the counters, `mem` shows internal RAM, PSRAM and task stacks. At boot, `mem boot/can/fs/wifi/tesla/scripts/ble` lines appear in the log.
- Wi-Fi only starts if credentials are stored (otherwise it stays off and saves internal RAM). The file server only starts once Wi-Fi has an IP.
- `trace.stop` returns `sent`, `notifies`, `rate_dropped` (max_fps), `ble_dropped` (ring/BLE full), `push_dropped` and `dropped` (old name for `rate_dropped`).
- Heartbeat (debug on) shows `iram`/`iram_min` and a line `trace=… sent=… rate_drop=… ble_drop=… | ble ntf ok=… enomem=… msys_free=…`.

## Safe mode and USB console

**Safe mode:** power on the board or press RESET, **then** press and hold BOOT for at least
0.5 s within the first 3 s. Persistent scripts then do not start. BLE and web UI work
normally. The log shows `SAFE MODE`. Do not hold BOOT during reset: that starts the ROM
download mode.

**Console** (USB, COMx, 115200; e.g. `pio device monitor` or PuTTY), command + Enter:

| Command | Effect |
|---|---|
| `help` | show commands |
| `scripts` | scripts and status |
| `script stop all` | stop all scripts; autostart is kept for the next boot |
| `script stop <file>` / `script start <file>` | individually, persisted |
| `safe` | stop all **and** clear autostart |
| `filter` | active RX filters |
| `debug on` / `debug off` / `debug status` | debug log |
| `reboot` | restart |
