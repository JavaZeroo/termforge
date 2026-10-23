"""Pure context counterexamples and separately identified real-window gates."""
from __future__ import annotations

import copy
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from experiments.build_paths import ROOT, binary
from experiments.shell.run import execute
from experiments.shell.selection_context_checks import validate_selection_context, validate_selection_context_workflow


PHASES = ("bottom-selected", "main-a-selected", "main-b-selected", "main-a-hidden", "main-a-restored",
          "live-before", "live-overwritten", "main-a-reselected", "rename-editor", "rename-copy",
          "rename-cancelled", "main-b-restored", "main-b-close-prepared", "main-b-closed", "remaining-copy", "input-after")
FOCUS = ("A", "B", "C", "C", "B", "B", "B", "B", "editor", "editor", "B", "C", "C", "B", "B", "B")
B_TEXT = ("", "TF_FIND_075", "TF_FIND_075", "TF_FIND_075", "TF_FIND_075", "TF_STREAM_TICK_", "",
          "TF_FIND_075", "TF_FIND_075", "TF_FIND_075", "TF_FIND_075", "TF_FIND_075",
          "TF_FIND_075", "TF_FIND_075", "TF_FIND_075", "TF_FIND_075")


def synthetic_context():
    data = {"schema": "termforge-window-selection-context-v1", "system_clipboard": False,
            "input_source": "owned-tab-mouse-and-queued-workspace-edit-keys",
            "fixture": "shell_text_client-80-tilde-minus", "local_cf_actions": False, "snapshots": []}
    base = [f"TF_FIND_{number:03d} needle 中文 e\u0301" for number in range(80)] + ["TF_FIND_READY 080"]
    for index, phase in enumerate(PHASES):
        active = "B" if FOCUS[index] == "editor" else FOCUS[index]
        visible_main = "C" if index in (0, 2, 3, 11, 12) else "B"
        editor = index in (8, 9)
        changed = ({"before_id": 0, "before_version": 0, "after_id": 0, "after_version": 0} if index < 5 else
                   {"before_id": 1082, "before_version": 40, "after_id": 0 if index == 5 else 1082,
                    "after_version": 0 if index == 5 else 80})
        sessions = []
        for slot_index, slot in enumerate(("A", "B", "C")):
            if slot == "C" and index >= 13:
                sessions.append({"slot": "C", "present": False, "closed_hwnd_gone": True})
                continue
            selected = "TF_FIND_077" if slot == "A" else B_TEXT[index] if slot == "B" else "TF_FIND_079" if index >= 2 else ""
            content = base + (["TF_STREAM_STARTED", "TF_STREAM_TICK_00000007"] if slot == "B" and index == 5 else
                              ["TF_STREAM_STARTED", "TF_STREAM_STOPPED", ""] if slot == "B" and index > 5 else [""])
            start = len(content) - 7
            lines = [{"id": 1000 * slot_index + position, "version": 40 if index == 5 and slot == "B" and position == 82 else
                      80 if index > 5 and slot == "B" and position == 82 else 10, "text": content[position]}
                     for position in range(start, len(content))]
            sessions.append({"slot": slot, "present": True, "visible": slot == "A" or slot == visible_main,
                             "focused": slot == FOCUS[index], "workspace_active": slot == active, "closing": False,
                             "captured": False, "selected": bool(selected), "status": "ok" if selected else "invalidated" if slot == "B" and index == 6 else "empty",
                             "text": selected, "overlay_cells": len(selected) if selected else 0, "frame_matches_full": True,
                             "columns": 80, "rows": 7, "ticks": 10 + 4 * index, "top": start, "total": len(content), "maximum": start,
                             "tab_title": f"cmd {slot_index+1}", "product_status": "", "visible_lines": lines})
        data["snapshots"].append({"phase": phase, "entry": "none", "focus": FOCUS[index], "active": active,
                                  "main_tab_count": 1 if index >= 13 else 2, "clipboard": None,
                                  "raw": {"A": "", "B": "" if index < 5 else "~" if index == 5 else "~-x" if index == 15 else "~-", "C": ""},
                                  "overwrite": changed,
                                  "rename": {"present": editor, "focused": editor, "old_editor_gone": index >= 10,
                                             "text": "Context rename 中文" if editor else None, "class": "Edit" if editor else None,
                                             "selection_begin": 0 if editor else None, "selection_end": 17 if editor else None},
                                  "sessions": sessions})
    record = {"result": {"valid": True, "mode": "ui-selection-context", "sessions_closed": 3,
                         "active_children_after": 0, "presented_done": None},
              "timed_out": False, "exit_code": 0, "desktop_gone": True, "job_after": {"active": 0}}
    return data, record


