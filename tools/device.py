#!/usr/bin/env python3
"""Inspect or configure VibeMon over local USB. Secrets are prompted, never command arguments."""
import argparse
import getpass
import json
import time

import serial


def exchange(port, command, timeout=8):
    message = json.dumps({**command, "requestId": 2}, ensure_ascii=False, separators=(",", ":")).encode("utf-8") + b"\n"
    if len(message) > 1024:
        raise ValueError("USB command exceeds 1024 bytes")
    connection = serial.Serial(baudrate=115200, timeout=0.2, write_timeout=2, exclusive=True)
    connection.dtr = False
    connection.rts = False
    connection.port = port
    with connection:
        # CH340 boards can reset on open despite inactive DTR/RTS. Probe readiness
        # with an idempotent command; send a settings mutation exactly once.
        deadline = time.monotonic() + timeout
        next_probe = 0
        ready = False
        pending = bytearray()
        while time.monotonic() < deadline:
            if not ready and time.monotonic() >= next_probe:
                connection.write(b'{"command":"info","requestId":1}\n')
                connection.flush()
                next_probe = time.monotonic() + 0.5
            pending.extend(connection.read_until(b"\n", size=4096))
            if len(pending) > 4096:
                raise ValueError("The device returned an oversized USB response")
            while b"\n" in pending:
                line, _, remaining = pending.partition(b"\n")
                pending = bytearray(remaining)
                try:
                    response = json.loads(line)
                except ValueError:
                    continue  # Never echo boot messages or board identifiers.
                if not isinstance(response, dict) or not isinstance(response.get("ok"), bool):
                    continue
                if not ready and response.get("requestId") == 1 and response["ok"]:
                    if command == {"command": "info"}:
                        response.pop("requestId")
                        return response
                    ready = True
                    connection.write(message)
                    connection.flush()
                    deadline = time.monotonic() + timeout
                elif ready and response.get("requestId") == 2:
                    response.pop("requestId")
                    return response
    raise TimeoutError("No VibeMon response. Check the port, firmware boot and USB cable.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    commands = parser.add_subparsers(dest="command", required=True)
    for command in ("info", "dashboard", "refresh"):
        commands.add_parser(command)
    wifi = commands.add_parser("wifi")
    wifi.add_argument("--ssid", help="Network name; prompted if omitted")
    cloud = commands.add_parser("cloud")
    cloud.add_argument("--origin", default="https://vibemon.io")
    args = parser.parse_args()
    if args.command == "wifi":
        request = {"command": "configure-wifi", "ssid": args.ssid if args.ssid is not None else input("Wi-Fi network: "), "password": getpass.getpass("Wi-Fi password (blank for an open network): ")}
    elif args.command == "cloud":
        request = {"command": "configure-cloud", "origin": args.origin, "token": getpass.getpass("Read token (blank keeps the saved key on the same server): ")}
    else:
        request = {"command": args.command}
    response = exchange(args.port, request)
    print(json.dumps(response, indent=2))
    if not response["ok"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
