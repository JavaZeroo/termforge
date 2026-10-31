"""Synthetic schema/oracle tests, separate from real-window acceptance."""
from __future__ import annotations

import copy
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from experiments.shell.selection_bytes_checks import (
    PHASES, DPIS, HARD, CONT, READY, intended_output, expected_physical_lines, validate_selection_bytes,
    validate_selection_bytes_workflow,
)
from experiments.build_paths import ROOT, binary
from experiments.shell.run import execute


# Literal expected bytes are independent of the implementation's serializer.
SOFT = "SOFT_sssssssssssssssssssssssssssssssss  "
MULTI = SOFT + "CONT 中文 e\u0301  \r\nHARD 中文 e\u0301  "
PAYLOAD = ("\x1b[2J\x1b[H" + MULTI + "\r\nTF_SELECT_BYTES_READY\r\n").encode("utf-8")
STYLED_PAYLOAD = ("\x1b[2J\x1b[H" + SOFT + "CONT 中文 e\u0301\x1b[43m  \x1b[0m\r\n"
                  "HARD 中文 e\u0301\x1b[43m  \x1b[0m\r\nTF_SELECT_BYTES_READY\r\n").encode("utf-8")


def synthetic_contract():
    snapshots = []
    for index, (phase, dpi) in enumerate(zip(PHASES, DPIS)):
        sessions = []
        for group in ("bottom", "main"):
            text = MULTI if group == "main" and index < 2 else HARD if group == "main" and index < 6 else ""
            sessions.append({
                "group": group, "selected": bool(text), "focused": group == "main",
                "workspace_active": group == "main", "captured": False,
                "status": "ok" if text else "empty", "text": text,
                "overlay_cells": 10 if text else 0, "frame_matches_full": True,
                "display_dpi": dpi, "zoom": 100, "ticks": 10 + index * 4,
                "columns": 40, "rows": 8, "initial_columns": 40, "initial_rows": 8,
                "cell_width": max(1, dpi // 12), "cell_height": max(1, dpi // 6),
                "visible_lines": [SOFT.rstrip(" "), CONT.rstrip(" "), HARD.rstrip(" "), READY, "", "", "", ""],
                "product_status": "No selected text" if phase == "empty-copy" else "",
            })
        snapshots.append({"phase": phase, "controlled_dpi": dpi, "os_window_dpi": 96,
                          "copy_entry": "queued-Ctrl-Shift-C-empty" if phase == "empty-copy" else "none",
                          "clipboard": None, "raw_bottom": "", "raw_main": "x" if phase == "input-after" else "",
                          "sessions": sessions})
    data = {"schema": "termforge-window-selection-bytes-v1", "system_clipboard": False,
            "producer": "real-ConPTY-console-writefile", "input_source": "physical-pixel-drag-and-queued-workspace-keys",
            "controlled_dpi_only": True, "transport_observation_limit": 8192, "snapshots": snapshots}
    record = {"result": {"valid": True, "mode": "ui-selection-bytes", "sessions_closed": 2,
                         "active_children_after": 0, "presented_done": None},
              "timed_out": False, "exit_code": 0, "desktop_gone": True, "job_after": {"active": 0}}
    return data, record


class SelectionBytesOracleTest(unittest.TestCase):
    def test_literal_fixture_has_real_soft_boundary_hard_crlf_and_tails(self):
        self.assertEqual(len(SOFT), 40)
        self.assertEqual(intended_output(40), PAYLOAD)
        self.assertEqual(MULTI.count("\r\n"), 1)
        self.assertTrue(MULTI.endswith("e\u0301  "))

    def test_synthetic_contract_and_dpi_phases(self):
        data, record = synthetic_contract()
        result = validate_selection_bytes(data, False, record)
        self.assertEqual(result["phases"], 9)
        self.assertEqual(result["controlled_dpi"], [96, 144, 192, 96])

    def test_independent_repaint_oracle_accounts_for_one_extra_column(self):
        self.assertEqual(expected_physical_lines(40, 41, 6),
                         [SOFT + "C", "ONT 中文 e\u0301", "HARD 中文 e\u0301", READY, "", ""])

    def test_rejects_semantic_input_and_cleanup_counterexamples(self):
        data, record = synthetic_contract()
        changes = {
            "soft_join_changed_to_hard": lambda d, r: d["snapshots"][0]["sessions"][1].__setitem__("text", MULTI.replace("  CONT", "  \r\nCONT")),
            "explicit_tail_lost": lambda d, r: d["snapshots"][0]["sessions"][1].__setitem__("text", MULTI.rstrip(" ")),
            "hard_crlf_lost": lambda d, r: d["snapshots"][1]["sessions"][1].__setitem__("text", MULTI.replace("\r\n", "")),
            "normalization_changed": lambda d, r: d["snapshots"][1]["sessions"][1].__setitem__("text", MULTI.replace("e\u0301", "\u00e9")),
            "selection_invalidated_by_dpi": lambda d, r: d["snapshots"][3]["sessions"][1].update(text="", selected=False, status="invalidated", overlay_cells=0),
            "wrong_session": lambda d, r: d["snapshots"][0]["sessions"][0].__setitem__("focused", True),
            "old_overlay": lambda d, r: d["snapshots"][6]["sessions"][1].__setitem__("overlay_cells", 1),
            "no_full_redraw": lambda d, r: d["snapshots"][3]["sessions"][1].__setitem__("frame_matches_full", False),
            "missing_dpi": lambda d, r: d["snapshots"][4].__setitem__("controlled_dpi", 96),
            "fake_tick_progress": lambda d, r: d["snapshots"][4]["sessions"][1].__setitem__("ticks", 10),
            "escape_leaked": lambda d, r: d["snapshots"][8].__setitem__("raw_main", "x\x1b"),
            "wrong_raw_receiver": lambda d, r: d["snapshots"][8].__setitem__("raw_bottom", "x"),
            "missing_phase": lambda d, r: d["snapshots"].pop(3),
            "local_cf_touched": lambda d, r: d["snapshots"][0].__setitem__("clipboard", MULTI),
            "missing_empty_copy_action": lambda d, r: d["snapshots"][7].__setitem__("copy_entry", "none"),
            "desktop_leak": lambda d, r: r.__setitem__("desktop_gone", False),
            "job_leak": lambda d, r: r["job_after"].__setitem__("active", 1),
            "native_timeout": lambda d, r: r.__setitem__("timed_out", True),
        }
        for name, change in changes.items():
            with self.subTest(name=name):
                modified, actual_run = copy.deepcopy(data), copy.deepcopy(record)
                change(modified, actual_run)
                with self.assertRaises(ValueError):
                    validate_selection_bytes(modified, False, actual_run)

    def test_synthetic_files_keep_producer_transport_and_raw_separate(self):
        data, record = synthetic_contract()
        with tempfile.TemporaryDirectory() as path:
            root = Path(path)
            (root / "selection-bytes.json").write_text(json.dumps(data), encoding="utf-8")
            facts = {"schema": "termforge-selection-producer-v1", "console_input": True, "console_output": True,
                     "columns": 40, "rows": 8, "input_mode_before": 7, "input_mode_requested": 512,
                     "input_mode_readback": 512, "output_mode_before": 3, "output_mode_requested": 15,
                     "output_mode_readback": 15, "input_code_page": 65001, "output_code_page": 65001,
                     "payload_bytes": len(PAYLOAD)}
            for region in ("bottom", "main"):
                prefix = root / f"selection-bytes-{region}"
                prefix.with_suffix(".producer.json").write_text(json.dumps(facts), encoding="utf-8")
                prefix.with_suffix(".producer.bin").write_bytes(PAYLOAD)
                prefix.with_suffix(".transport.bin").write_bytes(b"\x1b[0m" + PAYLOAD)
                prefix.with_suffix(".bin").write_bytes(b"x" if region == "main" else b"")
            summary = validate_selection_bytes_workflow(root, False, record)
            self.assertFalse(summary["evidence"]["main"]["transport_equals_producer"])
            corruptions = (
                ("producer.json", lambda p: p.write_text(json.dumps(dict(facts, output_mode_readback=3)), encoding="utf-8")),
                ("producer.bin", lambda p: p.write_bytes(PAYLOAD.replace(b"\r\n", b"\n"))),
                ("transport.bin", lambda p: p.write_bytes(b"")),
                ("transport.bin", lambda p: p.write_bytes(b"x" * 8192)),
                ("bin", lambda p: p.write_bytes(b"x\x1b")),
            )
            for suffix, corrupt in corruptions:
                evidence = (root / "selection-bytes-main").with_suffix("." + suffix)
                original = evidence.read_bytes()
                with self.subTest(suffix=suffix):
                    corrupt(evidence)
                    with self.assertRaises(ValueError):
                        validate_selection_bytes_workflow(root, False, record)
                    evidence.write_bytes(original)

    def test_styled_fixture_requires_new_identity_and_literal_transport_spaces(self):
        data, record = synthetic_contract()
        data.update(schema="termforge-window-selection-bytes-v2", fixture_style="styled-tail-v1")
        for snapshot in data["snapshots"]:
            for session in snapshot["sessions"]:
                session["visible_lines"][:4] = [SOFT, CONT, HARD, READY]
        self.assertEqual(expected_physical_lines(40, 40, 8, "styled-tail-v1"),
                         [SOFT, CONT, HARD, READY, "", "", "", ""])
        self.assertEqual(intended_output(40, "styled-tail-v1"), STYLED_PAYLOAD)
        with tempfile.TemporaryDirectory() as path:
            root = Path(path)
            (root / "selection-bytes.json").write_text(json.dumps(data), encoding="utf-8")
            facts = {"schema": "termforge-selection-producer-v1", "fixture_style": "styled-tail-v1",
                     "console_input": True, "console_output": True, "columns": 40, "rows": 8,
                     "input_mode_before": 7, "input_mode_requested": 512, "input_mode_readback": 512,
                     "output_mode_before": 7, "output_mode_requested": 15, "output_mode_readback": 15,
                     "input_code_page": 65001, "output_code_page": 65001, "payload_bytes": len(STYLED_PAYLOAD)}
            transport = STYLED_PAYLOAD.replace(b"\x1b[0m", b"\x1b[m")
            for region in ("bottom", "main"):
                prefix = root / f"selection-bytes-{region}"
                prefix.with_suffix(".producer.json").write_text(json.dumps(facts), encoding="utf-8")
                prefix.with_suffix(".producer.bin").write_bytes(STYLED_PAYLOAD)
                prefix.with_suffix(".transport.bin").write_bytes(transport)
                prefix.with_suffix(".bin").write_bytes(b"x" if region == "main" else b"")
            summary = validate_selection_bytes_workflow(root, False, record)
            self.assertEqual(summary["fixture_style"], "styled-tail-v1")
            self.assertEqual(summary["evidence"]["main"]["literal_styled_tail_occurrences"], 2)
            (root / "selection-bytes-main.transport.bin").write_bytes(transport.replace(b"\x1b[43m  \x1b[m", b"\x1b[K"))
            with self.assertRaises(ValueError):
                validate_selection_bytes_workflow(root, False, record)
            (root / "selection-bytes-main.transport.bin").write_bytes(transport)
            facts["fixture_style"] = "plain-v1"
            (root / "selection-bytes-main.producer.json").write_text(json.dumps(facts), encoding="utf-8")
            with self.assertRaises(ValueError):
                validate_selection_bytes_workflow(root, False, record)


@unittest.skipUnless(os.name == "nt", "real selection byte acceptance requires Windows ConPTY")
class SelectionBytesWindowTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe = binary("termforge_shell")
        parent = Path(os.environ.get("TERMFORGE_TEST_ARTIFACTS", ROOT / "build/test-artifacts")) / "selection-bytes"
        parent.mkdir(parents=True, exist_ok=True)
        cls.out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))

    def test_real_styled_conpty_copy_projection_and_dpi_twice(self):
        for repeat in range(2):
            record = execute(self.exe, "ui-selection-bytes", self.out / f"local-{repeat}")
            summary = record["selection_bytes_workflow"]
            self.assertEqual(summary["fixture_style"], "styled-tail-v1")
            self.assertEqual(summary["phases"], 9)
            self.assertEqual(summary["controlled_dpi"], [96, 144, 192, 96])
            self.assertTrue(summary["dpi_selection_preserved"])
            self.assertTrue(summary["cleanup_verified"])

    @unittest.skipUnless(os.environ.get("GITHUB_ACTIONS") == "true" and
                         os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted", "system CF requires a disposable hosted VM")
    def test_owned_clipboard_contention_preserves_write_and_read_failures(self):
        for operation, stage in (("sentinel-write", 6), ("readback", 7)):
            with self.subTest(operation=operation):
                out = self.out / ("occupied-" + operation)
                with mock.patch.dict(os.environ, {"TERMFORGE_TEST_SELECTION_CLIPBOARD_HOLD": operation}):
                    # Workspace scenarios prefix their failure with the scenario
                    # identity, so the message may carry that prefix (#73).
                    with self.assertRaisesRegex(
                            ValueError, "shell probe failed: (workspace-failure .*? what=)?OpenClipboard: 5"):
                        execute(self.exe, "ui-copy-bytes-system", out)
                sample = json.loads((out / "sample.json").read_text(encoding="utf-8"))
                self.assertEqual(sample["exit_code"], 2)
                self.assertFalse(sample["timed_out"])
                self.assertTrue(sample["desktop_gone"])
                self.assertEqual(sample["job_after"]["active"], 0)
                evidence = json.loads((out / "selection-clipboard-failure.json").read_text(encoding="utf-8"))
                self.assertEqual(evidence, {"schema": "termforge-selection-clipboard-failure-v1", "stage": stage,
                    "operation": operation, "native_operation": "OpenClipboard", "native_error": 5,
                    "observation": "after-error-nonatomic", "open_window": "same-process-window",
                    "data_owner": "same-process-window"})
                released = json.loads((out / "selection-clipboard-owner-release.json").read_text(encoding="utf-8"))
                self.assertEqual(released, {"release_event": True, "thread_exited": True, "native_error": 0,
                    "worker_error": 0, "secondary_native_error": 0, "secondary_worker_error": 0})

    @unittest.skipUnless(os.environ.get("GITHUB_ACTIONS") == "true" and
                         os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted", "system CF requires a disposable hosted VM")
    def test_real_system_copy_shortcuts_for_styled_fixture_twice(self):
        for repeat in range(2):
            record = execute(self.exe, "ui-copy-bytes-system", self.out / f"system-{repeat}")
            summary = record["selection_bytes_workflow"]
            self.assertTrue(summary["system_clipboard"])
            self.assertEqual(summary["fixture_style"], "styled-tail-v1")
            self.assertEqual(summary["explicit_trailing_spaces"], 2)
            self.assertTrue(summary["cleanup_verified"])

    @unittest.skipIf(os.environ.get("GITHUB_ACTIONS") == "true" and
                     os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted", "hosted environment is authorized for system CF")
    def test_native_and_python_system_mode_guards_refuse_local(self):
        frame = self.out / "blocked-native.bmp"
        result = subprocess.run([str(self.exe), "--test-owned", "ui-copy-bytes-system", str(frame)],
                                capture_output=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"disposable GitHub-hosted", result.stderr)
        self.assertFalse(frame.exists())
        with self.assertRaises(ValueError):
            execute(self.exe, "ui-copy-bytes-system", self.out / "blocked-python")


if __name__ == "__main__":
    unittest.main()
