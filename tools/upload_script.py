#!/usr/bin/env python3
"""Uploads a Berry script over BLE to the Dorky Commander (ESP32-DualCAN),
the same way the web UI does: source (.be) + runtime file preprocessed with
the DBC (.bep), optional RX filters (bus.set_filter), then enable it
(= autostart after reboot) and show N seconds of log.

DBC: the firmware has no DBC on board. Signal names (can_msg_new("DI_speed"),
msg_sig_set(m, "DI_vehicleSpeed", v)) are resolved to bit positions BEFORE
upload. Order:
  1. --bep FILE, otherwise
  2. <script>.bep next to the script (prebuilt, no Node.js needed), otherwise
  3. node tools/dbc_prep/preprocess.ts with --dbc (default: dbc/Model3_VEH.dbc
     dbc/Model3_CH.dbc - the same files the web UI loads for Tesla).

Protocol (firmware/src/protocol/protocol.c):
  Service 6e400001-..., RX (write) 6e400002-..., TX (notify) 6e400003-...
  Frame = 4-byte length LE (upper nibble = type, 0 = JSON) + JSON.

IMPORTANT: disconnect the web UI first. The firmware holds only one session.

Examples (Windows, Python 3.14, pip install bleak):
  python upload_script.py model_y_sim.be --filters bench   # filters + upload + start
  python upload_script.py model_y_sim.be --log 300
  python upload_script.py --filters juniper                # only set filters (car)
  python upload_script.py --filters none                   # filters off (accept all)
  python upload_script.py --show-filters
  python upload_script.py --stop model_y_sim.be            # disable (no autostart)
  python upload_script.py --delete model_y_sim.be
  python upload_script.py --list
"""
import argparse
import asyncio
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

from bleak import BleakClient, BleakScanner

SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
RX_CHAR = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   # we write
TX_CHAR = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"   # device notifies

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
DEFAULT_DBC = [os.path.join(REPO, "dbc", "Model3_VEH.dbc"), os.path.join(REPO, "dbc", "Model3_CH.dbc")]


class Dorky:
    def __init__(self, client, fast=False, quiet_raw=False):
        self.c = client
        self.fast = fast
        self.quiet_raw = quiet_raw
        self.buf = bytearray()
        self.next_id = 1
        self.pending = {}
        self.t0 = time.time()
        self.log_lines = []

    def _on_notify(self, _sender, data: bytearray):
        if len(data) >= 2 and data[0] == 0xFD:          # firmware disconnect reason
            print("!! firmware reports: session replaced (other connection)" if data[1] == 1
                  else f"!! firmware notify 0xFD {data[1]:#x}")
            return
        self.buf += data
        while len(self.buf) >= 4:
            hdr = struct.unpack_from("<I", self.buf)[0]
            ln, typ = hdr & 0x0FFFFFFF, hdr >> 28
            if ln > 256 * 1024:                          # broken stream -> resync
                self.buf.clear()
                return
            if len(self.buf) < 4 + ln:
                return
            payload = bytes(self.buf[4:4 + ln])
            del self.buf[:4 + ln]
            if typ != 0:
                continue
            try:
                msg = json.loads(payload)
            except ValueError:
                print("?? invalid JSON:", payload[:80])
                continue
            self._handle(msg)

    def _handle(self, msg):
        if "id" in msg and "ok" in msg and msg["id"] in self.pending:
            fut = self.pending.pop(msg["id"])
            if not fut.done():
                fut.set_result(msg)
            return
        t = msg.get("type")
        ts = f"[{time.time() - self.t0:7.1f}s]"
        if t == "log":
            line = msg.get("msg") or msg.get("raw") or ""
            self.log_lines.append(line)
            if "msg" in msg:
                print(ts, f"{msg.get('src', '')}:", msg["msg"])
            elif "raw" in msg and not self.quiet_raw:
                print(ts, msg["raw"])
        elif t == "script_update":
            print(ts, "script_update:", msg.get("script"))
        # ignore trace/bus_status/cpu

    async def start(self):
        await self.c.start_notify(TX_CHAR, self._on_notify)

    async def call(self, op, timeout=10.0, **params):
        rid = self.next_id
        self.next_id += 1
        body = json.dumps({"op": op, "id": rid, **params}, separators=(",", ":")).encode()
        frame = struct.pack("<I", len(body) & 0x0FFFFFFF) + body
        fut = asyncio.get_running_loop().create_future()
        self.pending[rid] = fut
        mtu = getattr(self.c, "mtu_size", 23) or 23
        chunk = max(20, min(mtu - 3, 244)) if mtu > 23 else 180
        for off in range(0, len(frame), chunk):
            await self.c.write_gatt_char(RX_CHAR, frame[off:off + chunk], response=not self.fast)
            if self.fast:
                await asyncio.sleep(0.01)
        try:
            resp = await asyncio.wait_for(fut, timeout)
        except asyncio.TimeoutError:
            self.pending.pop(rid, None)
            raise TimeoutError(f"timeout on {op} ({timeout:.0f} s)")
        if not resp.get("ok"):
            raise RuntimeError(f"{op} failed: {resp.get('error')}")
        return resp.get("result")

    async def script_entry(self, fname):
        res = await self.call("script.list", timeout=15.0)
        for s in res.get("scripts", []):
            if s.get("filename") == fname:
                return s
        return None


