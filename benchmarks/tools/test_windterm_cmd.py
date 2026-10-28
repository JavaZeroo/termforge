"""Public diagnostic-boundary tests. Never attach to or launch WindTerm."""
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

from benchmarks.tools import windterm_cmd as cmd


RUN_ID = "0123456789abcdef0123456789abcdef"


class WorkloadTests(unittest.TestCase):
    def test_full_deterministic_ascii_oracle(self):
        lines, data = cmd.workload(RUN_ID)
        self.assertEqual(cmd.workload(RUN_ID), (lines, data))
        self.assertEqual(data.decode("ascii"), "\r\n".join(lines) + "\r\n")
        self.assertEqual(len(lines), 14)
        self.assertLess(max(map(len, lines)), 80)
        self.assertNotEqual(cmd.workload("f" * 32)[1], data)
        for value in (None, "", "../secret", "a" * 33, "A" * 32):
            with self.subTest(value=value), self.assertRaises(ValueError):
                cmd.workload(value)

    def test_missing_middle_prefix_extra_content_and_spaces_rejected(self):
        expected, _ = cmd.workload(RUN_ID)
        self.assertEqual(cmd.check_text({"lines": expected + ["", ""], "line_count": 16}, expected)["status"], "passed")
        for rows in ([expected[-1]], expected[1:], expected[:5] + expected[6:],
                     ["unexpected"] + expected, expected + ["unexpected"],
                     [expected[0] + " "] + expected[1:]):
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                cmd.check_text({"lines": rows, "line_count": len(rows)}, expected)
        for value in (len(expected) - 1, True, None):
            with self.assertRaises(ValueError):
                cmd.check_text({"lines": expected, "line_count": value}, expected)

    def test_prepare_rejects_cmd_expansion_before_creating_target(self):
        with patch.object(cmd, "prepare") as prepare:
            with self.assertRaisesRegex(ValueError, "cmd path"):
                cmd.prepare_run(Path("source"), Path("local/%USERNAME%"), RUN_ID)
            prepare.assert_not_called()

    def test_all_cmd_metacharacters_rejected_in_each_interpreted_path(self):
        for char in '&|<>^()%!"\r\n':
            for field in ("out", "runner", "executable"):
                with self.subTest(char=repr(char), field=field):
                    out = Path("local/out" + char + "echo.TF_CMD_REVIEW_MARKER") if field == "out" else Path("local/safe")
                    runner = str(Path("runner" + char + "probe.py")) if field == "runner" else cmd.__file__
                    executable = str(Path("python" + char + "probe.exe")) if field == "executable" else sys.executable
                    with patch.object(cmd, "__file__", runner), patch.object(cmd.sys, "executable", executable), \
                            patch.object(cmd, "prepare", side_effect=AssertionError("distribution preparation reached")) as prepare:
                        with self.assertRaisesRegex(ValueError, "cmd path"):
                            cmd.prepare_run(Path("source"), out, RUN_ID)
                        prepare.assert_not_called()

    @unittest.skipUnless(sys.platform == "win32", "Public CLI uses Windows-only run entry")
    def test_public_cli_rejects_ampersand_before_missing_source_or_output(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            out = root / "out&echo.TF_CMD_REVIEW_MARKER"
            # Existing Python stands in only for CLI path validation; no REA CLI,
            # cmd, WindTerm, desktop or provider may be launched after rejection.
            result = subprocess.run([sys.executable, "-B", "-X", "utf8", str(Path(cmd.__file__).resolve()),
                "run", "--source", str(root / "missing-distribution"), "--rea", sys.executable,
                "--out", str(out), "--seconds", "10"], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                timeout=10, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b"Unsupported cmd path characters", result.stderr)
            self.assertNotIn(b"FileNotFoundError", result.stderr)
            self.assertFalse(out.exists())
            self.assertEqual(list(root.iterdir()), [])

    def test_runtime_bound_rejected_before_preparation(self):
        with patch.object(cmd.sys, "platform", "win32"), patch.object(cmd, "prepare_run") as prepare:
            for value in (0, 9, 61, True):
                with self.assertRaisesRegex(ValueError, "10..60"):
                    cmd.run(Path("source"), Path("out"), Path("rea"), value)
            prepare.assert_not_called()


class EvidenceTests(unittest.TestCase):
    def fixture(self, out):
        lines, data = cmd.workload(RUN_ID)
        script = b"synthetic original probe"
        sha = hashlib.sha256(script).hexdigest()
        identity = {"schema": cmd.SCHEMA, "run_id": RUN_ID, "pid": 12345,
                    "sha256": cmd.EXPECTED, "files_sha256": cmd.EXPECTED_FILES,
                    "profile": "synthetic-local-only", "desktop": "owned-non-input",
                    "script_sha256": sha, "runner_sha256": cmd.digest(Path(cmd.__file__))}
        text = {"kind": "text_complete", "run_id": RUN_ID, "line_count": 14, "lines": lines,
                "screen_line_capacity": 24, "widget_font": {"family": "synthetic", "point_size": 9, "pixel_size": -1}, "widget_geometry": [0, 0, 99, 99]}
        evidence = {"ok": True, "data": {"evidence": {"operation": "instrument_with_frida",
                    "normalized_result": {"source_sha256": sha, "pid": 12345, "messages_truncated": False,
                                          "messages": [{"type": "send", "payload": text}]}}}}
        records = {"run.json": identity, "workload.json": {"run_id": RUN_ID, "lines": lines,
                   "payload_bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()},
                   "cleanup.json": {"run_id": RUN_ID, "target_pid": 12345, "active": 0, "total": 5},
                   "profile-check.json": {"only_synthetic_session": True, "session_count": 1},
                   "evidence.json": evidence,
                   "producer.json": {"run_id": RUN_ID, "sha256": hashlib.sha256(data).hexdigest(),
                    "payload_bytes": len(data), "status": "complete", "error": None,
                    "t0_counter_ns": 10, "producer_done_counter_ns": 20,
                    "consumer_done": None, "presented_done": None, "console": {"vt_enable_succeeded": True, "columns": 80, "rows": 24}}}
        for name, value in records.items():
            cmd.write_json(out / name, value)
        (out / "payload.bin").write_bytes(data)
        (out / "probe.js").write_bytes(script)
        return records

    def test_complete_text_passes_without_terminal_or_perf_endpoints(self):
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            self.fixture(out)
            result = cmd.summarize(out)
            self.assertEqual(result["correctness"]["status"], "passed")
            self.assertEqual(result["performance_gate"], "not_evaluated")
            self.assertIsNone(result["consumer_done"])
            self.assertIsNone(result["presented_done"])
            public = json.dumps(result)
            self.assertNotIn('"pid":', public)
            self.assertNotIn(folder, public)
            self.assertNotIn("counter_ns", public)

    def test_missing_producer_or_text_stays_unknown(self):
        for missing in ("producer.json", "evidence.json"):
            with self.subTest(missing=missing), tempfile.TemporaryDirectory() as folder:
                out = Path(folder)
                self.fixture(out)
                (out / missing).unlink()
                result = cmd.summarize(out)
                self.assertEqual(result["correctness"]["status"], "unknown")

    def test_identity_truncation_errors_wrong_run_and_marker_only_fail(self):
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            records = self.fixture(out)
            for mutation in ("pid", "hash", "run", "truncated", "script-error", "marker", "duplicate"):
                evidence = copy.deepcopy(records["evidence.json"])
                result = evidence["data"]["evidence"]["normalized_result"]
                if mutation == "pid": result["pid"] += 1
                elif mutation == "hash": result["source_sha256"] = "a" * 64
                elif mutation == "run": result["messages"][0]["payload"]["run_id"] = "f" * 32
                elif mutation == "truncated": result["messages_truncated"] = True
                elif mutation == "script-error": result["messages"].append({"type": "error"})
                elif mutation == "marker":
                    result["messages"][0]["payload"]["lines"] = ["TF-CMD-END " + RUN_ID]
                    result["messages"][0]["payload"]["line_count"] = 1
                elif mutation == "duplicate": result["messages"] *= 2
                (out / "evidence.json").write_text(json.dumps(evidence), encoding="utf-8")
                with self.subTest(mutation=mutation):
                    self.assertEqual(cmd.summarize(out)["correctness"]["status"], "unknown")

    def test_active_job_profile_scope_changed_or_source_drift_rejected(self):
        for record, value in (("cleanup.json", {"run_id": RUN_ID, "target_pid": 12345, "active": 1}),
                              ("cleanup.json", {"run_id": "f" * 32, "target_pid": 12345, "active": 0}),
                              ("profile-check.json", {"only_synthetic_session": False, "session_count": 2})):
            with tempfile.TemporaryDirectory() as folder:
                out = Path(folder)
                self.fixture(out)
                (out / record).write_text(json.dumps(value), encoding="utf-8")
                with self.assertRaises(ValueError): cmd.summarize(out)
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            records = self.fixture(out)
            records["run.json"]["runner_sha256"] = "f" * 64
            (out / "run.json").write_text(json.dumps(records["run.json"]), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "source changed"): cmd.summarize(out)

    def test_public_settings_drop_unlisted_fields_and_reject_path_or_nan(self):
        snapshot = {"screen_line_capacity": 24, "widget_geometry": [0, 0, 99, 99],
                    "widget_font": {"family": "Synthetic", "point_size": 9, "pixel_size": -1,
                                    "private_path": "should not be exported"}, "secret": "not public"}
        self.assertNotIn("private_path", json.dumps(cmd.terminal_settings(snapshot)))
        self.assertNotIn("secret", json.dumps(cmd.terminal_settings(snapshot)))
        for family, point in (("C:/private/font", 9), ("Synthetic", float("nan"))):
            altered = copy.deepcopy(snapshot)
            altered["widget_font"].update(family=family, point_size=point)
            with self.assertRaises(ValueError): cmd.terminal_settings(altered)


class CleanupTests(unittest.TestCase):
    def test_failed_target_stop_still_terminates_and_records_job(self):
        for signaled in (0, 258):
            with self.subTest(signaled=signaled), tempfile.TemporaryDirectory() as folder:
                out = Path(folder)
                kernel, job = Mock(), Mock()
                kernel.WaitForSingleObject.side_effect = [258, signaled]
                kernel.TerminateProcess.return_value = False
                job.drain.return_value = {"active": 0, "total": 1}
                info = Mock(process=123, pid=12345)
                with patch.object(cmd, "check_profile_scope") as check:
                    if signaled == 0:
                        cmd.cleanup_owned(kernel, info, job, out, RUN_ID)
                    else:
                        with self.assertRaisesRegex(OSError, "termination failed"):
                            cmd.cleanup_owned(kernel, info, job, out, RUN_ID)
                    check.assert_called_once_with(out)
                job.terminate.assert_called_once()
                row = json.loads((out / "cleanup.json").read_text())
                self.assertEqual(row["run_id"], RUN_ID)
                self.assertEqual(row["target_pid"], 12345)
                self.assertEqual(row["active"], 0)


if __name__ == "__main__":
    unittest.main()
