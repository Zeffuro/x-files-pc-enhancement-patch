import hashlib
import importlib.util
import pathlib
import struct
import tempfile
import unittest
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("check_package", ROOT / "tools/check-package.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="xfiles-package-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.zip_path = self.root / "xfiles-enhancement-0.1.0-windows-x86.zip"
        self.source = b"Synthetic source archive for package checker tests; not FFmpeg."
        self.script = self.root / "build-ffmpeg.sh"
        recipe = "version=1.2.3\nchecksum=" + hashlib.sha256(self.source).hexdigest() + "\n"
        self.script.write_bytes(recipe.encode("ascii"))
        pe = bytearray(256)
        pe[:2] = b"MZ"
        struct.pack_into("<I", pe, 60, 128)
        pe[128:132] = b"PE\0\0"
        struct.pack_into("<H", pe, 132, 0x14C)
        struct.pack_into("<H", pe, 152, 0x10B)
        self.files = {name: b"test content\n" for name in package.REQUIRED}
        for name in package.RUNTIME | {f"{c}-1.dll" for c in package.COMPONENTS}:
            self.files[name] = bytes(pe)
        for name in ("ddraw.ini", "patch.ini"):
            self.files["defaults/" + name] = (ROOT / "config" / name).read_bytes()
        for name in ("LICENSE", "THIRD_PARTY.md"):
            self.files[name] = (ROOT / name).read_bytes()
        self.files["source/ffmpeg/build-ffmpeg.sh"] = self.script.read_bytes()
        self.files["source/ffmpeg/ffmpeg-1.2.3.tar.xz"] = self.source

    def check(self):
        with zipfile.ZipFile(self.zip_path, "w", zipfile.ZIP_STORED) as output:
            for name, data in self.files.items():
                output.writestr(name, data)
        package.check_package(self.zip_path, "0.1.0", self.script)

    def test_complete(self):
        self.check()

    def test_recipe_line_endings(self):
        recipe = self.script.read_bytes()
        for checkout_ending in (b"\n", b"\r\n"):
            for bundled_ending in (b"\n", b"\r\n"):
                with self.subTest(checkout=checkout_ending, bundled=bundled_ending):
                    self.script.write_bytes(recipe.replace(b"\n", checkout_ending))
                    self.files["source/ffmpeg/build-ffmpeg.sh"] = recipe.replace(
                        b"\n", bundled_ending)
                    self.check()

    def test_invalid_pin(self):
        recipe = self.script.read_bytes()
        checksum = hashlib.sha256(self.source).hexdigest().encode("ascii")
        invalid_recipes = (
            recipe.replace(b"version=1.2.3", b"# version=1.2.3"),
            recipe.replace(b"version=1.2.3", b"version=1.2.3-extra"),
            recipe.replace(checksum, checksum[:-1]),
        )
        for invalid in invalid_recipes:
            for ending in (b"\n", b"\r\n"):
                with self.subTest(recipe=invalid, ending=ending):
                    with self.assertRaisesRegex(package.PackageError, "Cannot read the FFmpeg"):
                        package.ffmpeg_pin(invalid.replace(b"\n", ending))

    def test_changed_recipe_crlf(self):
        recipe = self.script.read_bytes()
        self.script.write_bytes(recipe.replace(b"\n", b"\r\n"))
        self.files["source/ffmpeg/build-ffmpeg.sh"] += b"# different build\n"
        with self.assertRaisesRegex(package.PackageError, "differs from this checkout"):
            self.check()

    def test_live_settings_not_shipped(self):
        for name in ("ddraw.ini", "patch.ini", "preferences.ini", "session.ini", "QUICKSAVE.x"):
            with self.subTest(name=name):
                self.files[name] = b"user data must not be bundled"
                try:
                    with self.assertRaisesRegex(package.PackageError, "unexpected files"):
                        self.check()
                finally:
                    del self.files[name]

    def test_changed_defaults(self):
        self.files["defaults/patch.ini"] += b"\nchanged=1\n"
        with self.assertRaisesRegex(package.PackageError, "bundled defaults/patch.ini"):
            self.check()

    def test_default_line_endings(self):
        for name in ("ddraw.ini", "patch.ini"):
            data = self.files["defaults/" + name]
            self.files["defaults/" + name] = data.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
        self.check()

    def test_missing_notice(self):
        del self.files["zlib.LICENSE"]
        with self.assertRaisesRegex(package.PackageError, "Missing files"):
            self.check()

    def test_game_file(self):
        self.files["XFiles.exe"] = b"must not be shipped"
        with self.assertRaisesRegex(package.PackageError, "unexpected files"):
            self.check()

    def test_private_docs(self):
        self.files[".dev_docs/notes.md"] = b"private"
        with self.assertRaisesRegex(package.PackageError, "unexpected files"):
            self.check()

    def test_x64(self):
        pe = bytearray(self.files["QuickTime.qts"])
        struct.pack_into("<H", pe, 132, 0x8664)
        self.files["QuickTime.qts"] = bytes(pe)
        with self.assertRaisesRegex(package.PackageError, "32-bit x86"):
            self.check()

    def test_bad_pe(self):
        self.files["ddraw.dll"] = b"not a DLL"
        with self.assertRaisesRegex(package.PackageError, "Windows binary"):
            self.check()

    def test_bad_source(self):
        self.files["source/ffmpeg/ffmpeg-1.2.3.tar.xz"] = b"wrong source"
        with self.assertRaisesRegex(package.PackageError, "pinned checksum"):
            self.check()

    def test_changed_recipe(self):
        self.files["source/ffmpeg/build-ffmpeg.sh"] += b"# different build\n"
        with self.assertRaisesRegex(package.PackageError, "differs from this checkout"):
            self.check()

    def test_case_collision(self):
        self.files["readme.md"] = b"collision"
        with self.assertRaisesRegex(package.PackageError, "case-colliding"):
            self.check()

    def test_escape_path(self):
        self.files["../outside"] = b"escape"
        with self.assertRaisesRegex(package.PackageError, "Unsafe package path"):
            self.check()

    def test_extra_dll_version(self):
        self.files["avcodec-2.dll"] = self.files["avcodec-1.dll"]
        with self.assertRaisesRegex(package.PackageError, "exactly one avcodec"):
            self.check()

    def test_wrong_package_version(self):
        self.check()
        with self.assertRaisesRegex(package.PackageError, "Expected package name"):
            package.check_package(self.zip_path, "0.1.1", self.script)

    def test_changed_project_license(self):
        self.files["LICENSE"] += b"extra restriction"
        with self.assertRaisesRegex(package.PackageError, "bundled LICENSE"):
            self.check()

    def test_empty_file(self):
        self.files["defaults/patch.ini"] = b""
        with self.assertRaisesRegex(package.PackageError, "empty"):
            self.check()

    def test_crc_corruption(self):
        self.check()
        data = self.zip_path.read_bytes().replace(self.source, b"x" * len(self.source), 1)
        self.zip_path.write_bytes(data)
        with self.assertRaisesRegex(package.PackageError, "CRC check failed"):
            package.check_package(self.zip_path, "0.1.0", self.script)


if __name__ == "__main__":
    unittest.main()