# ---------------- DBC preprocessing ----------------

def strip_comment_lines(code):
    """Blank out pure comment lines (line numbers stay the same) - saves
    space in the script.write frame; only for the .bep, never for the source."""
    return "\n".join("" if l.lstrip().startswith("#") else l for l in code.split("\n"))


UNRESOLVED = re.compile(r'can_msg_new\(\s*"|can_msg_get\([^,()]+,\s*"|msg_sig_(?:set|get)\([^,()]+,\s*"')


def check_bep(bep, origin):
    m = UNRESOLVED.search(strip_comment_lines(bep))
    if m:
        line = bep.count("\n", 0, m.start()) + 1
        sys.exit(f"{origin}: line {line}: DBC name not resolved - stale .bep or wrong DBC?")


def build_runtime(script, args):
    code = open(script, encoding="utf-8").read()
    uses_dbc = any(c in code for c in ('can_msg_new("', 'msg_sig_set(', 'msg_sig_get(', 'can_msg_get('))
    if args.bep:
        bep, origin = open(args.bep, encoding="utf-8").read(), args.bep
    else:
        side = os.path.splitext(script)[0] + ".bep"
        node = shutil.which("node")
        pre = os.path.join(HERE, "dbc_prep", "preprocess.ts")
        if node and os.path.exists(pre) and all(os.path.exists(d) for d in args.dbc):
            with tempfile.TemporaryDirectory() as td:
                out = os.path.join(td, "out.bep")
                r = subprocess.run([node, pre, script, out, *args.dbc], capture_output=True, text=True)
                sys.stderr.write(r.stderr)
                if r.returncode != 0:
                    sys.exit("DBC preprocessing failed (see above)")
                bep, origin = open(out, encoding="utf-8").read(), "preprocess.ts"
        elif os.path.exists(side):
            if os.path.getmtime(side) + 1 < os.path.getmtime(script):
                print(f"Warning: {side} is older than {script} - regenerate it if needed")
            bep, origin = open(side, encoding="utf-8").read(), side
        elif uses_dbc:
            sys.exit("Script uses DBC names, but neither a .bep nor node + DBC were found.\n"
                     "Fix: put the .bep next to the script or install Node.js.")
        else:
            return code, None
    check_bep(bep, origin)
    print(f"Runtime (.bep) from {origin}")
    return code, strip_comment_lines(bep)


