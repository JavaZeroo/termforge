"""Timing contract and diagnostic-failure preservation; no speed assertions."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

from experiments.build_paths import binary
from experiments.measure.run import execute, run_command, validate_result
from experiments.measure import run


class MeasurementContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe = binary("core_measure")

    def sample(self, route, chunk, observe):
        expected = dict(kind="core_cost", route=route, chunk=chunk, observe=observe)
        record = execute([str(self.exe), route, str(chunk), str(observe), ".1"], .1, expected)
        self.assertNotIn("error", record, record)
        return record["result"], expected

    def test_chunked_parse_and_direct_state_have_matching_final_state(self):
        small, _ = self.sample("parse", 1, 0)
        large, expected = self.sample("parse", 65536, 1)
        state, _ = self.sample("state", 65536, 0)
        self.assertEqual(small["state_fnv1a64"], state["state_fnv1a64"])
        self.assertEqual(large["state_fnv1a64"], state["state_fnv1a64"])
        self.assertEqual(large["wall_gate_polls"] * 8, large["rounds"])
        # These corruptions used to be invisible to a producer-complete gate.
        for field, value in (("state_fnv1a64", "wrong"), ("schema_version", 1),
                             ("interval_cpu_seconds", .01), ("interval_wall_seconds", float("nan")),
                             ("counted_parser_destructions", large["rounds"]),
                             ("presented_done", 0), ("chunk", 1)):
            broken = copy.deepcopy(large)
            broken[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_result(broken, .1, expected)

    def test_calibration_checks_counts_and_preserves_order(self):
        for first in (0, 1):
            expected = dict(kind="calibration", observe_first=first)
            record = execute([str(self.exe), "calibrate", ".1", str(first)], .1, expected)
            self.assertNotIn("error", record, record)
            result = record["result"]
            self.assertFalse(result["calibration_is_subtracted"])
            broken = copy.deepcopy(result)
            counted = next(item for item in broken["allocation_loop"] if item["observe"])
            counted["ordinary_allocations"] -= 1
            with self.assertRaises(ValueError):
                validate_result(broken, .1, expected)

    def test_invalid_arguments_fail_before_measurement(self):
        cases = [("parse", "0", "0", ".1"), ("parse", "512", "2", ".1"),
                 ("parse", "512bad", "0", ".1"), ("parse", "512", "0", "nan"),
                 ("parse", "512", "0", ".1suffix"), ("calibrate", ".09"),
                 ("calibrate", ".1", "-1")]
        for arguments in cases:
            with self.subTest(arguments=arguments):
                record = run_command([str(self.exe), *arguments])
                self.assertEqual(record["exit_code"], 2)
                self.assertFalse(record["timed_out"])
                self.assertEqual(record["stdout"], "")
                self.assertTrue(record["stderr"])


class FailureEvidence(unittest.TestCase):
    def test_bad_json_retains_both_raw_streams(self):
        record = execute([sys.executable, "-c", "import sys; print('not-json'); print('detail', file=sys.stderr)"],
                         .1, dict(kind="calibration", observe_first=0))
        self.assertIn("error", record)
        self.assertEqual(record["stdout"].strip(), "not-json")
        self.assertEqual(record["stderr"].strip(), "detail")

    def test_nonzero_exit_retains_diagnostic_json(self):
        record = execute([sys.executable, "-c", "import sys; print('{\"partial\":1}'); print('failed', file=sys.stderr); sys.exit(7)"],
                         .1, dict(kind="calibration", observe_first=0))
        self.assertEqual(record["exit_code"], 7)
        self.assertEqual(json.loads(record["stdout"]), {"partial": 1})
        self.assertEqual(record["stderr"].strip(), "failed")
        self.assertIn("error", record)

    def test_wrong_json_shape_retains_original_response(self):
        record = execute([sys.executable, "-c", "print('[1, 2]')"],
                         .1, dict(kind="calibration", observe_first=0))
        self.assertIn("error", record)
        self.assertEqual(json.loads(record["stdout"]), [1, 2])

    def test_timeout_retains_partial_output(self):
        script = "import sys,time; print('partial',flush=True); print('waiting',file=sys.stderr,flush=True); time.sleep(10)"
        record = run_command([sys.executable, "-c", script], timeout=1)
        self.assertTrue(record["timed_out"])
        self.assertIsNone(record["exit_code"])
        self.assertEqual(record["stdout"].strip(), "partial")
        self.assertEqual(record["stderr"].strip(), "waiting")

    def test_missing_executable_is_recorded(self):
        record = run_command(["termforge-deliberately-missing-executable-34624"])
        self.assertIsNone(record["exit_code"])
        self.assertIn("error", record)

    def test_failed_batch_keeps_manifest_and_diagnostic_record(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "batch"
            # A real executable that rejects the probe CLI before any workload.
            with patch.object(run, "binary", return_value=Path(sys.executable)), patch.object(
                sys, "argv", ["run.py", "--out", str(output), "--cpu-seconds", ".1", "--repeats", "1"]
            ), self.assertRaises(RuntimeError):
                run.main()
            manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
            record = json.loads((output / "calibration.jsonl").read_text(encoding="utf-8"))
            self.assertEqual(manifest["status"], "failed")
            self.assertIn("finished_utc", manifest)
            self.assertNotEqual(record["exit_code"], 0)
            self.assertTrue(record["stderr"])
            self.assertIn("stdout", record)


if __name__ == "__main__":
    unittest.main()
