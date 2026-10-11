import importlib.util
import json
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("device", ROOT / "tools/device.py")
device = importlib.util.module_from_spec(spec)
spec.loader.exec_module(device)


class Connection:
    def __init__(self, chunks):
        self.chunks = list(chunks)
        self.sent = b""

    def __enter__(self):
        assert not self.dtr and not self.rts
        return self

    def __exit__(self, *_args):
        return False

    def write(self, data):
        self.sent += data

    def flush(self):
        pass

    def read_until(self, *_args, **_kwargs):
        return self.chunks.pop(0) if self.chunks else b""


class UsbProtocolTests(unittest.TestCase):
    def test_partial_frames_and_boot_text(self):
        connection = Connection([b"Boot ready\n", b'{"ok":tr', b'ue,"version":"3.0.0"}', b"\n"])
        with patch.object(device.serial, "Serial", return_value=connection):
            result = device.exchange("test-port", {"command": "info"})
        self.assertEqual(result, {"ok": True, "version": "3.0.0"})
        self.assertEqual(json.loads(connection.sent), {"command": "info"})

    def test_no_response_is_an_error(self):
        with patch.object(device.serial, "Serial", return_value=Connection([])):
            with self.assertRaises(TimeoutError):
                device.exchange("test-port", {"command": "info"}, timeout=0)

    def test_bounded_input_and_output(self):
        with patch.object(device.serial, "Serial") as serial:
            with self.assertRaises(ValueError):
                device.exchange("test-port", {"token": "x" * 2048})
            serial.assert_not_called()
        with patch.object(device.serial, "Serial", return_value=Connection([b"x" * 4097])):
            with self.assertRaises(ValueError):
                device.exchange("test-port", {"command": "info"})


if __name__ == "__main__":
    unittest.main()