def load_filters(profile, path):
    cfg = json.load(open(path, encoding="utf-8"))
    if profile not in cfg or profile.startswith("_"):
        sys.exit(f"Filter profile '{profile}' not in {path} (available: "
                 f"{', '.join(k for k in cfg if not k.startswith('_'))})")
    out = {}
    for bus, ids in cfg[profile].items():
        out[int(bus)] = [int(x, 0) if isinstance(x, str) else int(x) for x in ids]
    return out


# ---------------- Flow ----------------

async def find(args):
    if args.address:
        return args.address
    dev = None
    if args.name:
        print(f"Scanning for {args.name} ...")
        dev = await BleakScanner.find_device_by_name(args.name, timeout=args.scan)
    else:
        print("Scanning for a Dorky device (service UUID) ...")
    if dev is None:
        devs = await BleakScanner.discover(timeout=5.0, service_uuids=[SERVICE])
        dev = devs[0] if devs else None
    if dev is None:
        sys.exit("Not found. Web UI disconnected? Board powered? Antenna attached?")
    print("Found:", dev.address, dev.name)
    return dev


async def enable_robust(d, fname, timeout):
    """script.enable with a long timeout. A timeout is not fatal if
    script.list (or the log) shows that the script is running."""
    try:
        await d.call("script.enable", timeout=timeout, filename=fname)
        return True
    except TimeoutError as e:
        print(f"{e} - checking status via script.list ...")
    for attempt in range(3):
        try:
            s = await d.script_entry(fname)
            if s and s.get("enabled") and not s.get("errored"):
                print("Status: enabled (response just came late)")
                return True
            if s and s.get("errored"):
                print("Status: ERROR", s.get("error"))
                return False
        except (TimeoutError, RuntimeError) as e:
            print("script.list:", e)
        await asyncio.sleep(2.0)
    if any(("Enabled" in l or "enabled" in l) and fname in l for l in d.log_lines):
        print("Status from log: enabled")
        return True
    print("Status unclear - check with --list")
    return False


async def session(args, target, code, runtime, filters):
    async with BleakClient(target, timeout=30.0) as client:
        print("Connected, MTU", getattr(client, "mtu_size", "?"))
        d = Dorky(client, fast=args.fast, quiet_raw=args.no_raw)
        await d.start()
        await asyncio.sleep(0.5)   # firmware authorises/kicks after ~100 ms
        if not client.is_connected:
            sys.exit("Disconnected immediately: pairing window closed? Press BOOT briefly and retry.")
        info = await d.call("system.info", timeout=15.0)
        print("Firmware:", info.get("fw_version"), "| proto", info.get("proto_version"),
              "| scripting_api", info.get("scripting_api_version"))

        if filters is not None:
            for bus in sorted(filters):
                try:
                    r = await d.call("bus.set_filter", timeout=15.0, bus=bus, ids=filters[bus])
                    n = r.get("count", 0) if r else 0
                    print(f"Filter bus{bus}: {n} IDs" if n else f"Filter bus{bus}: off (all)")
                except RuntimeError as e:
                    if "unknown op" in str(e).lower():
                        print("Firmware does not know bus.set_filter (old image) - filters skipped")
                        break
                    raise
        if args.show_filters or filters is not None:
            try:
                r = await d.call("bus.get_filter", timeout=10.0)
                for b in r.get("buses", []):
                    ids = " ".join(f"{i:03X}" for i in b.get("ids", [])) or "all"
                    print(f"  bus{b['bus']}: {ids}")
            except RuntimeError as e:
                print("bus.get_filter:", e)

        fname = os.path.basename(args.script) if args.script else None
        if args.stop or args.delete:
            if not fname:
                sys.exit("file name missing")
            await d.call("script.delete" if args.delete else "script.disable", timeout=20.0, filename=fname)
            print("deleted" if args.delete else "disabled", fname)
        elif fname:
            params = {"filename": fname, "code": code}
            if runtime is not None:
                params["runtime"] = runtime
            size = len(json.dumps(params).encode())
            print(f"Uploading {fname} (source {len(code.encode())} B"
                  + (f", Runtime {len(runtime.encode())} B" if runtime is not None else "")
                  + f", Frame ~{size} B) ...")
            if size > 32 * 1024 - 64:
                print("Note: frame > 32 KB - needs the new image (64 KB buffer in PSRAM)")
            await d.call("script.write", timeout=45.0, **params)
            print("written")
            if not args.no_start:
                if await enable_robust(d, fname, args.enable_timeout):
                    print("enabled (running now and starts automatically after every reboot)")

        if args.list or fname:
            try:
                res = await d.call("script.list", timeout=15.0)
                for s in res.get("scripts", []):
                    flag = "ON " if s.get("enabled") else "off"
                    err = f"  ERROR: {s.get('error')}" if s.get("errored") else ""
                    print(f"  [{flag}] {s.get('filename')}{err}")
            except (TimeoutError, RuntimeError) as e:
                print("script.list:", e)

        if args.log > 0:
            print(f"Log for {args.log:.0f} s (Ctrl+C to stop) ...")
            end = time.time() + args.log
            try:
                while time.time() < end and client.is_connected:
                    await asyncio.sleep(0.5)
            except (KeyboardInterrupt, asyncio.CancelledError):
                pass
            if not client.is_connected:
                print("Connection lost.")


