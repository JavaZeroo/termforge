"""Evidence rejection and host boundary tests, independent of a vendor runtime."""
import ctypes
import json
import os
import tempfile
import sys
import hashlib
import socket
from pathlib import Path
import unittest
import uuid
from unittest.mock import patch

from research.ui import r1_launch as launch
from research.ui import r1_summarize as summary


def observation():
    phases = ("session", "select-all", "find-open", "find-typed", "find-needle-enter", "find-f3",
              "find-f3-wrap", "find-shift-f3", "find-escape", "history-home", "history-after-output",
              "history-end", "shift-page-up", "shift-page-down", "doubleclick-word", "tripleclick-line",
              "before-typing", "after-typing", "select-word-api", "select-line-api", "find-unicode",
              "drag-unicode", "reverse-drag-unicode")
    snapshots = []
    for phase in phases:
        state = {"scroll": 0, "lines": 88, "capacity": 23, "caret": 0, "anchor": 0, "selection": ""}
        if phase in ("history-after-output", "history-end", "before-typing", "after-typing"):
            state["lines"] = 108
        if phase in ("history-end", "after-typing"):
            state["scroll"] = 85
        if phase in ("find-typed", "find-needle-enter", "find-f3", "find-f3-wrap", "find-shift-f3"):
            line = 78 if phase == "find-needle-enter" else (0 if phase == "find-f3-wrap" else 79)
            state.update(selection="needle", anchor=line * 34 + 21)
        selections = {"find-unicode": "中文", "doubleclick-word": "TF-HISTORY-000",
                      "tripleclick-line": "TF-HISTORY-000 alpha needle omega",
                      "select-line-api": "TF-HISTORY-000 alpha needle omega\n",
                      "drag-unicode": summary.SELECTION, "reverse-drag-unicode": summary.SELECTION}
        state["selection"] = selections.get(phase, state["selection"])
        snapshots.append({"type": "send", "payload": {"kind": "snapshot", "phase": phase,
            "text_state": json.dumps(state), "focus": "ComboBox" if phase == "find-open" else "ScintillaEditBase",
            "peer": {"received": "x", "appended": True}}})
    snapshots.append({"type": "send", "payload": {"kind": "scenario_complete", "coverage": "navigation-selection-only"}})
    identity = {"sha256": summary.EXPECTED_EXE, "files_sha256": summary.EXPECTED_FILES,
                "profile": "synthetic-local-only", "clipboard": "owned-window-station", "pid": 101, "script_sha256": "script"}
    evidence = {"ok": True, "data": {"evidence": {"operation": "instrument_with_frida", "evidence_id": "synthetic-test",
                "normalized_result": {"source_sha256": "script", "pid": 101, "messages": snapshots}}}}
    return [identity, evidence, {"active": 0}, {"only_synthetic_session": True, "session_count": 1}, "script"]


