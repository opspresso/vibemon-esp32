#!/usr/bin/env python3
"""Inspect or configure VibeMon over local USB. Secrets are prompted, never command arguments."""
import argparse
import getpass
import json
import time

import serial


def exchange(port, command, timeout=8):
    message = json.dumps(command, ensure_ascii=False, separators=(",", ":")).encode("utf-8") + b"\n"
    if len(message) > 1024:
        raise ValueError("USB command exceeds 1024 bytes")
    connection = serial.Serial(baudrate=115200, timeout=0.2, write_timeout=2, exclusive=True)
    connection.dtr = False
    connection.rts = False
    connection.port = port
    with connection:
        connection.write(message)
        connection.flush()
        deadline = time.monotonic() + timeout
        pending = bytearray()
        while time.monotonic() < deadline:
            pending.extend(connection.read_until(b"\n", size=4096))
            if len(pending) > 4096:
                raise ValueError("The device returned an oversized USB response")
            while b"\n" in pending:
                line, _, remaining = pending.partition(b"\n")
                pending = bytearray(remaining)
                try:
                    response = json.loads(line)
                except ValueError:
                    continue  # Boot messages are not command responses and may contain board identifiers.
                if isinstance(response, dict) and isinstance(response.get("ok"), bool):
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
