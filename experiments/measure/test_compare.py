"""Candidate measurement contracts and interleaved raw-sample gates; no speed assertions."""
import json
from pathlib import Path
import tempfile
import unittest

from experiments.measure.compare import CANDIDATE_STATE, candidate_binary, run_batch
from experiments.measure.run import execute


class CandidateMeasurementTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            cls.candidate = candidate_binary()
        except FileNotFoundError:
            raise unittest.SkipTest("optional alacritty-measure binary is not built")

    def test_projection_observer_and_allocation_scope(self):
        for observe in (0, 1):
            expected = dict(kind="core_cost", route="project", chunk=4096, observe=observe,
                            state_fnv1a64=CANDIDATE_STATE)
            record = execute([str(self.candidate), "project", "4096", str(observe), ".1"], .1, expected)
            self.assertNotIn("error", record, record)
            result = record["result"]
            self.assertEqual(result["state_fnv1a64"], CANDIDATE_STATE)
            self.assertEqual(result["ordinary_allocations"] is not None, bool(observe))
            self.assertIn("not equivalent to C++ new", result["allocation_scope"])

    def test_interleaved_batch_keeps_raw_samples(self):
        with tempfile.TemporaryDirectory() as folder:
            from experiments.build_paths import binary
            manifest, summary = run_batch(binary("core_measure"), self.candidate, Path(folder) / "batch", .1, 1, 42)
            self.assertEqual(manifest["status"], "complete")
            self.assertEqual(manifest["samples"], 12)
            self.assertEqual(len(summary["groups"]), 12)
            raw = (Path(folder) / "batch/samples.jsonl").read_text().splitlines()
            self.assertEqual(len(raw), 12)
            for line in raw:
                record = json.loads(line)
                self.assertNotIn("error", record, record)
                self.assertGreater(record["cpu_ns_per_byte"], 0)
                self.assertGreater(record["wall_ns_per_byte"], 0)
            self.assertTrue(all(group["samples"] == 1 for group in summary["groups"]))
            self.assertEqual(summary["performance_gate"], "not_evaluated")

    def test_wrong_candidate_state_is_rejected(self):
        expected = dict(kind="core_cost", route="project", chunk=4096, observe=0, state_fnv1a64="wrong")
        record = execute([str(self.candidate), "project", "4096", "0", ".1"], .1, expected)
        self.assertIn("unexpected state digest", record["error"])


if __name__ == "__main__":
    unittest.main()
