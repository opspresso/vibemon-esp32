#!/usr/bin/env python3
"""Verify or update the public App contract pinned in app-contract.json."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
SOURCES = {
    "constants.json": "src/shared/data/constants.json",
    "monitoring.json": "src/shared/data/monitoring.json",
    "cases.json": "src/shared/monitoring/cases.json",
}


def verify():
    manifest = json.loads((ROOT / "app-contract.json").read_text())
    if manifest["repository"] != "opspresso/vibemon-app" or not re.fullmatch(r"[0-9a-f]{40}", manifest["revision"]):
        raise ValueError("Pin the public App repository by full commit SHA")
    if set(manifest["files"]) != set(SOURCES):
        raise ValueError("The pinned contract must contain every declared source")
    for filename, digest in manifest["files"].items():
        data = (ROOT / "contracts" / filename).read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError("Contract snapshot changed: " + filename)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", help="Update to this full App commit SHA")
    parser.add_argument("--app-dir", type=Path, help="Read the exact commit from a local App checkout")
    args = parser.parse_args()
    if args.revision:
        if not re.fullmatch(r"[0-9a-f]{40}", args.revision):
            parser.error("Use a full App commit SHA")
        payload = {}
        for filename, source in SOURCES.items():
            if args.app_dir:
                data = subprocess.check_output(["git", "show", args.revision + ":" + source], cwd=args.app_dir)
            else:
                url = "https://raw.githubusercontent.com/opspresso/vibemon-app/" + args.revision + "/" + source
                with urllib.request.urlopen(url, timeout=30) as response:
                    data = response.read(1024 * 1024 + 1)
            if len(data) > 1024 * 1024:
                raise ValueError("Contract file exceeds its size limit")
            json.loads(data)
            payload[filename] = data
        (ROOT / "contracts").mkdir(exist_ok=True)
        for filename, data in payload.items():
            (ROOT / "contracts" / filename).write_bytes(data)
        manifest = {"repository": "opspresso/vibemon-app", "revision": args.revision,
                    "files": {name: hashlib.sha256(data).hexdigest() for name, data in payload.items()}}
        (ROOT / "app-contract.json").write_text(json.dumps(manifest, indent=2) + "\n")
    manifest = verify()
    print("Verified public App contract at " + manifest["revision"])


if __name__ == "__main__":
    main()
