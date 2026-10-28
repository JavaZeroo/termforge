"""Public trace/launcher boundaries for the bounded Node diagnostic."""
import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest

from research.ui.node_contract_deadline import (
    ROOT, diagnose_node, prepare_plan, read_checkpoints, run_plan,
)


def trace(rows):
    return b"".join(b"TF_NODE_CONTRACT_V1:" + json.dumps(row).encode() + b"\n" for row in rows)


class CheckpointTests(unittest.TestCase):
    def test_partial_prefix_preserves_last_observed_stage(self):
        rows = [
            {"sequence": 0, "stage": "node-first-checkpoint", "pid": 123,
             "monotonic_ns": "100", "node_version": "v0.synthetic", "arch": "x64", "platform": "win32"},
            {"sequence": 1, "stage": "go-received", "pid": 123, "monotonic_ns": "110"},
        ]
        result = read_checkpoints(trace(rows), 123)
        self.assertEqual(result["last_observed_stage"], "go-received")
        self.assertFalse(result["complete"])
        self.assertEqual(result["observations"], rows)
        self.assertEqual(read_checkpoints(b"", 123)["last_observed_stage"], None)

    def test_sequence_pid_clock_and_unknown_stage_are_rejected(self):
        first = {"sequence": 0, "stage": "node-first-checkpoint", "pid": 123,
                 "monotonic_ns": "100", "node_version": "v0.synthetic", "arch": "x64", "platform": "win32"}
        for change in ({"sequence": 1}, {"pid": 124}, {"stage": "private-path"},
                       {"monotonic_ns": "-1"}, {"node_version": 1}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                read_checkpoints(trace([{**first, **change}]), 123)
        second = {"sequence": 1, "stage": "go-received", "pid": 123, "monotonic_ns": "99"}
        with self.assertRaises(ValueError):
            read_checkpoints(trace([first, second]), 123)

    def test_truncated_json_and_fake_final_cannot_claim_complete(self):
        with self.assertRaises(ValueError):
            read_checkpoints(b'TF_NODE_CONTRACT_V1:{"sequence":0\n', 123)
        with self.assertRaises(ValueError):
            read_checkpoints(trace([{"sequence": 0, "stage": "final", "pid": 123,
                                     "monotonic_ns": "1"}]), 123)


class PlanTests(unittest.TestCase):
    def temporary(self):
        (ROOT / "local").mkdir(exist_ok=True)
        return tempfile.TemporaryDirectory(prefix="node-preflight-", dir=ROOT / "local")

    def test_false_plan_refuses_launch_and_retains_no_attempt_marker(self):
        with self.temporary() as temporary:
            plan = Path(temporary) / "plan.json"
            prepare_plan(plan, Path(temporary) / "result")
            with self.assertRaisesRegex(ValueError, "offline proposal"):
                run_plan(plan)
            self.assertFalse(plan.with_suffix(".attempted").exists())
            self.assertFalse((Path(temporary) / "result").exists())

    def test_source_identity_change_is_rejected_before_launch(self):
        with self.temporary() as temporary:
            plan = Path(temporary) / "plan.json"
            prepare_plan(plan, Path(temporary) / "result")
            data = json.loads(plan.read_text(encoding="utf-8"))
            data["execution_authorized"] = True
            data["source_files_sha256"]["research/ui/node_contract_bootstrap.js"] = "0" * 64
            plan.write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "source identity"):
                run_plan(plan)
            self.assertFalse(plan.with_suffix(".attempted").exists())


@unittest.skipUnless(os.name == "nt" and shutil.which("node"), "Windows and Node are required")
class OwnedNodeTests(unittest.TestCase):
    def check_case(self, case):
        result = diagnose_node(Path(shutil.which("node")), case)
        # These synthetic test observations remain separate from any frozen batch.
        print("TF_NODE_PREFLIGHT_TEST_RESULT:" + json.dumps(result, sort_keys=True))
        self.assertTrue(result["cleanup_verified"], result)
        self.assertTrue(result["root_reaped"], result)
        self.assertTrue(result["root_in_owned_job"], result)
        self.assertEqual(result["job_after"]["active"], 0)
        return result

    def test_normal_retains_all_cases_and_natural_success(self):
        result = self.check_case("normal")
        self.assertEqual(result["status"], "passed", result)
        self.assertEqual(result["exit_code"], 0)
        self.assertFalse(result["forced_job_termination"])
        self.assertEqual(result["trace"]["last_observed_stage"], "final")
        starts = [row["stage"] for row in result["trace"]["observations"] if row["stage"].startswith("case-start:")]
        self.assertEqual(starts, ["case-start:" + case for case in ("normal", "grab", "toImage", "empty", "dpr", "pixel", "destroy")])

    def test_controlled_hang_keeps_ten_second_failure_and_prefix(self):
        result = self.check_case("hang-vm-helpers")
        self.assertEqual(result["status"], "timed_out", result)
        self.assertEqual(result["execution_budget_seconds"], 10)
        self.assertEqual(result["primary_exception"]["type"], "TimeoutExpired")
        self.assertEqual(result["trace"]["last_observed_stage"], "vm-helpers-start")
        self.assertFalse(result["trace"]["complete"])
        self.assertTrue(result["forced_job_termination"])
        self.assertGreaterEqual(result["job_at_timeout"]["active"], 1)
        self.assertGreaterEqual(result["communicate_elapsed_seconds"], 9)

    def test_controlled_exception_is_failed_despite_clean_job(self):
        result = self.check_case("throw-before-grab")
        self.assertEqual(result["status"], "failed", result)
        self.assertNotEqual(result["exit_code"], 0)
        self.assertFalse(result["forced_job_termination"])
        self.assertEqual(result["trace"]["last_observed_stage"], "case-start:grab")
        self.assertIn("TF_NODE_CONTROLLED_EXCEPTION", result["stderr"])
        self.assertFalse(result["trace"]["complete"])


if __name__ == "__main__":
    unittest.main()