class SelectionContextOracleTest(unittest.TestCase):
    def test_synthetic_contract_keeps_three_distinct_selections_and_close_target(self):
        data, record = synthetic_context()
        self.assertEqual(validate_selection_context(data, False, record)["phases"], 16)
        self.assertFalse(validate_selection_context(data, False, record)["rename_edit_copy"])

    def test_independent_context_counterexamples(self):
        data, record = synthetic_context()
        changes = {
            "missing_phase": lambda d, r: d["snapshots"].pop(6),
            "hidden_selection_lost": lambda d, r: d["snapshots"][3]["sessions"][1].__setitem__("text", ""),
            "restored_wrong_tab_text": lambda d, r: d["snapshots"][4]["sessions"][1].__setitem__("text", "TF_FIND_079"),
            "live_old_text_copied": lambda d, r: d["snapshots"][6]["sessions"][1].update(text="TF_STREAM_TICK_", selected=True, status="ok", overlay_cells=15),
            "no_same_line_mutation": lambda d, r: d["snapshots"][6]["overwrite"].__setitem__("after_id", 9999),
            "no_version_increment": lambda d, r: d["snapshots"][6]["overwrite"].__setitem__("after_version", 40),
            "unrelated_overwrite_row": lambda d, r: d["snapshots"][6]["sessions"][1]["visible_lines"][-2].__setitem__("id", 9999),
            "rename_wrong_focus": lambda d, r: d["snapshots"][9].__setitem__("focus", "B"),
            "rename_overwrites_pty_selection": lambda d, r: d["snapshots"][9]["sessions"][1].__setitem__("text", "Context rename 中文"),
            "rename_input_leaks": lambda d, r: d["snapshots"][9]["raw"].__setitem__("B", "~-\x03"),
            "rename_not_cancelled": lambda d, r: d["snapshots"][10]["rename"].__setitem__("old_editor_gone", False),
            "selected_close_not_observed": lambda d, r: d["snapshots"][12]["sessions"][2].__setitem__("text", ""),
            "closed_hwnd_left": lambda d, r: d["snapshots"][13]["sessions"][2].__setitem__("closed_hwnd_gone", False),
            "closed_selection_leaks": lambda d, r: d["snapshots"][14]["sessions"][1].__setitem__("text", "TF_FIND_079"),
            "wrong_remaining_input": lambda d, r: d["snapshots"][15]["raw"].__setitem__("C", "x"),
            "local_cf_called": lambda d, r: d["snapshots"][4].__setitem__("clipboard", "TF_FIND_075"),
            "stale_frame": lambda d, r: d["snapshots"][4]["sessions"][1].__setitem__("frame_matches_full", False),
            "not_three_closed": lambda d, r: r["result"].__setitem__("sessions_closed", 2),
            "job_leak": lambda d, r: r["job_after"].__setitem__("active", 1),
            "desktop_leak": lambda d, r: r.__setitem__("desktop_gone", False),
        }
        for name, change in changes.items():
            with self.subTest(name=name):
                modified, actual = copy.deepcopy(data), copy.deepcopy(record)
                change(modified, actual)
                with self.assertRaises(ValueError):
                    validate_selection_context(modified, False, actual)

    def test_live_status_can_be_empty_only_with_all_actual_invalidation_proof(self):
        data, record = synthetic_context()
        data["snapshots"][6]["sessions"][1]["status"] = "empty"
        validate_selection_context(data, False, record)
        data["snapshots"][6]["overwrite"]["after_version"] = 40
        with self.assertRaises(ValueError):
            validate_selection_context(data, False, record)

    def test_synthetic_system_contract_rejects_old_terminal_or_wrong_editor_copy(self):
        data, record = synthetic_context()
        data["system_clipboard"] = True
        record["result"]["mode"] = "ui-copy-context-system"
        expected = {"main-a-restored": "TF_FIND_075", "live-overwritten": "TF_CONTEXT_SENTINEL 中文 e\u0301",
                    "rename-copy": "Context rename 中文", "main-b-restored": "TF_FIND_079", "remaining-copy": "TF_FIND_075"}
        for row in data["snapshots"]:
            if row["phase"] not in expected:
                continue
            row["clipboard"] = expected[row["phase"]]
            row["entry"] = "queued-Ctrl-C-native-EDIT" if row["phase"] == "rename-copy" else "queued-Ctrl-Shift-C"
            if row["phase"] != "rename-copy":
                active = row["sessions"][1 if row["active"] == "B" else 2]
                active["product_status"] = "Selected text changed; select it again" if row["phase"] == "live-overwritten" else "Selected text copied"
        self.assertTrue(validate_selection_context(data, True, record)["rename_edit_copy"])
        for index, wrong in ((4, "TF_FIND_079"), (6, "TF_STREAM_TICK_"), (9, "TF_FIND_075"), (14, "TF_FIND_079")):
            with self.subTest(phase=index):
                modified = copy.deepcopy(data); modified["snapshots"][index]["clipboard"] = wrong
                with self.assertRaises(ValueError):
                    validate_selection_context(modified, True, record)

    def test_actual_files_and_journal_are_independent_gates(self):
        data, record = synthetic_context()
        with tempfile.TemporaryDirectory() as path:
            root = Path(path)
            (root / "selection-context.json").write_text(json.dumps(data), encoding="utf-8")
            journal = "\n".join(json.dumps(row) for row in data["snapshots"]) + "\n"
            (root / "selection-context-progress.jsonl").write_text(journal, encoding="utf-8")
            for slot, raw in {"A": b"", "B": b"~-x", "C": b""}.items():
                (root / f"selection-context-{slot}.bin").write_bytes(raw)
            transport = b"TF_STREAM_STARTED\r\n\rTF_STREAM_TICK_00000007\r\x1b[2KTF_STREAM_STOPPED\r\n"
            (root / "selection-context-overwrite.transport.bin").write_bytes(transport)
            validate_selection_context_workflow(root, False, record)
            for filename, corruption in (
                ("selection-context-B.bin", b"~-x\x03"),
                ("selection-context-C.bin", b"x"),
                ("selection-context-progress.jsonl", (journal + "{}\n").encode()),
                ("selection-context-overwrite.transport.bin", b"TF_STREAM_STARTED\r\nTF_STREAM_STOPPED"),
            ):
                target = root / filename; original = target.read_bytes()
                with self.subTest(file=filename):
                    target.write_bytes(corruption)
                    with self.assertRaises(ValueError):
                        validate_selection_context_workflow(root, False, record)
                    target.write_bytes(original)


