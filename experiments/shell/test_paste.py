"""Separate transport and real system-clipboard gates; never use a user's clipboard.

Local: private window station only, with a recorded unsupported result if Windows
denies access. Hosted CI: additionally require the real clipboard API on the
disposable GitHub-hosted VM. Both transport and clipboard must pass there.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import tempfile
import unittest

from experiments.build_paths import ROOT, binary
from experiments.conpty.run import OwnedJob


def validate_transport(rows, directory):
    mode_rows = [row for row in rows if "mode" in row]
    by_mode = {row["mode"]: row for row in mode_rows}
    if len(mode_rows) != 5 or set(by_mode) != {"complete", "cancel", "close", "target-failure", "unavailable"}:
        raise ValueError("missing or unexpected transport result")
    summaries = {}
    for name in ("complete", "cancel", "close", "target-failure"):
        row = by_mode[name]
        if any(type(row.get(key)) is not int for key in ("total", "accepted", "sent", "received", "active_children_after")):
            raise ValueError("invalid transport counters")
        if not 0 <= row["received"] <= row["sent"] <= row["accepted"] <= row["total"]:
            raise ValueError("invalid accepted/sent/consumed ordering")
        if row["active_children_after"] != 0 or row.get("prefix_exact") is not True:
            raise ValueError("child cleanup or prefix gate failed")
        expected = (("A汉e\u0301🙂\t \r" * (20000 if name == "complete" else 100000)).encode("utf-8")
                    if name in ("complete", "cancel") else "汉".encode("utf-8") * 1000000)
        received = (directory / (name + ".bin")).read_bytes()
        if row["total"] != len(expected) or row["received"] != len(received) or received != expected[:len(received)]:
            raise ValueError("independent UTF-8 prefix oracle failed")
        if name in ("complete", "cancel"):
            if not (16384 < row["sent"] == row["accepted"] == len(received)
                    and row["messages"] > 0 and 0 < row["queue_high_water"] <= 16384):
                raise ValueError("bounded transfer or message pump gate failed")
            if (name == "complete") != (received == expected):
                raise ValueError("completion/cancellation boundary mismatch")
            if (directory / (name + "-other.bin")).read_bytes() != b"OTHER":
                raise ValueError("paste leaked into another session")
        else:
            if row["accepted"] >= row["total"] or row.get("retained_capacity", 100) > 32 or row.get("state") not in ("cancelled", "failed"):
                raise ValueError("closed transfer did not release bounded payload")
        summaries[name] = {"bytes": len(received), "sha256": hashlib.sha256(received).hexdigest()}
    failure = by_mode["unavailable"]
    if failure != {"mode": "unavailable", "accepted": 1024, "sent": 0, "state": "failed", "active_children_after": 0}:
        raise ValueError("actual WriteFile failure accounting mismatch")
    return summaries


def validate_clipboard(rows, directory):
    expected = "Unicode 汉字 e\u0301 🙂\r\nspace  \t".encode("utf-16-le")
    if (directory / "clipboard-written.utf16le").read_bytes() != expected + b"\0\0":
        raise ValueError("independent system clipboard write oracle failed")
    if (directory / "clipboard-read.utf16le").read_bytes() != expected:
        raise ValueError("independent system clipboard read oracle failed")
    if (directory / "clipboard-worker.utf16le").read_bytes() != "Reader 汉字 🙂 e\u0301\r\n".encode("utf-16-le"):
        raise ValueError("helper framed UTF16 oracle failed")
    cases = [row for row in rows if "clipboard_cases" in row]
    expected_cases = dict.fromkeys(("independent_write", "independent_read", "occupied_read", "occupied_write",
                                   "invalid_utf16", "empty", "oversize_allocation", "text_limit", "unterminated", "missing_format"), True)
    expected_cases["clipboard_cases"] = "passed"
    if cases != [expected_cases]:
        raise ValueError("missing system clipboard case")
    reader = {row["clipboard_reader"]: row for row in rows if "clipboard_reader" in row}
    if set(reader) != {"cancelled", "timed_out", "destroyed", "early-eof", "passed"}:
        raise ValueError("clipboard reader cancellation/timeout/EOF/destruction gate incomplete")
    if any(row.get("cleanup") is not True for row in reader.values()):
        raise ValueError("clipboard helper resource cleanup failed")
    for name in ("cancelled", "timed_out"):
        if reader[name].get("delayed_renderer_entered") is not True or reader[name].get("messages", 0) <= 0:
            raise ValueError("blocked renderer did not leave the UI processing messages")


def execute(executable: Path, mode: str, directory: Path):
    process = None
    record = {"mode": mode, "platform": platform.platform(), "exe_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
              "helper_sha256": hashlib.sha256(executable.with_name("shell_clipboard_reader.exe").read_bytes()).hexdigest(),
              "github_sha": os.environ.get("GITHUB_SHA"),
              "timed_out": False}
    job = OwnedJob()
    try:
        process = subprocess.Popen([str(executable), "--supervised", mode, str(directory)],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        job.assign(process.pid)
        try:
            stdout, stderr = process.communicate(b"GO\n", timeout=50)
        except subprocess.TimeoutExpired:
            record["timed_out"] = True
            job.terminate()
            stdout, stderr = process.communicate(timeout=10)
        record.update(exit_code=process.returncode, stdout=stdout.decode("utf-8", "replace"),
                      stderr=stderr.decode("utf-8", "replace"), job_after=job.drain())
        record["rows"] = [json.loads(line) for line in record["stdout"].splitlines() if line.startswith("{")]
        if record["job_after"]["active"]:
            raise ValueError("paste probe leaked an owned process")
        return record
    finally:
        if process and process.poll() is None:
            job.terminate()
            # If assignment itself failed, the gated host is not yet in Job.
            # This process handle is ours and cannot have spawned any children.
            process.kill(); process.communicate(timeout=10)
        job.close()
        directory.parent.mkdir(parents=True, exist_ok=True)
        directory.with_suffix(".json").write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding="utf-8")


@unittest.skipUnless(os.name == "nt", "real clipboard/ConPTY requires Windows")
class PasteTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.executable = binary("shell_paste_probe")
        parent = Path(os.environ.get("TERMFORGE_TEST_ARTIFACTS", ROOT / "build/test-artifacts")) / "paste"
        parent.mkdir(parents=True, exist_ok=True)
        cls.out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))

    def test_transport_order_cancel_close_and_isolation(self):
        for repeat in range(3):
            directory = self.out / f"transport-{repeat}"
            record = execute(self.executable, "--transport", directory)
            self.assertEqual(record["exit_code"], 0, record)
            self.assertFalse(record["timed_out"], record)
            self.assertEqual(record["rows"][-1], {"clipboard": "not-run", "private_station": True, "transport": "passed", "cleanup": True})
            hashes = validate_transport(record["rows"], directory)
            (directory / "hashes.json").write_text(json.dumps(hashes, indent=2), encoding="utf-8")

    def test_real_system_clipboard(self):
        directory = self.out / "private-clipboard"
        record = execute(self.executable, "--isolated", directory)
        hosted = os.environ.get("GITHUB_ACTIONS") == "true" and os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted"
        if record["exit_code"]:
            denied = record["rows"] == [{"clipboard": "failed", "win32_error": 5, "private_station": True, "cleanup": True}]
            self.assertTrue(denied, record)
            if not hosted:
                if os.environ.get("GITHUB_ACTIONS") == "true" or os.environ.get("TERMFORGE_REQUIRE_SYSTEM_CLIPBOARD") == "1":
                    self.fail(f"Required real system clipboard gate cannot use the current environment: {record}")
                self.skipTest(f"Private window station OpenClipboard denied (5); system API gate remains unverified. Evidence: {directory.with_suffix('.json')}")
            directory = self.out / "hosted-clipboard"
            record = execute(self.executable, "--hosted-ci", directory)
        self.assertEqual(record["exit_code"], 0, record)
        self.assertFalse(record["timed_out"], record)
        self.assertEqual(record["rows"][-1]["clipboard"], "passed")
        self.assertTrue(record["rows"][-1]["cleanup"])
        validate_clipboard(record["rows"], directory)
        hashes = validate_transport(record["rows"], directory)
        (directory / "hashes.json").write_text(json.dumps(hashes, indent=2), encoding="utf-8")

    def test_hosted_fallback_cannot_run_on_user_desktop(self):
        if os.environ.get("GITHUB_ACTIONS") == "true" and os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted":
            self.skipTest("hosted VM is the explicitly allowed environment")
        record = execute(self.executable, "--hosted-ci", self.out / "forbidden-fallback")
        self.assertNotEqual(record["exit_code"], 0)
        self.assertIn("restricted to a disposable GitHub-hosted VM", record["stderr"])
        self.assertEqual(record["rows"], [])


if __name__ == "__main__":
    unittest.main()
