import ctypes
import os
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from visual import WIDTH, HEIGHT, CELL, COLS, INPUT, marker_matches, clock_bounds, template_matches
from calibrate_visual import nearest_rank
from validate_visual import validate


def fixture_pixels(code):
    pixels = bytearray()
    for y in range(HEIGHT):
        for x in range(WIDTH):
            bit = (y // CELL) * COLS + x // CELL
            value = 240 if code & (1 << bit) else 16
            pixels.extend((value, value, value, 255))
    return pixels


class VisualMath(unittest.TestCase):
    def test_template_checks_all_rgb_not_alpha(self):
        self.assertTrue(template_matches(b"\x10\x20\x30\x00", b"\x10\x20\x30\xff"))
        self.assertFalse(template_matches(b"\x10\x20\x35\x00", b"\x10\x20\x30\xff"))
        with self.assertRaises(ValueError):
            template_matches(b"", b"")

    def test_marker_rejects_stale_or_partial_state(self):
        code = 0x639AC4B7
        pixels = fixture_pixels(code)
        self.assertTrue(marker_matches(pixels, WIDTH, HEIGHT, code))
        self.assertFalse(marker_matches(pixels, WIDTH, HEIGHT, code ^ 1))
        # One wrong center channel must invalidate an otherwise complete marker.
        pixels[((CELL // 2) * WIDTH + CELL // 2) * 4] = 100
        self.assertFalse(marker_matches(pixels, WIDTH, HEIGHT, code))

    def test_color_tolerance_and_shape(self):
        pixels = fixture_pixels(0)
        for i in range(0, len(pixels), 4):
            pixels[i] += 4
        self.assertTrue(marker_matches(pixels, WIDTH, HEIGHT, 0))
        self.assertFalse(marker_matches(pixels, WIDTH, HEIGHT, 0, tolerance=3))
        with self.assertRaises(ValueError):
            marker_matches(pixels[:-1], WIDTH, HEIGHT, 0)

    def test_clock_offset_interval_contains_known_offset(self):
        # host->worker delay 3, reply delay 7, remote clock is +50.
        exchanges = [{"before_ns": 100, "remote_ns": 153, "after_ns": 110},
                     {"before_ns": 200, "remote_ns": 260, "after_ns": 230}]
        result = clock_bounds(exchanges)
        self.assertEqual((result["lower_ns"], result["upper_ns"]), (43, 53))
        self.assertLessEqual(result["lower_ns"], 50)
        self.assertGreaterEqual(result["upper_ns"], 50)
        with self.assertRaises(ValueError):
            clock_bounds([])

    def test_quantile_is_nearest_rank(self):
        self.assertEqual(nearest_rank(list(range(1, 201)), .95), 190)
        with self.assertRaises(ValueError):
            nearest_rank([], .95)

    def test_validator_rejects_invented_latency(self):
        report = {
            "kind": "owned_window_visual_diagnostic", "status": "complete_diagnostic",
            "source_sha256": {"fixture.py": "0" * 64}, "configured_delays_ms": [0],
            "events_per_delay": 1,
            "samples": [{"event_id": "0-1", "status": "observed", "delay_ms": 0,
                         "injection": {"submit_ns": 10, "return_ns": 12, "accepted_events": 2},
                         "first_match": {"capture_after_ns": 30}, "capture_observed_latency_ns": 20,
                         "observation_bracket_ns": [0, 20],
                         "fixture": {"input_received_ns": 15, "paint_submitted_ns": 20}}],
            "summary": {"0": {"observed": 1, "median_ns": 20, "p95_ns": None, "p99_ns": None}}
        }
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "frames.jsonl").write_text(json.dumps(
                {"event_id": "0-1", "capture_before_ns": 25, "capture_after_ns": 30, "matched": True}) + "\n")
            (root / "summary.json").write_text(json.dumps(report))
            self.assertEqual(validate(root)["record_consistency"], "passed")
            report["samples"][0]["capture_observed_latency_ns"] = 1
            (root / "summary.json").write_text(json.dumps(report))
            with self.assertRaises(ValueError):
                validate(root)

    @unittest.skipUnless(os.name == "nt", "Windows ABI")
    def test_input_structure_matches_windows_abi(self):
        self.assertEqual(ctypes.sizeof(INPUT), 40 if ctypes.sizeof(ctypes.c_void_p) == 8 else 28)


if __name__ == "__main__":
    unittest.main()
