import importlib.util
import json
import hashlib
import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("device", ROOT / "tools/device.py")
device = importlib.util.module_from_spec(spec)
spec.loader.exec_module(device)

flash_spec = importlib.util.spec_from_file_location("flash", ROOT / "tools/flash_firmware.py")
flash = importlib.util.module_from_spec(flash_spec)
flash_spec.loader.exec_module(flash)

release_spec = importlib.util.spec_from_file_location("firmware_release", ROOT / "tools/firmware_release.py")
release = importlib.util.module_from_spec(release_spec)
release_spec.loader.exec_module(release)


class UpdateDescriptionTests(unittest.TestCase):
    def test_describes_the_tagged_application_image(self):
        image = {"size": 3800000, "sha256": "a" * 64}
        self.assertEqual(release.update_description("3.4.0", image), {
            "board": "ESP32-4848S040", "version": "3.4.0",
            "url": "https://github.com/opspresso/vibemon-esp32/releases/download/v3.4.0/vibemon-esp32.ino.bin",
            "size": 3800000, "sha256": "a" * 64})

    def test_rejects_versions_and_images_the_device_refuses(self):
        with self.assertRaisesRegex(ValueError, "MAJOR.MINOR.PATCH"):
            release.update_description("3.4.0-rc.1", {"size": 1, "sha256": "a" * 64})
        with self.assertRaisesRegex(ValueError, "update slot"):
            release.update_description("3.4.0", {"size": 0x700001, "sha256": "a" * 64})


class UploadIntegrityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.manifest = {"board": "ESP32-4848S040", "images": {}}
        for name, offset in [("vibemon-esp32.ino.bootloader.bin", 0), ("vibemon-esp32.ino.partitions.bin", 0x8000), ("boot_app0.bin", 0xe000), ("vibemon-esp32.ino.bin", 0x10000)]:
            self.image(name, offset, b"firmware")

    def image(self, name, offset, data):
        (self.directory / name).write_bytes(data)
        self.manifest["images"][name] = {"offset": offset, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        (self.directory / "manifest.json").write_text(json.dumps(self.manifest))

    def test_valid_individual_images(self):
        self.assertEqual(flash.verify_images(self.directory), self.manifest)

    def test_changed_binary_is_rejected(self):
        (self.directory / "vibemon-esp32.ino.bin").write_bytes(b"modified")
        with self.assertRaisesRegex(ValueError, "integrity"):
            flash.verify_images(self.directory)

    def test_nvs_overlap_is_rejected_even_with_matching_hash(self):
        self.image("vibemon-esp32.ino.partitions.bin", 0x8000, b"x" * 0x1001)
        with self.assertRaisesRegex(ValueError, "NVS"):
            flash.verify_images(self.directory)

    def test_app_cannot_overflow_into_the_second_update_slot(self):
        self.image("vibemon-esp32.ino.bin", 0x10000, b"x" * (0x700000 + 1))
        with self.assertRaisesRegex(ValueError, "application partition"):
            flash.verify_images(self.directory)


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
        connection = Connection([b"Boot ready\n", b'{"ok":tr', b'ue,"requestId":1,"version":"3.0.0"}', b"\n"])
        with patch.object(device.serial, "Serial", return_value=connection):
            result = device.exchange("test-port", {"command": "info"})
        self.assertEqual(result, {"ok": True, "version": "3.0.0"})
        self.assertEqual(json.loads(connection.sent), {"command": "info", "requestId": 1})

    def test_settings_sent_once_after_boot_and_stale_probe_ignored(self):
        connection = Connection([
            b"ROM boot text\n", b'{"ok":true,"requestId":1}\n',
            b'{"ok":true,"requestId":1}\n',
            b'{"ok":false,"requestId":2,"error":"rejected"}\n',
        ])
        request = {"command": "configure-wifi", "ssid": "test", "password": "test-password"}
        with patch.object(device.serial, "Serial", return_value=connection):
            result = device.exchange("test-port", request)
        self.assertEqual(result, {"ok": False, "error": "rejected"})
        sent = [json.loads(line) for line in connection.sent.splitlines()]
        self.assertEqual(sent, [{"command": "info", "requestId": 1}, {**request, "requestId": 2}])

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
