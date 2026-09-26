#!/usr/bin/env python3
"""Read-only hardware checks. Requires pyserial; close other serial monitors."""

import argparse
import json
import re
import time
from pathlib import Path

import serial
from serial.tools import list_ports


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="Default: discover the single Teensy USB serial device")
    parser.add_argument("--expect", choices=("mounted", "absent"), required=True)
    parser.add_argument("--mount", action="store_true", help="Explicitly mount before checking")
    parser.add_argument("--read", metavar="PATH", help="Optional existing small text file")
    parser.add_argument("--psram", action="store_true",
                        help="Require fitted PSRAM and repeat the firmware 4 KiB check")
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args()
    require(args.repeat > 0, "--repeat must be positive")
    require(not args.read or args.expect == "mounted", "--read requires --expect mounted")
    require(not args.read or not any(c in args.read for c in '\r\n"\\'),
            "Unsupported characters in file path")
    report = {"started": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
              "options": {k: str(v) if isinstance(v, Path) else v
                          for k, v in vars(args).items()}, "commands": [], "passed": False}
    started = time.monotonic()
    try:
        if not args.port:
            ports = [p.device for p in list_ports.comports()
                     if (p.vid, p.pid) == (0x16C0, 0x0483)]
            require(len(ports) == 1, f"Expected one Teensy serial device, found {ports}; use --port")
            args.port = ports[0]
        report["port"] = args.port
        with serial.Serial(args.port, 115200, timeout=0.05, write_timeout=2,
                           exclusive=True) as console:
            # CDC readiness may lag opening the host port.
            time.sleep(1)
            console.reset_input_buffer()

            def command(text):
                console.write((text + "\r").encode())
                data = bytearray()
                deadline = time.monotonic() + 12
                while time.monotonic() < deadline:
                    data.extend(console.read(4096))
                    if data.endswith(b"solaros[teensy41]> "):
                        break
                output = data.decode(errors="replace")
                report["commands"].append({"command": text, "output": output})
                require(data.endswith(b"solaros[teensy41]> "),
                        f"Console timeout for {text!r}: {output!r}")
                return output

            def info():
                output = command("info")
                match = re.search(r"Uptime=(\d+) ms heartbeat=(\d+) stack-free=(\d+) words", output)
                require(match, f"Invalid info response: {output}")
                values = tuple(map(int, match.groups()))
                require(values[2] > 0, "No console stack headroom")
                return values

            def memory():
                output = command("mem")
                match = re.search(r"Internal heap: (\d+) free / (\d+) bytes", output)
                require(match, f"Invalid memory response: {output}")
                free, total = map(int, match.groups())
                require(0 < free <= total, "Invalid heap accounting")
                return free

            def sd_check():
                output = command("ls /")
                if args.expect == "absent":
                    require("SD not mounted; use mount" in output, output)
                else:
                    require("SD not mounted" not in output and "Directory not found" not in output,
                            f"SD root listing failed: {output}")
                    if args.read:
                        output = command(f'cat "{args.read}"')
                        require("File not found" not in output and "SD not mounted" not in output,
                                f"SD file read failed: {output}")
                        return output
                return output

            command("")
            first = info()
            print(f"Connected: uptime={first[0]} ms heartbeat={first[1]}", flush=True)
            status = command("sdinfo")
            require("SDIO: mounted=" in status, "Firmware must support sdinfo")
            print(status.strip(), flush=True)
            if args.mount:
                output = command("mount")
                expected = "SDIO: OK" if args.expect == "mounted" else "SDIO: NOT_FOUND"
                require(expected in output, f"Unexpected mount result: {output}")
                print(command("sdinfo").strip(), flush=True)
            sd_baseline = sd_check()
            require("\r\n14\r\n" in command('calc "2 + 3 * 4"'), "Calculator failed")
            def psram_check():
                require("PSRAM 4 KiB test passed" in command("psram"),
                        "Fitted PSRAM check failed")

            before = memory()
            if args.psram:
                psram_check()
            for index in range(args.repeat):
                require("\r\n14\r\n" in command('calc "2 + 3 * 4"'), "Calculator failed")
                require(sd_check() == sd_baseline, "SD output changed between reads")
                if args.psram:
                    psram_check()
                if (index + 1) % 100 == 0:
                    print(f"Completed {index + 1}/{args.repeat} repetitions", flush=True)
            after = memory()
            require(before == after, f"Reported free heap changed: {before} -> {after}")
            time.sleep(1.1)
            last = info()
            require(last[0] > first[0] and last[1] > first[1], "Uptime/heartbeat did not advance")
            report.update(passed=True, heap_free=after, first_info=first, last_info=last)
            print(f"PASS: SD {args.expect}, {args.repeat} repetitions, heap={after}, "
                  f"stack headroom={last[2]} words", flush=True)
    except Exception as exc:
        report["error"] = str(exc)
        raise
    finally:
        report["elapsed_seconds"] = round(time.monotonic() - started, 3)
        args.log.write_text(json.dumps(report, indent=2) + "\n")
        print(f"Log: {args.log}", flush=True)


if __name__ == "__main__":
    main()
