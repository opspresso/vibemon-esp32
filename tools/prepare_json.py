#!/usr/bin/env python3
"""Preserve binary64 measurements in the isolated, pinned ArduinoJson parser."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
START = "#if ARDUINOJSON_USE_DOUBLE\n  bool isDouble ="
END = "\n}\n\ntemplate <typename T>"
REPLACEMENT = """#if ARDUINOJSON_USE_DOUBLE
  // VibeMon measurements use binary64, matching the authenticated Web API.
  // Do not narrow decimal tokens to float based on their mantissa length.
  double value = 0;
  const auto parsed = std::from_chars(original, s, value);
  if (parsed.ec != std::errc() || parsed.ptr != s)
    return Number();
  return Number(value);
#else
  auto value = make_float(float(mantissa), exponent);
  return Number(is_negative ? -value : value);
#endif"""


def patched(source):
    if source.count(START) != 1 or source.count(END) != 1:
        raise ValueError("The pinned ArduinoJson parser layout changed")
    source = source.replace("#pragma once\n", "#pragma once\n\n#include <charconv>\n", 1)
    source = source.replace("  bool is_negative = false;", "  const char* original = s;\n  bool is_negative = false;", 1)
    return source[:source.index(START)] + REPLACEMENT + source[source.index(END):]


def prepare():
    lock = json.loads((ROOT / "toolchain.lock.json").read_text())
    path = ROOT / "build/arduino/libraries/ArduinoJson/src/ArduinoJson/Numbers/parseNumber.hpp"
    source = path.read_bytes()
    digest = hashlib.sha256(source).hexdigest()
    if digest == lock["arduinojson_precise_parser_sha256"]:
        return
    if digest != lock["arduinojson_number_parser_sha256"]:
        raise ValueError("ArduinoJson numeric parser differs from the locked source")
    output = patched(source.decode()).encode()
    if hashlib.sha256(output).hexdigest() != lock["arduinojson_precise_parser_sha256"]:
        raise ValueError("The numeric parser patch differs from the locked result")
    path.write_bytes(output)


if __name__ == "__main__":
    prepare()
