import importlib.util
import pathlib
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("dvd_mpeg", ROOT / "tools/dvd-mpeg.py")
dvd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dvd)


class RegistrationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="xfiles-dvd-register-")
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        (self.root / "XFiles.exe").write_bytes(b"synthetic DVD executable")
        (self.root / "XFilesMpeg.dll").write_bytes(b"synthetic adapter")
        (self.root / "dlmpgmci.dll").write_bytes(b"original plugin")
        self.ini = self.root / "dlmpg.ini"
        self.old_hash = dvd.DVD_HASH
        dvd.DVD_HASH = dvd.digest(b"synthetic DVD executable")
        self.addCleanup(setattr, dvd, "DVD_HASH", self.old_hash)

    def test_restore_existing_exactly(self):
        original = b"; original comment\r\n[Interface]\r\nDLL=dlmpgmci.dll\r\nextra=yes\r\n"
        self.ini.write_bytes(original)
        dvd.register(self.root)
        self.assertIn(b"dll=XFilesMpeg.dll", self.ini.read_bytes())
        self.assertIn(b"extra=yes", self.ini.read_bytes())
        dvd.register(self.root)
        dvd.register(self.root, True)
        self.assertEqual(self.ini.read_bytes(), original)
        self.assertEqual((self.root / "dlmpgmci.dll").read_bytes(), b"original plugin")

    def test_restore_absent(self):
        dvd.register(self.root)
        dvd.register(self.root, True)
        self.assertFalse(self.ini.exists())
        self.assertFalse((self.root / dvd.STATE).exists())

    def test_preserve_subsequent_edits(self):
        dvd.register(self.root)
        self.ini.write_bytes(b"user edit")
        for disable in (False, True):
            with self.assertRaisesRegex(ValueError, "changed after registration"):
                dvd.register(self.root, disable)
        self.assertEqual(self.ini.read_bytes(), b"user edit")

    def test_reject_wrong_edition(self):
        (self.root / "XFiles.exe").write_bytes(b"CD executable")
        with self.assertRaisesRegex(ValueError, "supported DVD"):
            dvd.register(self.root)
        self.assertFalse(self.ini.exists())

    def test_require_adapter(self):
        (self.root / "XFilesMpeg.dll").unlink()
        with self.assertRaisesRegex(ValueError, "missing"):
            dvd.register(self.root)
        self.assertFalse(self.ini.exists())

    def test_recover_interrupted_registration(self):
        for original in (None, b"[interface]\r\ndll=dlmpgmci.dll\r\n"):
            for disable in (False, True):
                if original is not None:
                    self.ini.write_bytes(original)
                dvd.register(self.root)
                if original is None:
                    self.ini.unlink()
                else:
                    self.ini.write_bytes(original)
                dvd.register(self.root, disable)
                if not disable:
                    self.assertIn(b"dll=XFilesMpeg.dll", self.ini.read_bytes())
                    dvd.register(self.root, True)
                self.assertEqual(self.ini.read_bytes() if self.ini.exists() else None, original)
                self.ini.unlink(missing_ok=True)


if __name__ == "__main__":
    unittest.main()