@unittest.skipUnless(os.name == "nt", "selection context requires Windows ConPTY")
class SelectionContextWindowTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe = binary("termforge_shell")
        parent = Path(os.environ.get("TERMFORGE_TEST_ARTIFACTS", ROOT / "build/test-artifacts")) / "selection-context"
        parent.mkdir(parents=True, exist_ok=True)
        cls.out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))

    def test_real_selection_context_twice_without_clipboard(self):
        for repeat in range(2):
            record = execute(self.exe, "ui-selection-context", self.out / f"local-{repeat}")
            self.assertEqual(record["selection_context_workflow"]["phases"], 16)
            self.assertTrue(record["selection_context_workflow"]["selected_session_close_target_isolated"])
            self.assertFalse(record["selection_context_workflow"]["rename_edit_copy"])

    @unittest.skipUnless(os.environ.get("GITHUB_ACTIONS") == "true" and
                         os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted", "real CF/EDIT Copy requires a disposable hosted VM")
    def test_real_system_copy_and_edit_context_twice(self):
        for repeat in range(2):
            record = execute(self.exe, "ui-copy-context-system", self.out / f"system-{repeat}")
            self.assertEqual(record["selection_context_workflow"]["phases"], 16)
            self.assertTrue(record["selection_context_workflow"]["rename_edit_copy"])

    @unittest.skipIf(os.environ.get("GITHUB_ACTIONS") == "true" and
                     os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted", "hosted VM is authorized for real CF")
    def test_local_native_and_python_system_guards(self):
        frame = self.out / "blocked-native.bmp"
        result = subprocess.run([str(self.exe), "--test-owned", "ui-copy-context-system", str(frame)],
                                capture_output=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"disposable GitHub-hosted", result.stderr)
        self.assertFalse(frame.exists())
        with self.assertRaises(ValueError):
            execute(self.exe, "ui-copy-context-system", self.out / "blocked-python")


if __name__ == "__main__":
    unittest.main()
