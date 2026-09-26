#!/usr/bin/env python3
"""Exercise the upstream Teensy USB shell. Read-only SD checks; requires pyserial."""
import argparse
import json
import re
import time
from pathlib import Path
import serial
from serial.tools import list_ports

ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
PROMPT = "user@teensy41:/ "


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port")
    parser.add_argument("--read", help="Existing small text file, absolute SD path")
    parser.add_argument("--repeat", type=int, default=100)
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args()
    require(args.repeat > 0, "--repeat must be positive")
    require(not args.read or (args.read.startswith("/") and not any(c in args.read for c in '\r\n"\\')),
            "--read must be an absolute path without quotes/control characters")
    report = {"started": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "commands": [], "passed": False}
    started = time.monotonic()
    try:
        ports = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x16C0, 0x0483)]
        if not args.port:
            require(len(ports) == 1, f"Expected one Teensy: {ports}")
            args.port = ports[0]
        with serial.Serial(args.port, 115200, timeout=.05, write_timeout=2, exclusive=True) as console:
            # Opening/reconnecting the USB port starts a fresh shell session.
            time.sleep(1)
            console.reset_input_buffer()

            def command(raw, expected_prompt=PROMPT):
                console.write(raw)
                data = bytearray()
                deadline = time.monotonic() + 12
                while time.monotonic() < deadline:
                    data.extend(console.read(4096))
                    text = ANSI.sub("", data.decode(errors="replace")).replace("\r", "")
                    if text.endswith(expected_prompt):
                        report["commands"].append({"input": raw.decode(errors="replace"),
                                                   "output": data.decode(errors="replace")})
                        return text
                report["commands"].append({"input": repr(raw), "output": data.decode(errors="replace")})
                raise RuntimeError(f"Timed out for {raw!r}: {data[-1000:]!r}")

            def check(raw, expected, prompt=PROMPT):
                out = command(raw, prompt)
                require(expected in out, f"Missing {expected!r} in response: {out!r}")
                return out

            def memory():
                out = command(b"mem\r")
                match = re.search(r"Internal heap: (\d+) free / (\d+) bytes; PSRAM: (\d+) free / (\d+) bytes", out)
                require(match, f"Invalid memory response: {out}")
                values = tuple(map(int, match.groups()))
                require(0 < values[0] <= values[1] and values[2] <= values[3], "Invalid memory accounting")
                return values

            def uptime():
                out = command(b"uptime\r")
                match = re.search(r"Uptime=(\d+) ms stack-free=(\d+) words", out)
                require(match and int(match[2]) > 0, f"Invalid uptime/headroom: {out}")
                return tuple(map(int, match.groups()))

            command(b"\r")
            first = uptime()
            check(b"help\r", "list shell commands")
            check(b"apps\r", "scientific calculator")
            check(b'echo "hello world"\r', "\nhello world\n")
            check(b'calc -e "2 + 3 * 4"\r', "\n14\n")
            check(b'calc -e "sqrt(81)"\r', "\n9\n")
            check(b'calc -e "2 +"\r', "calc: expected a number")
            check(b'calc --bad-option\r', "usage: calc")
            check(b'unknown_command\r', "unknown command")
            check(b'echo "unterminated\r', "quote")
            check(b'echx\x7fo edited\r', "\nedited\n")
            check(b'echo stale\x03echo cancelled\r', "\ncancelled\n")
            check(b'echo history-marker\r', "\nhistory-marker\n")
            check(b'\x1b[A\r', "\nhistory-marker\n")
            check(b'ech\to completed\r', "\no completed\n")
            check(b'echo crlf\r\n', "\ncrlf\n")
            check(b'calc\r', "SolarOS calculator", "> ")
            check(b'6 * 7\r', "\n42\n", "> ")
            command(b'\x1d')
            check(b'echo resumed\r', "\nresumed\n")
            check(b'calc\r', "SolarOS calculator", "> ")
            command(b':quit\r')
            check(b'exit\r', "cannot close the last shell")
            command(b'cd /\r')
            check(b'cd /__solaros_missing_directory__\r', "cd:")
            check(b'cat /__solaros_missing_file__\r', "cat:")
            listing = command(b'ls /\r')
            require("ls:" not in listing, f"Root listing failed: {listing}")
            file_command = f'cat "{args.read}"\r'.encode() if args.read else None
            baseline = command(file_command) if file_command else listing
            require(not file_command or "cat:" not in baseline, f"File read failed: {baseline}")
            before = memory()
            for i in range(args.repeat):
                check(b'calc -e "6 * 7"\r', "\n42\n")
                result = command(file_command or b'ls /\r')
                require(result == baseline, "SD read changed")
                if (i + 1) % 100 == 0:
                    print(f"Completed {i + 1}/{args.repeat}", flush=True)
            after = memory()
            require(before == after, f"Memory changed: {before} -> {after}")
            last = uptime()
            require(last[0] > first[0], "Uptime did not advance")
            report.update(passed=True, repeats=args.repeat, first_info=first, last_info=last, memory=after)
            print(f"PASS: upstream shell, calculator lifecycle, editing/history, SD reads; {args.repeat} cycles; memory={after}")
    except Exception as exc:
        report["error"] = str(exc)
        raise
    finally:
        report["elapsed_seconds"] = round(time.monotonic() - started, 3)
        args.log.write_text(json.dumps(report, indent=2) + "\n")
        print(f"Log: {args.log}")


if __name__ == "__main__":
    main()
