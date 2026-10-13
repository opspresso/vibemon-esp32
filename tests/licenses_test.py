import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from collect_licenses import notice_files, write_notices, verify_notices


class LicenseBundleTest(unittest.TestCase):
    def test_preserves_notice_bytes_and_detects_modified_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            payload = {'library/LICENSE': b'Copyright A\r\nPermission\r\n', 'font/OFL.txt': b'Font notice\n'}
            result = write_notices(root, payload, {'library': {'version': '1'}})
            verify_notices(root, result['sha256'])
            self.assertEqual((root / 'library/LICENSE').read_bytes(), payload['library/LICENSE'])
            index = json.loads((root / 'index.json').read_text())
            self.assertEqual(index['files']['font/OFL.txt'], hashlib.sha256(payload['font/OFL.txt']).hexdigest())
            (root / 'font/OFL.txt').write_text('changed')
            with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                verify_notices(root, result['sha256'])

    def test_regeneration_removes_only_recorded_files_and_preserves_unrelated_content(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_notices(root, {'old/LICENSE': b'old'}, {})
            write_notices(root, {'new/LICENSE': b'new'}, {})
            self.assertFalse((root / 'old/LICENSE').exists())
            extra = root / 'keep.txt'; extra.write_text('user file')
            with self.assertRaisesRegex(ValueError, 'unrelated'):
                write_notices(root, {}, {})
            self.assertEqual(extra.read_text(), 'user file')

    def test_rejects_parent_paths_and_symlinks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(ValueError, 'Invalid notice'):
                write_notices(root / 'output', {'../outside': b'bad'}, {})
            outside = root / 'outside'; outside.write_text('preserve')
            (root / 'output/link').symlink_to(outside)
            with self.assertRaises(ValueError):
                write_notices(root / 'output', {'link': b'bad'}, {})
            self.assertEqual(outside.read_text(), 'preserve')

    def test_includes_nested_library_notices_without_test_and_example_data(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            names = ['LICENCE.txt', 'src/font/Font-LICENSE.txt', 'tests/LICENSE', 'examples/LICENSE', 'scripts/license.py']
            for name in names:
                path = root / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_text('notice')
            self.assertEqual([str(path.relative_to(root)) for path in notice_files(root)], names[:2])


if __name__ == '__main__':
    unittest.main()
