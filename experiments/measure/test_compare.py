"""Candidate measurement contracts and interleaved raw-sample gates; no speed assertions."""
import copy
import json
from pathlib import Path
import tempfile
import unittest

from experiments.measure.compare import CANDIDATE_STATE, WORKLOADS, candidate_binary, run_batch
from experiments.measure.run import execute
from experiments.candidates.alacritty.run import run_trace, trace_binary


def expected_candidate_projection(route):
    """Derive the fixed fixture's terminal state without replaying a parser.

    Each of 1,000 CR/LF-terminated rows fills fewer than 80 columns. The final
    screen has 23 completed rows and one empty row; the last 100 completed rows
    occupy history. Interactive editing changes abcdefgh to abcdeQgh. Neither
    fixture changes default style/modes, generates replies or overflows an EGC.
    """
    text = "x" * 76 if route == "project" else "abcdeQgh"
    style = dict(fg=-1, bg=-1, bold=False)

    def row(value):
        return [dict(text=value[c] if c < len(value) else "",
                     width=1 if c < len(value) else 0, style=style.copy())
                for c in range(80)]

    return dict(offset=0, mode_bits=163969, replies=[], grapheme_overflows=0,
                screen=dict(row=23, column=0, wrap_pending=False,
                            current_style=style, cells=[row(text) for _ in range(23)] + [row("")],
                            history=[row(text) for _ in range(100)]))


def projection_digest(value):
    # serde_json's default map is key-sorted and serializes compact UTF-8.
    result = 14695981039346656037
    for byte in json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(",", ":")).encode("utf-8"):
        result = ((result ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return str(result)


class CandidateProjectionIdentityTests(unittest.TestCase):
    def test_overflow_counter_migrates_the_full_projection_digest(self):
        legacy = dict(project="11966868740884721388", interactive="8035857386658397897")
        for route, workload in WORKLOADS.items():
            with self.subTest(route=route):
                expected = expected_candidate_projection(route)
                old = copy.deepcopy(expected)
                del old["grapheme_overflows"]
                self.assertEqual(projection_digest(old), legacy[route])
                self.assertEqual(projection_digest(expected), workload["states"]["alacritty"])
                # Keep the diagnostic field in the hash; do not exempt it to
                # preserve an obsolete identity or accept a corrupted counter.
                changed = copy.deepcopy(expected)
                changed["grapheme_overflows"] = 1
                self.assertNotEqual(projection_digest(changed), workload["states"]["alacritty"])

    def test_actual_fixture_projection_matches_independent_complete_state(self):
        candidate = trace_binary()
        with tempfile.TemporaryDirectory() as folder:
            for route, workload in WORKLOADS.items():
                with self.subTest(route=route):
                    payload = workload["fixture"]
                    report, records = run_trace(candidate, payload, 24, 80, 100, [len(payload)],
                                                Path(folder) / route)
                    self.assertTrue(report["passed"], report)
                    actual = records[-1]
                    self.assertEqual(actual["offset"], len(payload))
                    actual["offset"] = 0  # measure.rs snapshots at observation offset zero.
                    self.assertEqual(actual, expected_candidate_projection(route))
                    self.assertEqual(projection_digest(actual), workload["states"]["alacritty"])


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

    def test_interactive_observer_and_allocation_scope(self):
        workload = WORKLOADS["interactive"]
        for observe in (0, 1):
            expected = dict(kind="core_cost", route="interactive", chunk=16, observe=observe,
                            bytes_per_round=workload["bytes_per_round"],
                            state_fnv1a64=workload["states"]["alacritty"])
            record = execute([str(self.candidate), "interactive", "16", str(observe), ".1"], .1, expected)
            self.assertNotIn("error", record, record)
            result = record["result"]
            self.assertEqual(result["bytes_per_round"], 22000)
            self.assertEqual(result["state_fnv1a64"], workload["states"]["alacritty"])
            self.assertEqual(result["ordinary_allocations"] is not None, bool(observe))

    def test_interleaved_interactive_batch_keeps_raw_samples(self):
        with tempfile.TemporaryDirectory() as folder:
            from experiments.build_paths import binary
            output = Path(folder) / "batch"
            manifest, summary = run_batch(binary("core_measure"), self.candidate, output, .1, 1, 42,
                                          route="interactive")
            self.assertEqual(manifest["status"], "complete")
            self.assertEqual(manifest["samples"], 16)
            self.assertEqual(manifest["route"], "interactive")
            self.assertEqual(manifest["bytes_per_round"], 22000)
            self.assertEqual(len(summary["groups"]), 16)
            raw = (output / "samples.jsonl").read_text().splitlines()
            self.assertEqual(len(raw), 16)
            for line in raw:
                record = json.loads(line)
                self.assertNotIn("error", record, record)
                self.assertEqual(record["route"], "interactive")
                self.assertEqual(record["bytes_per_round"], 22000)
                self.assertEqual(record["result"]["state_fnv1a64"], record["state_fnv1a64"])
                self.assertGreater(record["cpu_ns_per_byte"], 0)
                self.assertGreater(record["wall_ns_per_byte"], 0)
            self.assertTrue(all(group["samples"] == 1 for group in summary["groups"]))
            self.assertEqual(summary["performance_gate"], "not_evaluated")

    def test_wrong_candidate_state_is_rejected(self):
        expected = dict(kind="core_cost", route="project", chunk=4096, observe=0, state_fnv1a64="wrong")
        record = execute([str(self.candidate), "project", "4096", "0", ".1"], .1, expected)
        self.assertIn("unexpected state digest", record["error"])

    def test_wrong_interactive_candidate_state_is_rejected(self):
        expected = dict(kind="core_cost", route="interactive", chunk=16, observe=0,
                        bytes_per_round=22000, state_fnv1a64="wrong")
        record = execute([str(self.candidate), "interactive", "16", "0", ".1"], .1, expected)
        self.assertIn("unexpected state digest", record["error"])


if __name__ == "__main__":
    unittest.main()