async def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("script", nargs="?", help="path to the .be file (file name on the device = base name)")
    ap.add_argument("--name", help="BLE name, e.g. Dorky-XXXX (default: first device with the Dorky service UUID)")
    ap.add_argument("--address", help="BLE address instead of scanning, e.g. AA:BB:CC:DD:EE:FF")
    ap.add_argument("--scan", type=float, default=15.0, help="scan time s")
    ap.add_argument("--retries", type=int, default=3, help="connection attempts")
    ap.add_argument("--log", type=float, default=60.0, help="show the log for this many seconds (0 = no)")
    ap.add_argument("--no-start", action="store_true", help="upload only, do not enable")
    ap.add_argument("--enable-timeout", type=float, default=30.0, help="timeout for script.enable (s)")
    ap.add_argument("--stop", action="store_true", help="disable the script (autostart off)")
    ap.add_argument("--delete", action="store_true", help="delete the script on the device")
    ap.add_argument("--list", action="store_true", help="list scripts on the device")
    ap.add_argument("--dbc", nargs="+", default=DEFAULT_DBC, help="DBC files in bus order")
    ap.add_argument("--bep", help="prebuilt .bep file instead of preprocessing")
    ap.add_argument("--filters", help="filter profile from can_filters.json (bench, juniper, none)")
    ap.add_argument("--filters-file", default=os.path.join(HERE, "can_filters.json"))
    ap.add_argument("--show-filters", action="store_true", help="show active RX filters")
    ap.add_argument("--fast", action="store_true", help="write without response (faster)")
    ap.add_argument("--no-raw", action="store_true", help="only Berry print() lines, no ESP logs")
    args = ap.parse_args()

    code = runtime = None
    if args.script and not (args.stop or args.delete):
        code, runtime = build_runtime(args.script, args)
        if not code.endswith("\n"):
            code += "\n"
    filters = load_filters(args.filters, args.filters_file) if args.filters else None

    target = await find(args)
    for attempt in range(1, args.retries + 1):
        try:
            await session(args, target, code, runtime, filters)
            break
        except (TimeoutError, asyncio.TimeoutError, OSError) as e:
            # bleak reports GATT discovery timeouts etc. as TimeoutError/OSError
            print(f"Attempt {attempt}/{args.retries}: {type(e).__name__}: {e}")
            if attempt == args.retries:
                sys.exit("Giving up. Restart the board (with BOOT = safe mode if needed) and try again.")
            await asyncio.sleep(3.0)
            if not args.address:
                target = await find(args)
    print("done, BLE disconnected")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
