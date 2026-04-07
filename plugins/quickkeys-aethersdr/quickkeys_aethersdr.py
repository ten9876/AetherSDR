#!/usr/bin/env python3
"""
quickkeys_aethersdr.py — Xencelabs Quick Keys daemon for AetherSDR.

Reads HID reports from the Quick Keys (USB VID:PID 28BD:5202) and sends
TCI commands to AetherSDR's TCI WebSocket server (ws://localhost:50001).

HID report format (9 bytes, hidraw, report ID 0x02):
  byte 0: 0x02  (report ID)
  byte 1: 0xF0  (always)
  byte 2: button bitmask  (0x01=Btn1 .. 0x80=Btn8, 0=release)
  byte 3: 0x01=left button press, 0x02=knob press, 0x00=release
  byte 7: 0x01=knob CW, 0x02=knob CCW, 0x00=idle

Usage:
  python3 quickkeys_aethersdr.py [--config path/to/config.json] [--verbose]

Requires:
  pip install websocket-client
"""

import argparse
import glob
import json
import logging
import os
import select
import sys
import threading
import time

try:
    import websocket
except ImportError:
    print("ERROR: websocket-client not installed. Run: pip install websocket-client")
    sys.exit(1)

# ── Constants ──────────────────────────────────────────────────────────────────

VENDOR_ID  = 0x28BD
PRODUCT_ID = 0x5202
REPORT_ID  = 0x02
REPORT_LEN = 9
REPORT_SYNC = 0xF0

# Byte 2 — 8 labeled buttons
_BTN_BITS = {
    0x01: "btn1", 0x02: "btn2", 0x04: "btn3", 0x08: "btn4",
    0x10: "btn5", 0x20: "btn6", 0x40: "btn7", 0x80: "btn8",
}

# Band table (name, default_freq_hz)
_BANDS = [
    ("160m",  1_900_000), ("80m",  3_800_000), ("60m",  5_357_000),
    ("40m",   7_200_000), ("30m", 10_125_000), ("20m", 14_225_000),
    ("17m",  18_130_000), ("15m", 21_300_000), ("12m", 24_950_000),
    ("10m",  28_400_000), ("6m",  50_150_000),
]

log = logging.getLogger("quickkeys")


# ── Device discovery ───────────────────────────────────────────────────────────

def find_hidraw_devices() -> list[str]:
    """Return all hidraw paths matching the Xencelabs Quick Keys VID:PID."""
    found = []
    for node in sorted(glob.glob("/dev/hidraw*")):
        name = node.replace("/dev/hidraw", "")
        uevent = f"/sys/class/hidraw/hidraw{name}/device/uevent"
        try:
            with open(uevent) as f:
                content = f.read()
            if f"{VENDOR_ID:08X}:{PRODUCT_ID:08X}".upper() in content.upper():
                found.append(node)
        except OSError:
            continue
    if found:
        log.info(f"Found Quick Keys interfaces: {found}")
    return found


# ── TCI client ─────────────────────────────────────────────────────────────────

class TciClient:
    def __init__(self, host: str, port: int):
        self._url = f"ws://{host}:{port}"
        self._ws = None
        self._connected = False
        self._lock = threading.Lock()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def send(self, command: str):
        with self._lock:
            if self._ws and self._connected:
                try:
                    self._ws.send(command)
                    log.debug(f"TCI → {command!r}")
                except Exception as e:
                    log.error(f"TCI send error: {e}")
            else:
                log.warning(f"TCI not connected, dropped: {command!r}")

    def is_connected(self) -> bool:
        return self._connected

    def _run(self):
        while True:
            try:
                log.info(f"Connecting to TCI at {self._url}")
                self._ws = websocket.WebSocketApp(
                    self._url,
                    on_open=self._on_open,
                    on_close=self._on_close,
                    on_error=self._on_error,
                )
                self._ws.run_forever()
            except Exception as e:
                log.error(f"TCI error: {e}")
            self._connected = False
            log.info("TCI disconnected, retrying in 3s...")
            time.sleep(3)

    def _on_open(self, ws):
        with self._lock:
            self._connected = True
        log.info("TCI connected")

    def _on_close(self, ws, code, msg):
        with self._lock:
            self._connected = False
        log.info(f"TCI closed ({code})")

    def _on_error(self, ws, error):
        log.error(f"TCI error: {error}")


