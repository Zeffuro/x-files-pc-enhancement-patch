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
        for name in ("docs/standalone-devtools.md", "docs/devtools-notices.md"):
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

    def test_missing_demuxer(self):
        del self.files["avformat-1.dll"]
        with self.assertRaisesRegex(package.PackageError, "exactly one avformat"):
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


class DevtoolsPackageTests(unittest.TestCase):
    def setUp(self):
        PackageTests.setUp(self)
        self.patch_files = dict(self.files)
        self.zip_path = self.root / "xfiles-devtools-0.1.0-windows-x86.zip"
        self.files = {name: data for name, data in self.files.items()
                      if name in package.DEVTOOLS_REQUIRED or name.startswith("source/ffmpeg/")
                      or name in {f"{c}-1.dll" for c in package.COMPONENTS}}
        self.files["README.md"] = self.patch_files["docs/standalone-devtools.md"]
        self.files["THIRD_PARTY.md"] = self.patch_files["docs/devtools-notices.md"]

    def check(self):
        with zipfile.ZipFile(self.zip_path, "w", zipfile.ZIP_STORED) as output:
            for name, data in self.files.items():
                output.writestr(name, data)
        package.check_package(self.zip_path, "0.1.0", self.script, "devtools")

    def test_complete_without_patch_runtime(self):
        self.check()
        self.assertTrue((package.RUNTIME - package.DEVTOOLS_RUNTIME).isdisjoint(self.files))

    def test_unexpected_game_or_patch_files(self):
        for name in ("QuickTime.qts", "ddraw.dll", "XFilesPlay.exe", "XFiles.exe", "patch.ini",
                     "defaults/patch.ini", "saves/example.x", ".dev_docs/notes.md"):
            with self.subTest(name=name):
                self.files[name] = b"must not be shipped"
                with self.assertRaisesRegex(package.PackageError, "unexpected files"):
                    self.check()
                del self.files[name]

    def test_missing_dependencies_and_notices(self):
        for name in ("avformat-1.dll", "zlib.LICENSE", "FFmpeg.LICENSE", "README.md",
                     "source/ffmpeg/ffmpeg-1.2.3.tar.xz"):
            with self.subTest(name=name):
                original = self.files.pop(name)
                with self.assertRaises(package.PackageError):
                    self.check()
                self.files[name] = original

    def test_changed_readme_or_notices(self):
        for name in ("README.md", "THIRD_PARTY.md"):
            with self.subTest(name=name):
                original = self.files[name]
                self.files[name] += b"unexpected content"
                with self.assertRaisesRegex(package.PackageError, "differs from this checkout"):
                    self.check()
                self.files[name] = original

    def test_wrong_kind_and_version(self):
        self.check()
        with self.assertRaisesRegex(package.PackageError, "Expected package name"):
            package.check_package(self.zip_path, "0.1.0", self.script)
        with self.assertRaisesRegex(package.PackageError, "Expected package name"):
            package.check_package(self.zip_path, "0.1.1", self.script, "devtools")

    def test_bad_source(self):
        self.files["source/ffmpeg/ffmpeg-1.2.3.tar.xz"] = b"incorrect source"
        with self.assertRaisesRegex(package.PackageError, "pinned checksum"):
            self.check()

    def test_x64_browser(self):
        pe = bytearray(self.files["xfiles-devtools.exe"])
        struct.pack_into("<H", pe, 132, 0x8664)
        self.files["xfiles-devtools.exe"] = bytes(pe)
        with self.assertRaisesRegex(package.PackageError, "32-bit x86"):
            self.check()

    def prepare(self):
        spec = importlib.util.spec_from_file_location("package_release", ROOT / "tools/package-release.py")
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        patch = self.root / "xfiles-enhancement-0.1.0-windows-x86.zip"
        with zipfile.ZipFile(patch, "w") as output:
            for name, data in self.patch_files.items():
                output.writestr(name, data)
        return builder.prepare_packages(patch, "0.1.0", self.script)

    def test_release_preparation_and_checksums(self):
        patch, tools = self.prepare()
        package.check_package(tools, "0.1.0", self.script, "devtools")
        with zipfile.ZipFile(tools) as archive:
            self.assertEqual(set(archive.namelist()), set(self.files))
            for name, expected in self.files.items():
                self.assertEqual(archive.read(name), expected, name)
        sums = (self.root / "SHA256SUMS.txt").read_text().splitlines()
        self.assertEqual(len(sums), 2)
        for path, line in zip((patch, tools), sums):
            self.assertEqual(line, f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}")
            self.assertEqual(path.with_suffix(".zip.sha256").read_text().strip(), line)

    def test_invalid_patch_leaves_existing_tools_unchanged(self):
        self.zip_path.write_bytes(b"existing download")
        del self.patch_files["zlib.LICENSE"]
        with self.assertRaisesRegex(ValueError, "Missing files"):
            self.prepare()
        self.assertEqual(self.zip_path.read_bytes(), b"existing download")
        self.assertFalse((self.root / "SHA256SUMS.txt").exists())

    def test_bad_tool_document_leaves_existing_tools_unchanged(self):
        self.zip_path.write_bytes(b"existing download")
        self.patch_files["docs/standalone-devtools.md"] += b"stale document"
        with self.assertRaisesRegex(ValueError, "bundled README.md"):
            self.prepare()
        self.assertEqual(self.zip_path.read_bytes(), b"existing download")
        self.assertFalse((self.root / "SHA256SUMS.txt").exists())


if __name__ == "__main__":
    unittest.main()
