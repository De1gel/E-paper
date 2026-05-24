#!/usr/bin/env python3
"""Record one day of device serial logs with timestamps and issue extraction."""

from __future__ import annotations

import argparse
import datetime as dt
import os
import re
import sys
import time
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:
    print(
        "pyserial is required. Run this with PlatformIO's Python or install pyserial.",
        file=sys.stderr,
    )
    raise SystemExit(2) from exc


DEFAULT_BAUD = 115200
DEFAULT_HOURS = 24.0
DEFAULT_LOG_DIR = "logs"
DEFAULT_RECONNECT_DELAY_SEC = 5.0

ISSUE_PATTERNS = [
    re.compile(pattern, re.IGNORECASE)
    for pattern in (
        r"\b(error|failed|fail|timeout|panic|abort|brownout|wdt|exception|invalid)\b",
        r"\[CALSYNC\].*(failed|err=|error)",
        r"\[WIFI\].*(lost|timeout|failed|auth|NO_AP_FOUND)",
        r"\[SLEEP\].*(skip sleep|Error|err=)",
        r"\[HTTP\].*( 4\d\d| 5\d\d)",
    )
]


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def list_serial_ports() -> list[object]:
    return list(list_ports.comports())


def choose_port(preferred: str | None) -> str:
    if preferred:
        return preferred

    ports = list_serial_ports()
    candidates = []
    for port in ports:
        text = " ".join(
            str(part)
            for part in (
                port.device,
                port.description,
                port.manufacturer,
                port.hwid,
            )
            if part
        ).lower()
        if any(key in text for key in ("ch340", "ch341", "usb-serial", "1a86:7522")):
            candidates.append(port.device)

    if len(candidates) == 1:
        return candidates[0]
    if candidates:
        return candidates[0]
    if ports:
        return ports[0].device

    raise RuntimeError("No serial port found. Replug the device and try again.")


def open_serial(port: str, baud: int) -> serial.Serial:
    ser = serial.Serial(
        port=port,
        baudrate=baud,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=1.0,
        write_timeout=1.0,
        dsrdtr=False,
        rtscts=False,
    )
    ser.dtr = False
    ser.rts = False
    return ser


def decode_line(raw: bytes) -> str:
    return raw.decode("utf-8", errors="replace").rstrip("\r\n")


def is_issue(line: str) -> bool:
    return any(pattern.search(line) for pattern in ISSUE_PATTERNS)


def log_paths(log_dir: Path, date_stamp: str, port: str) -> tuple[Path, Path, Path]:
    safe_port = port.replace("\\", "_").replace("/", "_").replace(":", "")
    main = log_dir / f"{date_stamp}_{safe_port}.log"
    issues = log_dir / f"{date_stamp}_{safe_port}.issues.log"
    logger = log_dir / f"{date_stamp}_{safe_port}.logger.log"
    return main, issues, logger


def write_header(handle, port: str, baud: int, duration_hours: float) -> None:
    now = dt.datetime.now().astimezone()
    handle.write(
        f"# serial_day_logger start={now.isoformat(timespec='seconds')} "
        f"port={port} baud={baud} duration_hours={duration_hours}\n"
    )
    handle.flush()


def timestamp() -> str:
    return dt.datetime.now().astimezone().isoformat(timespec="milliseconds")


def write_logger_event(handle, message: str) -> None:
    handle.write(f"{timestamp()} [logger] {message}\n")
    handle.flush()


def run_logger(args: argparse.Namespace) -> int:
    port = choose_port(args.port)
    log_dir = Path(args.log_dir)
    if not log_dir.is_absolute():
        log_dir = repo_root() / log_dir
    log_dir.mkdir(parents=True, exist_ok=True)

    started = dt.datetime.now()
    end_at = time.monotonic() + args.hours * 3600.0
    date_stamp = started.strftime("%Y-%m-%d")
    main_path, issue_path, logger_path = log_paths(log_dir, date_stamp, port)

    print(f"[logger] port={port} baud={args.baud}")
    print(f"[logger] log={main_path}")
    print(f"[logger] issues={issue_path}")
    print(f"[logger] self={logger_path}")
    print("[logger] press Ctrl+C to stop")

    with main_path.open("a", encoding="utf-8") as main_log, issue_path.open(
        "a", encoding="utf-8"
    ) as issue_log, logger_path.open("a", encoding="utf-8") as logger_log:
        write_header(main_log, port, args.baud, args.hours)
        write_header(issue_log, port, args.baud, args.hours)
        write_header(logger_log, port, args.baud, args.hours)

        while time.monotonic() < end_at:
            try:
                with open_serial(port, args.baud) as ser:
                    write_logger_event(logger_log, f"connected port={port}")
                    while time.monotonic() < end_at:
                        raw = ser.readline()
                        if not raw:
                            continue
                        line = decode_line(raw)
                        row = f"{timestamp()} {line}\n"
                        main_log.write(row)
                        main_log.flush()
                        if args.echo:
                            print(row, end="")
                        if is_issue(line):
                            issue_log.write(row)
                            issue_log.flush()
            except serial.SerialException as exc:
                write_logger_event(logger_log, f"serial disconnected: {exc}")
                time.sleep(args.reconnect_delay)
            except OSError as exc:
                write_logger_event(logger_log, f"serial os error: {exc}")
                time.sleep(args.reconnect_delay)

    print("[logger] completed")
    return 0


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="Serial port, for example COM4. Auto-detects CH340 if omitted.")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help=f"Serial baud rate. Default: {DEFAULT_BAUD}")
    parser.add_argument("--hours", type=float, default=DEFAULT_HOURS, help=f"Recording duration. Default: {DEFAULT_HOURS}")
    parser.add_argument("--log-dir", default=DEFAULT_LOG_DIR, help=f"Output directory. Default: {DEFAULT_LOG_DIR}")
    parser.add_argument(
        "--reconnect-delay",
        type=float,
        default=DEFAULT_RECONNECT_DELAY_SEC,
        help=f"Seconds to wait before reopening a disconnected port. Default: {DEFAULT_RECONNECT_DELAY_SEC}",
    )
    parser.add_argument("--echo", action="store_true", help="Also print captured lines to the console.")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        return run_logger(args)
    except KeyboardInterrupt:
        print("\n[logger] stopped by user")
        return 130
    except Exception as exc:
        print(f"[logger] error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
