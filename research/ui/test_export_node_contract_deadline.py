"""Synthetic, in-memory tests of the public offline export boundary; no Node."""
from contextlib import ExitStack
import json
from pathlib import Path
import unittest
from unittest.mock import patch

from research.ui import export_node_contract_deadline as exporter


class ExportTests(unittest.TestCase):
    def setUp(self):
        self.root = Path.cwd() / "synthetic-node-export"
        self.files = {}
        self.plan_path = self.root / "authorized.json"
        self.log_path = self.root / "cli.json"
        self.exit_path = self.root / "exit.json"
        self.case_dir = self.root / "cases"
        source_hashes = {}
        for name in exporter.SOURCE_FILES:
            self.files[self.root / name] = ("synthetic source " + name).encode()
            source_hashes[name] = exporter.digest(self.files[self.root / name])
        self.files[self.root / "node"] = b"synthetic node EXE"
        self.files[self.root / "python"] = b"synthetic Python EXE"
        self.plan = {"source_base_commit": exporter.EXECUTED_SOURCE, "execution_authorized": True,
                     "maximum_attempts": 1, "cases": list(exporter.CASES), "execution_budget_seconds": 10,
                     "cleanup_budget_seconds": 5, "whole_lifecycle_bound_seconds": None,
                     "source_files_sha256": source_hashes, "node": str(self.root / "node"),
                     "python": str(self.root / "python"), "node_sha256": exporter.digest(b"synthetic node EXE"),
                     "python_sha256": exporter.digest(b"synthetic Python EXE"), "out": str(self.case_dir.resolve()),
                     "windows_version": "10.0.26200", "python_version": "3.12.10 synthetic"}
        self.put(self.plan_path, self.plan)
        self.plan_sha = exporter.digest(self.files[self.plan_path])
        self.files[self.plan_path.with_suffix(".attempted")] = b"One diagnostic attempt; retain after success or failure.\r\n"
        # Literal protocol witness, independent of the exporter's STAGES list.
        stages = ["node-first-checkpoint", "go-received", "fixture-before-load", "before-file-read",
                  "file-read", "vm-helpers-start", "vm-helpers-ready", "pure-checks-done"]
        for case in ("normal", "grab", "toImage", "empty", "dpr", "pixel", "destroy"):
            stages.extend(("case-start:" + case, "case-end:" + case))
        stages.append("final")
        self.batch = {"schema": "termforge-node-contract-batch-v1", "status": "expected_diagnostic_observations",
                      "original_timeout_root_cause": None, "whole_lifecycle_bound_seconds": None,
                      "windows_version": "10.0.26200", "python_version": "3.12.10 synthetic", "observations": []}
        for index, case in enumerate(exporter.CASES):
            events = [{"sequence": i, "stage": stage, "pid": 123, "monotonic_ns": str(100 + i)}
                      for i, stage in enumerate(stages[:(23, 6, 11)[index]])]
            events[0].update(node_version="v22.21.1", arch="x64", platform="win32")
            row = {"schema": "termforge-node-contract-observation-v1", "case": case,
                   "source_files_sha256": source_hashes, "node_sha256": self.plan["node_sha256"],
                   "windows_version": "10.0.26200", "python_version": "3.12.10 synthetic",
                   "execution_budget_seconds": 10, "cleanup_budget_seconds": 5, "process_created": True,
                   "root_reaped": True, "root_in_owned_job": True, "cleanup_verified": True, "root_pid": 123,
                   "trace": {"observations": events, "last_observed_stage": events[-1]["stage"], "complete": index == 0},
                   "stderr": "".join("TF_NODE_CONTRACT_V1:" + json.dumps(event) + "\n" for event in events),
                   "status": ("passed", "timed_out", "failed")[index], "exit_code": (0, 99, 1)[index],
                   "forced_job_termination": index == 1, "job_before_go": {"active": 1, "total": 1, "terminated": 0},
                   "job_after": {"active": 0, "total": 2, "terminated": 0},
                   "primary_exception": {"type": "TimeoutExpired", "timeout_seconds": 10} if index == 1 else None,
                   "stdout": "module paths, QRgb, ROI/ink gates,32B sret and normal-return destructor order passed\n" if index == 0 else "",
                   "process_creation_elapsed_seconds": 0.05, "communicate_elapsed_seconds": (0.1, 10.01, 0.06)[index]}
            if index == 1:
                row["job_at_timeout"] = {"active": 2, "total": 2, "terminated": 0}
            if index == 2:
                row["stderr"] += "TF_NODE_CONTROLLED_EXCEPTION\n"
            self.batch["observations"].append(row)
        self.put(self.exit_path, {"exit_code": 0})
        prior = {}
        for index in range(24):
            name = f"local/prior-{index}.json"
            self.files[self.root / name] = b"synthetic historical record"
            prior[name] = exporter.digest(self.files[self.root / name])
        self.put(self.root / "local/node-deadline-p3/prior-artifacts-before.json", prior)
        self.false_contracts = []
        for suffix, label, source in (("", "initial", "5ddecd4e0bd978d38a38974bb4d27b193b605fa7"),
                                      ("-p2", "protocol-fix", "0949ac6fc371e24ee20c25c36b6e90f372c99fe8"),
                                      ("-p3", "version-fix", exporter.EXECUTED_SOURCE)):
            path = self.root / f"local/node-144-offline-proposal{suffix}.json"
            self.put(path, {"execution_authorized": False, "source_base_commit": source,
                            "out": str(self.root / (label + "-absent-output"))})
            self.false_contracts.append((suffix, label, exporter.digest(self.files[path]), source))
        references = []
        for name in ("research/ui/raster_style_reference.js", "research/ui/test_raster_style_reference.py", "experiments/conpty/run.py"):
            data = self.files.setdefault(self.root / name, b"synthetic unchanged reference")
            self.files[self.root / "local/node-deadline-preflight/old-source" / name] = data
            references.append({"path": name, "git_blob_sha256": exporter.digest(data)})
        self.put(self.root / "local/node-deadline-preflight/old-source-identity.json",
                 {"original_head": "43883022883d382e3dd57bd627e9c735bafe58ca", "git_blob_archive": references})
        self.files[Path(exporter.__file__)] = b"synthetic exporter source"

    def put(self, path, value):
        self.files[path] = (json.dumps(value, indent=2) + "\n").encode()

    def export(self):
        self.put(self.log_path, self.batch)
        for index, row in enumerate(self.batch["observations"]):
            self.put(self.case_dir / f"{index}-{row['case']}.json", row)
        with ExitStack() as stack:
            stack.enter_context(patch.object(exporter, "ROOT", self.root))
            stack.enter_context(patch.object(exporter, "PLAN_SHA256", self.plan_sha))
            stack.enter_context(patch.object(exporter, "FALSE_PLANS", tuple(self.false_contracts), create=True))
            stack.enter_context(patch.object(Path, "read_bytes", lambda path: self.files[path]))
            stack.enter_context(patch.object(Path, "exists", lambda path: path in self.files))
            return json.loads(exporter.export(self.plan_path, self.log_path, self.exit_path, self.case_dir))

    def test_synthetic_valid_batch_exports_only_expected_diagnostic_results(self):
        result = self.export()
        self.assertEqual([row["status"] for row in result["observations"]], ["passed", "timed_out", "failed"])
        self.assertEqual([row["job_at_timeout"] for row in result["observations"]],
                         [None, {"active": 2, "total": 2, "terminated": 0}, None])
        self.assertNotIn("synthetic-node-export", json.dumps(result))

    def test_non_timeout_job_payload_is_rejected_even_with_matching_cli_and_case(self):
        for index in (0, 2):
            with self.subTest(case=exporter.CASES[index]):
                self.batch["observations"][index]["job_at_timeout"] = {
                    "pid": 987654, "private_path": "/private/TF_SYNTHETIC_NODE_SECRET"}
                with self.assertRaisesRegex(ValueError, "Unexpected timeout Job"):
                    self.export()
                del self.batch["observations"][index]["job_at_timeout"]

    def test_false_plan_source_or_other_byte_drift_is_rejected(self):
        path = self.root / "local/node-144-offline-proposal-p3.json"
        original = self.files[path]
        for change in ({"source_base_commit": "/private/TF_SYNTHETIC_FALSE_PLAN_ID"},
                       {"source_base_commit": "f" * 40}, {"extra": "synthetic-byte-drift"}):
            with self.subTest(change=change):
                self.put(path, {**json.loads(original), **change})
                with self.assertRaisesRegex(ValueError, "False proposal identity"):
                    self.export()
        self.files[path] = original


if __name__ == "__main__":
    unittest.main()
