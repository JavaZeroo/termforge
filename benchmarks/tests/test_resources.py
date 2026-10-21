import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from test_tools import load

summary = load("summarize_resources")


def process(pid=1, group="root", cpu=0, base=0, memory=10, created=100):
    return {"pid": pid, "create_time": created, "group": group, "status": "observed",
            "cpu_seconds_lifetime": cpu, "cpu_seconds_since_first_observation": cpu - base,
            "private_bytes": memory, "working_set_bytes": memory * 2, "handles": 1}


def fixture():
    return [
        {"type": "metadata", "schema_version": "0.1", "root_pid": 1,
         "root_create_time": 100, "clock": "perf_counter_ns", "start_counter_ns": 100,
         "requested_interval_ns": 1000000000, "requested_discovery_interval_ns": 1000000000},
        {"type": "sample", "elapsed_ns": 0, "actual_interval_ns": None,
         "collection_ns": 10, "schedule_lateness_ns": 0, "errors": [],
         "processes": [process(), process(2, "descendant", memory=100), process(9, "sampler")]},
        {"type": "sample", "elapsed_ns": 1000000000, "actual_interval_ns": 1000000000,
         "collection_ns": 20, "schedule_lateness_ns": 1, "errors": [],
         "processes": [process(cpu=.5), process(2, "descendant", cpu=.25, memory=1),
                       process(3, "descendant", memory=100), process(9, "sampler", cpu=.1)]},
        {"type": "sample", "elapsed_ns": 2000000000, "actual_interval_ns": 1000000000,
         "collection_ns": 12, "schedule_lateness_ns": 0, "errors": [],
         "processes": [process(cpu=1),
                       {"pid": 2, "create_time": 100, "group": "descendant", "status": "NoSuchProcess"},
                       process(3, "descendant", cpu=.5, memory=1), process(9, "sampler", cpu=.2)]},
        {"type": "end", "elapsed_ns": 2100000000, "status": "complete"}]


class ResourceSummary(unittest.TestCase):
    def report(self, rows):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "trace.jsonl"
            path.write_text("".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8")
            result = summary.summarize(path)
            self.assertEqual(result["source_sha256"], hashlib.sha256(path.read_bytes()).hexdigest())
            return result

    def test_exit_cpu_retained_and_peak_is_concurrent_sum(self):
        report = self.report(fixture())
        descendants = report["groups"]["descendant"]
        self.assertEqual(descendants["observed_cpu_seconds"], .75)
        self.assertEqual(descendants["observed_cpu_core_equivalents"], .375)
        self.assertEqual(descendants["sampled_peak_observed_private_bytes"], 101)
        self.assertEqual(descendants["unavailable_rows"], 1)
        self.assertEqual(report["quality"]["samples_with_unavailable_processes"], 1)
        self.assertEqual(report["performance_gate"], "not_evaluated")

    def test_pid_reuse_starts_new_cpu_baseline(self):
        rows = fixture()
        rows[3]["processes"][1] = process(2, "descendant", cpu=7, base=7, created=101)
        report = self.report(rows)["groups"]["descendant"]
        self.assertEqual(report["observed_processes"], 3)
        self.assertEqual(report["observed_cpu_seconds"], .75)

    def test_unknown_is_not_zero_and_one_sample_has_no_rate(self):
        rows = fixture()
        rows = [rows[0], rows[1], rows[-1]]
        rows[1]["processes"] = [process(9, "sampler"),
                               {"pid": 1, "create_time": 100, "group": "root", "status": "AccessDenied"}]
        report = self.report(rows)
        for group in ("root", "descendant"):
            self.assertIsNone(report["groups"][group]["observed_cpu_seconds"])
            self.assertIsNone(report["groups"][group]["sampled_peak_observed_private_bytes"])
        self.assertIsNone(report["groups"]["sampler"]["observed_cpu_core_equivalents"])

    def test_gaps_and_discovery_failures_remain_visible(self):
        rows = fixture()
        rows[2]["errors"] = [{"pid": 1, "operation": "children", "error": "AccessDenied"}]
        rows.insert(3, {"type": "schedule_gap", "skipped_ticks": 2})
        quality = self.report(rows)["quality"]
        self.assertEqual(quality["skipped_ticks"], 2)
        self.assertEqual(quality["discovery_errors"], 1)
        self.assertEqual(quality["collection_ns_max"], 20)

    def test_corrupt_records_rejected(self):
        original = fixture()
        changes = [
            lambda r: r.pop(),
            lambda r: r.append(r[1]),
            lambda r: r[0].update(schema_version="future"),
            lambda r: r[2].update(elapsed_ns=0),
            lambda r: r[2].update(actual_interval_ns=123),
            lambda r: r[2]["processes"].append(copy.deepcopy(r[2]["processes"][0])),
            lambda r: r[2]["processes"][0].update(cpu_seconds_lifetime=float("nan")),
            lambda r: r[2]["processes"][0].update(cpu_seconds_since_first_observation=2),
            lambda r: r[2]["processes"][0].update(private_bytes=-1),
            lambda r: r[2]["processes"][0].update(pid=99),
            lambda r: r[3]["processes"][0].update(cpu_seconds_lifetime=.1),
            lambda r: r[1]["processes"].pop(),
        ]
        for index, change in enumerate(changes):
            with self.subTest(index=index):
                rows = copy.deepcopy(original)
                change(rows)
                with self.assertRaises(ValueError):
                    self.report(rows)


if __name__ == "__main__":
    unittest.main()