# ── Built-in action handler ────────────────────────────────────────────────────

class BuiltinActions:
    def __init__(self, tci: TciClient, config: dict):
        self._tci = tci
        self._tune_step = int(config.get("tune_step_hz", 1000))
        self._freq_hz = 14_225_000
        self._band_idx = 5   # 20m default
        self._volume = 50
        self._muted = False
        self._mox = False

    def run(self, action: str):
        t = self._tci
        if action == "tune_up":
            self._freq_hz += self._tune_step
            t.send(f"vfo:0,0,{self._freq_hz};")
        elif action == "tune_down":
            self._freq_hz -= self._tune_step
            t.send(f"vfo:0,0,{self._freq_hz};")
        elif action == "band_up":
            self._band_idx = (self._band_idx + 1) % len(_BANDS)
            _, hz = _BANDS[self._band_idx]
            self._freq_hz = hz
            t.send(f"vfo:0,0,{hz};")
            log.info(f"Band → {_BANDS[self._band_idx][0]}")
        elif action == "band_down":
            self._band_idx = (self._band_idx - 1) % len(_BANDS)
            _, hz = _BANDS[self._band_idx]
            self._freq_hz = hz
            t.send(f"vfo:0,0,{hz};")
            log.info(f"Band → {_BANDS[self._band_idx][0]}")
        elif action == "ptt_on":
            t.send("trx:0,true;")
        elif action == "ptt_off":
            t.send("trx:0,false;")
        elif action == "mox_toggle":
            self._mox = not self._mox
            t.send(f"trx:0,{'true' if self._mox else 'false'};")
        elif action == "mute_toggle":
            self._muted = not self._muted
            t.send(f"mute:0,0,{'true' if self._muted else 'false'};")
        elif action == "vol_up":
            self._volume = min(100, self._volume + 5)
            t.send(f"volume:{self._volume};")
        elif action == "vol_down":
            self._volume = max(0, self._volume - 5)
            t.send(f"volume:{self._volume};")
        else:
            log.warning(f"Unknown built-in action: {action!r}")

    # Called by TCI status messages to keep freq in sync (future enhancement)
    def sync_freq(self, hz: int):
        self._freq_hz = hz


# ── HID event dispatcher ───────────────────────────────────────────────────────

class QuickKeysDispatcher:
    _BUILTINS = {
        "tune_up", "tune_down", "band_up", "band_down",
        "ptt_on", "ptt_off", "mox_toggle", "mute_toggle",
        "vol_up", "vol_down", "tune_cw", "tune_ccw",
    }

    def __init__(self, tci: TciClient, config: dict):
        self._tci = tci
        self._builtin = BuiltinActions(tci, config)
        self._buttons_cfg = config.get("buttons", {})
        self._knob_cw  = config.get("knob_cw",  "tune_cw")
        self._knob_ccw = config.get("knob_ccw", "tune_ccw")
        # Track previous button state for release detection
        self._prev_btn_mask = 0x00
        self._prev_b3       = 0x00

    def _dispatch(self, action: str):
        if not action:
            return
        if action in self._BUILTINS:
            # Map tune_cw/ccw to tune_up/down
            if action == "tune_cw":
                self._builtin.run("tune_up")
            elif action == "tune_ccw":
                self._builtin.run("tune_down")
            else:
                self._builtin.run(action)
        else:
            self._tci.send(action)

    def handle_report(self, report: bytes):
        if len(report) < REPORT_LEN:
            return
        if report[0] != REPORT_ID or report[1] != REPORT_SYNC:
            return

        btn_mask = report[2]
        b3       = report[3]
        b7       = report[7]

        # ── 8 labeled buttons (byte 2 bitmask) ──
        changed = btn_mask ^ self._prev_btn_mask
        for bit, name in _BTN_BITS.items():
            if changed & bit:
                cfg = self._buttons_cfg.get(name, {})
                if btn_mask & bit:
                    log.debug(f"{name} press")
                    self._dispatch(cfg.get("press", ""))
                else:
                    log.debug(f"{name} release")
                    self._dispatch(cfg.get("release", ""))
        self._prev_btn_mask = btn_mask

        # ── Left button (byte 3 bit 0) ──
        left_now  = bool(b3 & 0x01)
        left_prev = bool(self._prev_b3 & 0x01)
        if left_now != left_prev:
            cfg = self._buttons_cfg.get("left", {})
            if left_now:
                log.debug("left press")
                self._dispatch(cfg.get("press", ""))
            else:
                log.debug("left release")
                self._dispatch(cfg.get("release", ""))

        # ── Knob press (byte 3 bit 1) ──
        knob_now  = bool(b3 & 0x02)
        knob_prev = bool(self._prev_b3 & 0x02)
        if knob_now != knob_prev:
            cfg = self._buttons_cfg.get("knob", {})
            if knob_now:
                log.debug("knob press")
                self._dispatch(cfg.get("press", ""))
            else:
                log.debug("knob release")
                self._dispatch(cfg.get("release", ""))

        self._prev_b3 = b3

        # ── Knob rotation (byte 7) ──
        if b7 == 0x01:
            log.debug("knob CW")
            self._dispatch(self._knob_cw)
        elif b7 == 0x02:
            log.debug("knob CCW")
            self._dispatch(self._knob_ccw)


