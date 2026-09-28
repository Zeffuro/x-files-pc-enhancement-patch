import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


SPEC = importlib.util.spec_from_file_location(
    "dvd_probe", Path(__file__).resolve().parents[1] / "tools/probe-dvd-media.py")
PROBE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROBE)


def metadata():
    return {"format": {"format_name": "mpeg", "start_time": "0.347367"}, "streams": [
        {"index": 0, "codec_type": "data", "codec_name": "dvd_nav_packet"},
        {"index": 1, "codec_type": "video", "codec_name": "mpeg2video",
         "start_time": "0.414100", "sample_aspect_ratio": "8:9", "field_order": "tt"},
        {"index": 2, "codec_type": "audio", "codec_name": "pcm_dvd",
         "start_time": "0.347367", "sample_rate": "48000"},
    ]}


def run_probe(command, **kwargs):
    output = "ffprobe test\n" if command[-1] == "-version" else json.dumps(metadata())
    return subprocess.CompletedProcess(command, 0, output, "")


class DvdMediaProbeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "DVD source 日本語"
        self.root.mkdir()
        self.output = Path(self.temporary.name) / "inventory.json"
        self.movie = self.root / "scene.VOB"
        self.movie.write_bytes(b"test media")

    def run_main(self, *options):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return PROBE.main([str(self.root), str(self.output), *options])

    @patch.object(PROBE.subprocess, "run", side_effect=run_probe)
    def test_stable_manifest_keeps_stream_offsets_and_no_absolute_paths(self, runner):
        (self.root / "A.mpg").write_bytes(b"second clip")
        (self.root / "ignored.txt").write_text("not media")
        self.assertEqual(self.run_main(), 0)
        first = self.output.read_bytes()
        self.assertEqual(self.run_main(), 0)
        self.assertEqual(first, self.output.read_bytes())
        result = json.loads(first)
        self.assertEqual(result["file_count"], 2)
        self.assertEqual([r["path"] for r in result["files"]], ["A.mpg", "scene.VOB"])
        scene = result["files"][1]
        self.assertEqual(scene["sha256"], hashlib.sha256(b"test media").hexdigest())
        self.assertEqual(scene["streams"], metadata()["streams"])
        self.assertNotIn(str(self.root), first.decode())
        self.assertEqual(runner.call_args.args[0][-1], str(self.movie.resolve()))

    def test_failed_probe_is_recorded_without_losing_other_files(self):
        (self.root / "broken.vob").write_bytes(b"broken")

        def mixed(command, **kwargs):
            if command[-1].endswith("broken.vob"):
                return subprocess.CompletedProcess(command, 1, "", f"{command[-1]}: invalid data")
            return run_probe(command, **kwargs)

        with patch.object(PROBE.subprocess, "run", side_effect=mixed):
            self.assertEqual(self.run_main(), 1)
        report = json.loads(self.output.read_text())
        self.assertEqual(report["error_count"], 1)
        self.assertIn("error", report["files"][0])
        self.assertIn("streams", report["files"][1])
        self.assertNotIn(str(self.root), report["files"][0]["error"])

    def test_timeout_invalid_json_and_empty_streams_are_errors(self):
        for result in [subprocess.TimeoutExpired("ffprobe", 1), "not json", '{"streams": []}']:
            with self.subTest(result=result):
                def broken(*args, **kwargs):
                    if isinstance(result, Exception):
                        raise result
                    return subprocess.CompletedProcess(args[0], 0, result, "")
                with patch.object(PROBE.subprocess, "run", side_effect=broken):
                    record = PROBE.inspect(self.movie, self.root, "ffprobe", 1)
                self.assertIn("error", record)

    @patch.object(PROBE.subprocess, "run", side_effect=run_probe)
    def test_extension_override_and_empty_source(self, runner):
        self.assertEqual(self.run_main("--extension", "xmv"), 1)
        runner.assert_not_called()
        (self.root / "clip.XMV").write_bytes(b"mov")
        self.assertEqual(self.run_main("--extension", "xmv"), 0)
        self.assertEqual(json.loads(self.output.read_text())["files"][0]["path"], "clip.XMV")

    def test_media_cannot_be_overwritten_by_manifest(self):
        self.output = self.movie
        self.assertEqual(self.run_main(), 1)
        self.assertEqual(self.movie.read_bytes(), b"test media")

    def test_output_hard_link_cannot_overwrite_media(self):
        os.link(self.movie, self.output)
        self.assertEqual(self.run_main(), 1)
        self.assertEqual(self.movie.read_bytes(), b"test media")

    def test_file_changes_invalidate_hash_metadata_pair(self):
        def changing(command, **kwargs):
            self.movie.write_bytes(b"different longer media")
            return run_probe(command, **kwargs)
        with patch.object(PROBE.subprocess, "run", side_effect=changing):
            record = PROBE.inspect(self.movie, self.root, "ffprobe", 1)
        self.assertIn("File changed", record["error"])

    def test_directory_links_are_not_traversed(self):
        target = Path(self.temporary.name) / "outside"
        target.mkdir()
        (target / "outside.vob").write_bytes(b"outside")
        try:
            (self.root / "linked").symlink_to(target, target_is_directory=True)
        except OSError:
            self.skipTest("Directory symlinks unavailable")
        self.assertEqual(PROBE.media_paths(self.root, PROBE.DEFAULT_EXTENSIONS), [self.movie])


if __name__ == "__main__":
    unittest.main()
