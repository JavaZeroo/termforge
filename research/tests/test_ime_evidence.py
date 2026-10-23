"""Evidence retention tests; controlled children do not exercise or certify IME."""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import io
import json
import multiprocessing
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from research.ime import run_probe


def run_evidence(out: Path, marker: Path, sample: str, outcome: str, mode: str = "desktop") -> tuple[int, str]:
    """Run the actual collector, replacing only its native child with Python."""
    native_run = subprocess.run
    payload = {"schema": "termforge-ime-probe-v1", "sample": sample, "real_ime_workflow_verified": False}
    code = (
        "from pathlib import Path\nimport json, sys, time\n"
        f"Path({str(marker)!r}).write_text('executed', encoding='utf-8')\n"
    )
    if outcome == "timeout":
        code += "print('partial stdout before timeout', flush=True)\nprint('partial stderr before timeout', file=sys.stderr, flush=True)\ntime.sleep(10)\n"
    elif outcome == "raw":
        code += "sys.stdout.buffer.write(b'\\xff\\r\\n')\nsys.stderr.buffer.write(b'\\x00\\xfe\\r\\n')\n"
    else:
        code += f"print({json.dumps(payload)!r}, flush=True)\n"
        if outcome == "failure":
            code += "print('controlled failure', file=sys.stderr, flush=True)\nraise SystemExit(3)\n"

    def controlled_child(_command: list[str], **kwargs: object) -> subprocess.CompletedProcess[bytes]:
        return native_run([sys.executable, "-c", code], **kwargs)

    arguments = ["run_probe.py", "--exe", sys.executable, "--out", str(out), "--mode", mode, "--timeout", "1"]
    stdout, stderr = io.StringIO(), io.StringIO()
    with patch.object(sys, "argv", arguments), patch.object(run_probe.subprocess, "run", side_effect=controlled_child):
        with redirect_stdout(stdout), redirect_stderr(stderr):
            try:
                result = run_probe.main()
            except SystemExit as error:
                result = int(error.code)
    return result, stderr.getvalue()


def competing_run(out: str, marker: str, sample: str, barrier: object, queue: object) -> None:
    try:
        barrier.wait(timeout=8)
        queue.put((sample, *run_evidence(Path(out), Path(marker), sample, "failure")))
    except BaseException as error:
        queue.put((sample, -1, repr(error)))


class ImeEvidenceRetentionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="termforge-ime-evidence-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    @staticmethod
    def snapshot(directory: Path) -> dict[str, tuple[bytes, int]]:
        return {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in directory.iterdir()}

    def test_existing_destination_is_rejected_before_probe_starts(self) -> None:
        for populated in (False, True):
            with self.subTest(populated=populated):
                out = self.root / str(populated)
                out.mkdir()
                if populated:
                    (out / "result.json").write_bytes(b"original failed result\n")
                    (out / "stdout.jsonl").write_bytes(b"original stdout\n")
                    (out / "stderr.txt").write_bytes(b"original error\n")
                original = self.snapshot(out)
                marker = self.root / f"executed-{populated}"
                result, error = run_evidence(out, marker, "retry", "success")
                self.assertEqual(result, 2)
                self.assertIn("already exists", error)
                self.assertFalse(marker.exists(), "a rejected retry must not start another probe")
                self.assertEqual(self.snapshot(out), original)

    def test_failed_run_survives_retry_and_success_in_new_directory(self) -> None:
        failed = self.root / "failed"
        self.assertEqual(run_evidence(failed, self.root / "executed-first", "first", "failure")[0], 3)
        report = json.loads((failed / "result.json").read_text(encoding="utf-8"))
        self.assertEqual(report["returncode"], 3)
        self.assertEqual(report["probe"]["sample"], "first")
        self.assertEqual((failed / "stderr.txt").read_text(encoding="utf-8"), "controlled failure\n")
        original = self.snapshot(failed)
        marker = self.root / "executed-retry"
        self.assertEqual(run_evidence(failed, marker, "retry", "success")[0], 2)
        self.assertFalse(marker.exists())
        successful = self.root / "successful"
        self.assertEqual(run_evidence(successful, self.root / "executed-success", "success", "success")[0], 0)
        self.assertEqual(json.loads((successful / "result.json").read_text(encoding="utf-8"))["probe"]["sample"], "success")
        self.assertEqual(self.snapshot(failed), original)

    def test_timeout_keeps_partial_output_and_refuses_overwrite(self) -> None:
        out = self.root / "timeout"
        self.assertEqual(run_evidence(out, self.root / "executed-timeout", "timeout", "timeout")[0], 124)
        report = json.loads((out / "result.json").read_text(encoding="utf-8"))
        self.assertTrue(report["timed_out"])
        self.assertIsNone(report["returncode"])
        self.assertIn("partial stdout before timeout", (out / "stdout.jsonl").read_text(encoding="utf-8"))
        self.assertIn("partial stderr before timeout", (out / "stderr.txt").read_text(encoding="utf-8"))
        original = self.snapshot(out)
        self.assertEqual(run_evidence(out, self.root / "executed-after-timeout", "retry", "success")[0], 2)
        self.assertEqual(self.snapshot(out), original)

    def test_competing_processes_have_one_owner_and_keep_its_failure(self) -> None:
        context = multiprocessing.get_context("spawn")
        barrier, queue = context.Barrier(2), context.Queue()
        out = self.root / "competing"
        workers = [context.Process(target=competing_run, args=(str(out), str(self.root / f"executed-{name}"), name, barrier, queue))
                   for name in ("left", "right")]
        try:
            for worker in workers:
                worker.start()
            for worker in workers:
                worker.join(timeout=12)
                self.assertFalse(worker.is_alive(), "competing collector exceeded bounded deadline")
                self.assertEqual(worker.exitcode, 0)
            results = [queue.get(timeout=2), queue.get(timeout=2)]
        finally:
            for worker in workers:
                if worker.is_alive():
                    worker.terminate()
                    worker.join(timeout=5)
            queue.close()
            queue.join_thread()
        self.assertEqual(sorted(result[1] for result in results), [2, 3], results)
        winner = next(result[0] for result in results if result[1] == 3)
        self.assertEqual([path.name for path in self.root.glob("executed-*")], [f"executed-{winner}"])
        report = json.loads((out / "result.json").read_text(encoding="utf-8"))
        self.assertEqual(report["probe"]["sample"], winner)
        original = self.snapshot(out)
        self.assertEqual(run_evidence(out, self.root / "executed-later", "later", "success")[0], 2)
        self.assertEqual(self.snapshot(out), original)

    def test_zero_exit_without_real_ime_evidence_does_not_pass_hosted_gate(self) -> None:
        out = self.root / "hosted-incomplete"
        self.assertEqual(run_evidence(out, self.root / "executed-incomplete", "incomplete", "success", mode="hosted")[0], 3)
        report = json.loads((out / "result.json").read_text(encoding="utf-8"))
        self.assertFalse(report["real_ime_workflow_verified"])
        self.assertEqual(report["status"], "failed")

    def test_invalid_utf8_and_crlf_are_kept_as_original_stream_bytes(self) -> None:
        out = self.root / "raw-streams"
        self.assertEqual(run_evidence(out, self.root / "executed-raw", "raw", "raw")[0], 1)
        self.assertEqual((out / "stdout.jsonl").read_bytes(), b"\xff\r\n")
        self.assertEqual((out / "stderr.txt").read_bytes(), b"\x00\xfe\r\n")
        report = json.loads((out / "result.json").read_text(encoding="utf-8"))
        self.assertIsNotNone(report["parse_error"])
        self.assertEqual(report["status"], "failed")


if __name__ == "__main__":
    unittest.main()