# ── Main read loop ─────────────────────────────────────────────────────────────

def _consume(buf: bytes, dispatcher: QuickKeysDispatcher) -> bytes:
    """Parse all complete 9-byte reports from buf, return leftover bytes."""
    while len(buf) >= REPORT_LEN:
        idx = buf.find(bytes([REPORT_ID, REPORT_SYNC]))
        if idx < 0:
            return b""
        if idx > 0:
            buf = buf[idx:]
        if len(buf) < REPORT_LEN:
            break
        dispatcher.handle_report(buf[:REPORT_LEN])
        buf = buf[REPORT_LEN:]
    return buf


def run(hidraw_paths: list[str], dispatcher: QuickKeysDispatcher):
    """Open all hidraw interfaces and dispatch events from whichever produces data."""
    while True:
        fds = {}
        bufs = {}
        for path in hidraw_paths:
            try:
                # buffering=0 → unbuffered raw I/O — each read() goes straight
                # to the kernel so knob rotation events are dispatched immediately
                # rather than sitting in Python's internal read buffer.
                f = open(path, "rb", buffering=0)
                fds[f] = path
                bufs[f] = b""
                log.info(f"Opened {path}")
            except PermissionError:
                log.error(f"Permission denied: {path}. Add user to 'input' group.")
            except OSError as e:
                log.warning(f"Could not open {path}: {e}")

        if not fds:
            log.error("No Quick Keys interfaces could be opened. Retrying in 5s.")
            time.sleep(5)
            continue

        try:
            while True:
                readable, _, _ = select.select(list(fds.keys()), [], [], 1.0)
                for f in readable:
                    chunk = f.read(64)
                    if not chunk:
                        raise OSError("Device disconnected")
                    bufs[f] += chunk
                    bufs[f] = _consume(bufs[f], dispatcher)
        except OSError as e:
            log.warning(f"Device error: {e} — reopening in 2s")
            for f in fds:
                try:
                    f.close()
                except OSError:
                    pass
            time.sleep(2)


# ── Entry point ────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Xencelabs Quick Keys → AetherSDR TCI daemon")
    parser.add_argument("--config", default=os.path.join(os.path.dirname(__file__), "config.json"),
                        help="Path to config.json (default: config.json next to this script)")
    parser.add_argument("--device", default=None,
                        help="hidraw device path (default: auto-detect by VID:PID)")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Enable debug logging")
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
        datefmt="%H:%M:%S",
    )

    # Load config
    try:
        with open(args.config) as f:
            config = json.load(f)
        log.info(f"Config loaded from {args.config}")
    except FileNotFoundError:
        log.warning(f"Config not found at {args.config}, using defaults")
        config = {}
    except json.JSONDecodeError as e:
        log.error(f"Config parse error: {e}")
        sys.exit(1)

    # Find device(s)
    if args.device:
        hidraw_paths = [args.device]
    else:
        hidraw_paths = find_hidraw_devices()
    if not hidraw_paths:
        log.error("Xencelabs Quick Keys not found. Is it plugged in?")
        sys.exit(1)

    # Connect TCI
    host = config.get("tci_host", "localhost")
    port = int(config.get("tci_port", 50001))
    tci = TciClient(host, port)

    dispatcher = QuickKeysDispatcher(tci, config)

    log.info(f"Quick Keys daemon starting — interfaces={hidraw_paths} tci={host}:{port}")
    run(hidraw_paths, dispatcher)


if __name__ == "__main__":
    main()