class EvidenceBoundaryTests(unittest.TestCase):
    def test_public_menu_allowlist_keeps_only_r1_entries(self):
        rows = [{"text": text, "enabled": True} for text in ("全选(&A)\tCtrl+Shift+A", "查找...(&F)\tCtrl+Shift+F", "复制(&C)\tShift+Del", "粘贴(&P)\tShift+Ins", "传输二进制(&T)")]
        self.assertEqual(len(summary.r1_menu_actions(rows)), 4)
        self.assertNotIn("传输", json.dumps(summary.r1_menu_actions(rows), ensure_ascii=False))

    @unittest.skipUnless(os.name == "nt", "Windows exclusive loopback socket")
    def test_owned_telnet_peer_accepts_fragmented_naws_and_closes(self):
        from research.ui.r1_vt_fixture import Peer
        with tempfile.TemporaryDirectory() as directory:
            peer = Peer(Path(directory))
            try:
                with socket.create_connection(("127.0.0.1", peer.port), timeout=2) as client:
                    client.settimeout(2)
                    negotiation = b""
                    while len(negotiation) < 9:
                        negotiation += client.recv(9 - len(negotiation))
                    self.assertTrue(negotiation.startswith(bytes([255, 251, 1])))
                    for part in (bytes([255, 251]), bytes([31, 255, 250, 31, 0]), bytes([80, 0, 25, 255]), bytes([240])):
                        client.sendall(part)
                    received = b""
                    while b"TF-VT-END\r\n" not in received and len(received) < 4096:
                        received += client.recv(4096)
                    self.assertIn(b"LEFTRIGHT:END\r\n", received)
                    self.assertIn(b"LEFT\r\nRIGHT:END\r\n", received)
                    self.assertIn(b"TF-TAIL:two  \r\n", received)
            finally:
                peer.close()
            self.assertEqual(peer.state["sent_columns"], 80)
            self.assertFalse(peer.thread.is_alive())
            self.assertTrue(peer.state["closed"])

    @unittest.skipUnless(os.name == "nt", "Windows Job integration")
    def test_cli_timeout_reclaims_process_and_inherited_child(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            job = launch.OwnedJob()
            program = "import subprocess,sys,time,pathlib; p=subprocess.Popen([sys.executable,'-c','import time;time.sleep(60)']); pathlib.Path(sys.argv[1]).write_text(str(p.pid)); time.sleep(60)"
            try:
                with self.assertRaisesRegex(RuntimeError, "timed out"):
                    launch.run_cli([sys.executable, "-c", program, str(out / "child.pid")], out, job, 1.5)
                state = job.drain()
                self.assertEqual(state["active"], 0)
                self.assertGreaterEqual(state["total"], 2)  # venv Windows redirectors add processes.
                self.assertGreater(int((out / "child.pid").read_text()), 0)
                self.assertEqual(json.loads((out / "launcher-failure.json").read_text()), {"reason": "rea-timeout"})
            finally:
                job.terminate()
                job.close()

    def test_menu_summary_preserves_partial_vt_evidence_without_passing_menus(self):
        columns = 40
        text = "TF-SOFT:" + "s" * 28 + "LEFTRIGHT:END\nTF-HARD:" + "h" * 28 + "LEFT\nRIGHT:END\nTF-TAIL:two  \nTF-UNICODE:中文 e\u0301\nTF-VT-END\n"
        args = observation()
        args[0]["scenario"] = "menu-wrap"
        result = args[1]["data"]["evidence"]["normalized_result"]
        result["messages"] = [{"type": "send", "payload": {"kind": "snapshot", "phase": "vt-select-all", "text_state": json.dumps({"selection": text})}},
                              {"type": "send", "payload": {"error": "Error: system error", "stage": 8}}]
        peer = {"closed": True, "connected": True, "sent_columns": columns, "received_hex": "",
                "sent_sha256": hashlib.sha256(("\x1b[2J\x1b[H\x1b[?7h" + text.replace("\n", "\r\n")).encode()).hexdigest()}
        value = summary.summarize_menu(*args, peer)
        self.assertFalse(value["menu_verified"])
        self.assertFalse(value["complete"])
        self.assertTrue(value["vt"]["trailing_spaces_preserved"])
        peer["received_hex"] = "78"
        with self.assertRaisesRegex(ValueError, "leaked"):
            summary.summarize_menu(*args, peer)

    def test_local_success_never_claims_real_clipboard(self):
        result = summary.summarize(*observation())
        self.assertFalse(result["complete"])
        self.assertFalse(result["system_clipboard"]["verified"])

    def test_rejects_wrong_identity_incomplete_and_failed_runs(self):
        changes = [lambda a: a[0].update(sha256="other"),
                   lambda a: a[0].update(pid=202),
                   lambda a: a[0].update(script_sha256="other"),
                   lambda a: a[2].update(active=1),
                   lambda a: a[3].update(session_count=2),
                   lambda a: a[1]["data"]["evidence"]["normalized_result"].update(messages_truncated=True),
                   lambda a: a[1]["data"]["evidence"]["normalized_result"]["messages"].pop(),
                   lambda a: a[1]["data"]["evidence"]["normalized_result"]["messages"].append({"type": "error"}),
                   lambda a: a[1]["data"]["evidence"]["normalized_result"]["messages"][-1]["payload"].update(coverage="navigation-selection-system-clipboard-paste")]
        for change in changes:
            with self.subTest(change=change):
                args = observation()
                change(args)
                with self.assertRaises(ValueError):
                    summary.summarize(*args)

    def test_rejects_selection_with_lost_combining_mark(self):
        args = observation()
        messages = args[1]["data"]["evidence"]["normalized_result"]["messages"]
        phase = next(m["payload"] for m in messages if m["payload"].get("phase") == "drag-unicode")
        state = json.loads(phase["text_state"])
        state["selection"] = summary.SELECTION.replace("\u0301", "")
        phase["text_state"] = json.dumps(state)
        with self.assertRaisesRegex(ValueError, "Mouse drag"):
            summary.summarize(*args)

    def test_hosted_clipboard_guard_precedes_any_preparation(self):
        for environment in ({}, {"GITHUB_ACTIONS": "true"}, {"GITHUB_ACTIONS": "true", "RUNNER_ENVIRONMENT": "self-hosted"}):
            with self.subTest(environment=environment), patch.dict(launch.os.environ, environment, clear=True), patch.object(launch, "prepare") as prepare:
                with self.assertRaises(ValueError):
                    launch.run(Path("missing"), Path("missing"), Path("missing"), 1, hosted_ci=True)
                prepare.assert_not_called()

    def test_diagnostic_omits_widgets_paths_handles_and_error_stacks(self):
        args = observation()
        message = args[1]["data"]["evidence"]["normalized_result"]["messages"][0]["payload"]
        message["widgets"] = [{"class": "Private", "ptr": "PRIVATE-HANDLE", "name": "PRIVATE-PATH"}]
        args[1]["data"]["evidence"]["normalized_result"]["messages"].append({"type": "send", "payload": {
            "error": "Error: PRIVATE-PATH", "stack": "PRIVATE-STACK", "stage": 26}})
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, value in (("run", args[0]), ("evidence", args[1]), ("cleanup", args[2]), ("profile-check", args[3])):
                (root / (name + ".json")).write_text(json.dumps(value), encoding="utf-8")
            data = summary.diagnostic(root)
        self.assertNotIn("PRIVATE-", json.dumps(data))
        self.assertEqual(data["errors"], [{"stage": 26, "category": "probe-error", "reason": "unclassified"}])

    @unittest.skipUnless(os.name == "nt" and os.environ.get("TERMFORGE_R1_FAILURE_SOURCE"), "opt-in pinned reference failure injection")
    def test_suspended_target_is_reclaimed_if_job_assignment_fails(self):
        handles = []
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
        kernel.OpenProcess.restype = ctypes.c_void_p
        kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        def fail_assignment(job, pid):
            handle = kernel.OpenProcess(0x100000, False, pid)
            self.assertTrue(handle)
            handles.append(handle)
            raise RuntimeError("injected-job-assignment-failure")
        out = launch.ROOT / "local/ui-reference" / ("r1-cleanup-" + uuid.uuid4().hex)
        try:
            with patch.object(launch.OwnedJob, "assign", fail_assignment):
                with self.assertRaisesRegex(RuntimeError, "injected-job-assignment-failure"):
                    launch.run(Path(os.environ["TERMFORGE_R1_FAILURE_SOURCE"]), out, Path("unused-rea"), 1)
            self.assertEqual(len(handles), 1)
            self.assertEqual(kernel.WaitForSingleObject(handles[0], 0), 0)
            self.assertEqual(json.loads((out / "cleanup.json").read_text())["active"], 0)
            self.assertEqual(json.loads((out / "profile-check.json").read_text())["session_count"], 1)
        finally:
            for handle in handles:
                kernel.CloseHandle(handle)


if __name__ == "__main__":
    unittest.main()
