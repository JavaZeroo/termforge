"""Independent action counterexamples and bounded actual-window gates."""
from __future__ import annotations

import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from experiments.build_paths import ROOT, binary
from experiments.shell.action_checks import ALL_TEXT, validate_actions, validate_actions_workflow
from experiments.shell.run import execute
from experiments.shell.clipboard_checks import require_hosted_clipboard


BASIC_LOCAL_PHASES = ("ready", "terminal-empty", "empty-copy-rejected", "context-all", "context-find", "find-editor",
                "shortcut-find", "history-top", "history-bottom", "rename-editor", "busy-prompt", "busy-shortcut-rejected", "busy-terminal",
                "busy-session", "busy-cancelled", "live-selected", "invalidated-copy-rejected", "ordinary-input",
                "closing", "no-session", "empty-session-menu")
GEOMETRY_PHASES = ("geometry-144", "geometry-192", "geometry-96", "geometry-zoom75", "geometry-restored")
LOCAL_PHASES = BASIC_LOCAL_PHASES[:5] + GEOMETRY_PHASES + BASIC_LOCAL_PHASES[5:]
TAIL_PHASES = BASIC_LOCAL_PHASES[-6:]
SYSTEM_PHASES = ("system-copy", "system-shift-insert", "system-default-enter-prompt", "system-default-enter-accepted", "system-paste-prompt", "system-target-switched", "system-confirmed")
SYSTEM_FIND_PHASES = ("system-find-copy", "system-find-paste", "system-find-refocus")
ENTRIES = ("none", "terminal-context-inspect", "queued-Ctrl-Shift-C-empty", "terminal-context-all", "terminal-context-find",
           "native-edit-Ctrl-A-char-and-Ctrl-Shift-F-refocus", "queued-Ctrl-Shift-F", "queued-Ctrl-Shift-Home", "queued-Ctrl-Shift-End",
           "native-rename-Ctrl-A-and-char", "synthetic-prompt-state-fixture", "queued-Ctrl-Shift-V-busy", "terminal-context-inspect", "session-menu-inspect",
           "session-menu-cancel", "mouse-drag-live-prefix", "public-copy-invalidated", "queued-plain-character",
           "close-focused-session", "none", "session-menu-inspect")
SHIFT = "SHIFT 中文 e\u0301"
DEFAULT = "DEFAULT_ENTER 中文\rNEXT"
PASTE = "CTX 中文 e\u0301\rNEXT"


def popup(command, terminal, enabled):
    return {"schema": "termforge-owned-popup-menu-v1", "target_command": command, "actual_return_command": command,
            "inspection_only": command == 0, "opening_message": "WM_CONTEXTMENU" if terminal else "WM_LBUTTONDOWN",
            "input_path": "queued-context-message-and-popup-keys" if terminal else "queued-owner-mouse-and-popup-keys",
            "owned_non_input_desktop": True, "popup_class": "#32768", "popup_hwnd": 100,
            "popup_visible_observed": True, "returned": True, "popup_closed": True, "timed_out": False,
            "escape_cleanup": False, "endmenu_cleanup": False, "failure": None, "highlighted": command != 0,
            "navigation_keys": 1 if command else 0, "initial_state": 0, "highlighted_state": 128 if command else 0,
            "enabled_commands": list(enabled), "popup_rect": [10, 20, 300, 200], "item_rect": [11, 21, 299, 40]}


def synthetic_actions(system=False):
    common = BASIC_LOCAL_PHASES[:5] + GEOMETRY_PHASES + BASIC_LOCAL_PHASES[5:-6]
    find_end = common.index("find-editor") + 1
    phases = common[:find_end] + (SYSTEM_FIND_PHASES if system else ()) + common[find_end:] + (SYSTEM_PHASES if system else ()) + TAIL_PHASES
    entries = dict(zip(BASIC_LOCAL_PHASES, ENTRIES)) | dict(zip(SYSTEM_PHASES, (
        "terminal-context-copy", "queued-Shift-Insert", "terminal-context-default-paste", "queued-default-Enter", "terminal-context-paste", "tab-switch-during-prompt", "queued-prompt-Tab-Enter")))
    entries.update({phase: "controlled-DPI-with-Find-selection" if phase in GEOMETRY_PHASES[:3] else "session-zoom-with-Find-selection" for phase in GEOMETRY_PHASES})
    entries.update(dict(zip(SYSTEM_FIND_PHASES, ("queued-native-Find-EDIT-Ctrl-A-Ctrl-C", "queued-native-Find-EDIT-Ctrl-V", "queued-Ctrl-Shift-F-Chinese-and-Escape"))))
    # These tables express the externally required observations, independent of
    # the product availability implementation and of the native scenario's IDs.
    actual_popups = {
        "terminal-empty": (0, True, (2, 3, 4)), "context-all": (3, True, (2, 3, 4)),
        "context-find": (4, True, (1, 2, 3, 4)), "busy-terminal": (0, True, (3, 4)),
        "busy-session": (0, False, (8, 1, 2, 6, 5, 13, 15, 16)),
        "busy-cancelled": (13, False, (8, 1, 2, 6, 5, 13, 15, 16)),
        "system-copy": (1, True, (1, 2, 3, 4)), "system-paste-prompt": (2, True, (2, 3, 4)),
        "system-default-enter-prompt": (2, True, (2, 3, 4)),
        "empty-session-menu": (0, False, (8, 1, 2, 6, 5)),
    }
    data = {"schema": "termforge-window-actions-v1", "system_clipboard": system,
            "fixture": "shell_text_client-80", "snapshots": []}
    for index, phase in enumerate(phases):
        empty = phase in ("no-session", "empty-session-menu")
        focus = ("other" if empty else "rename" if phase == "rename-editor" else
                 "bottom-find" if phase in ("context-find", "find-editor") + GEOMETRY_PHASES + SYSTEM_FIND_PHASES[:2] else "main-find" if phase == "shortcut-find" else
                 "main-prompt" if phase in ("busy-prompt", "system-default-enter-prompt", "system-paste-prompt") else
                 "bottom" if phase in ("ready", "terminal-empty", "empty-copy-rejected", "context-all", "system-copy", "system-target-switched", "system-confirmed", "system-find-refocus") else "main")
        selected_copy = phase in ("context-all", "context-find", "find-editor", "system-copy", "system-target-switched", "system-confirmed", "live-selected") + GEOMETRY_PHASES + SYSTEM_FIND_PHASES
        busy_active = phase in ("busy-prompt", "busy-shortcut-rejected", "busy-terminal", "busy-session", "system-default-enter-prompt", "system-paste-prompt")
        states = {}
        for action in ("copy", "paste", "all", "find", "top", "bottom", "cancel"):
            reason = ("no-session" if empty else "closing" if phase == "closing" else "native-editor" if phase == "rename-editor" else
                      "no-selection" if action == "copy" and not selected_copy else "paste-busy" if action == "paste" and busy_active else
                      "no-paste" if action == "cancel" and not busy_active else "none")
            states[action] = {"enabled": reason == "none", "reason": reason}
        raw = SHIFT if phase in ("system-shift-insert", "system-default-enter-prompt") else SHIFT + DEFAULT if phase in ("system-default-enter-accepted", "system-paste-prompt", "system-target-switched") else SHIFT + DEFAULT + PASTE if phase == "system-confirmed" or system and phase in TAIL_PHASES else ""
        if phase in TAIL_PHASES:
            raw += "~" if phase == "live-selected" else "~-" if phase == "invalidated-copy-rejected" else "~-x"
        sessions = []
        for group in (() if empty else ("bottom", "main")):
            selected = group == "bottom" and index >= phases.index("context-all") or group == "main" and phase == "live-selected"
            query = "TF_FIND_077" if group == "bottom" and phase in ("context-find",) + GEOMETRY_PHASES else "q" if group == "bottom" and phase in ("find-editor", "system-find-copy") else "中文" if group == "bottom" and phase == "system-find-paste" else "TF_FIND_075" if group == "main" and phase == "shortcut-find" else None
            busy = group == "main" and phase in ("busy-prompt", "busy-shortcut-rejected", "busy-terminal", "busy-session", "system-default-enter-prompt", "system-paste-prompt", "system-target-switched")
            sessions.append({"group": group, "closing": group == "main" and phase == "closing", "active": focus.startswith(group),
                             "focused": focus == group, "selected": selected,
                             "selection": ALL_TEXT if selected and group == "bottom" else "TF_STREAM_TICK_" if selected else "",
                             "selection_status": "ok" if selected else "invalidated" if group == "main" and phase in TAIL_PHASES[1:] else "empty",
                             "paste_busy": busy, "prompt": busy, "prompt_focus": 1 if group == "main" and phase in ("busy-prompt", "system-default-enter-prompt", "system-paste-prompt") else 0, "find": query is not None, "query": query,
                             "find_line": query + " needle 中文 e\u0301" if query and phase not in ("find-editor",) + SYSTEM_FIND_PHASES else None,
                             "find_range": {"id": 78 if group == "bottom" else 1076, "version": 10 + index, "begin": 0, "end": 11} if query and phase not in ("find-editor",) + SYSTEM_FIND_PHASES else None,
                             "find_geometry": {"window": [10, 10, 560, 120], "edit": [20, 20, 250, 45], "font_height": -18 if phase == "geometry-144" else -24 if phase == "geometry-192" else -12, "os_dpi": 96} if query else None,
                             "display_dpi": 144 if phase == "geometry-144" else 192 if phase == "geometry-192" else 96,
                             "zoom_percent": 75 if group == "bottom" and phase == "geometry-zoom75" else 100,
                             "cell_pixels": [18, 39] if phase == "geometry-144" else [24, 52] if phase == "geometry-192" else [9, 20] if group == "bottom" and phase == "geometry-zoom75" else [12, 26],
                             "grid": [112, 7], "overlay_cells": 44 if selected else 0,
                             "top": 0 if phase == "history-top" and group == "main" else 74, "maximum": 74, "ticks": 10 + index,
                             "status": "Selected text changed; select it again" if group == "main" and phase == "invalidated-copy-rejected" else
                                       "Paste already in progress" if group == "main" and phase == "busy-shortcut-rejected" else
                                       "No selected text" if group == "bottom" and phase == "empty-copy-rejected" else "", "frame_matches_full": True})
            sessions[-1]["active"] = group == ("bottom" if focus.startswith("bottom") else "main")
            sessions[-1]["search_generation"] = 1 + index
            sessions[-1]["valid_find_ranges"] = [sessions[-1]["find_range"].copy()] if sessions[-1]["find_range"] else []
        changed = phase in TAIL_PHASES
        overwrite = {"before_id": 82 if changed else 0, "before_version": 40 if changed else 0,
                     "after_id": 82 if changed and phase != "live-selected" else 0,
                     "after_version": 80 if changed and phase != "live-selected" else 0}
        data["snapshots"].append({"phase": phase, "entry": entries[phase], "session_count": len(sessions), "workspace_actions": states,
            "controlled_dpi": 144 if phase == "geometry-144" else 192 if phase == "geometry-192" else 96,
            "os_window_dpi": 96, "logical_client": [1360, 860], "physical_client": [2040, 1290] if phase == "geometry-144" else [2720, 1720] if phase == "geometry-192" else [1360, 860],
            "focus": focus, "rename_text": "R" if phase == "rename-editor" else None,
            "find_refocus": {"input_path": "queued-Ctrl-Shift-F-in-Find-EDIT", "before": [1, 1], "after": [0, 1],
                             "query_before": "z", "selected": "z", "focused": True} if phase == "find-editor" else None,
            "find_edit_clipboard": {"editor_hwnd": 321, "editor_class": "Edit", "focused_before_escape": True,
                                    "query": "q" if phase == "system-find-copy" else "中文",
                                    "selection": [0, 1] if phase == "system-find-copy" else [2, 2] if phase == "system-find-paste" else [0, 2],
                                    "selected": "q" if phase == "system-find-copy" else "" if phase == "system-find-paste" else "中文",
                                    "clipboard_before_copy": "TF_FIND_EDIT_SENTINEL 中文",
                                    "clipboard_before_paste": None if phase == "system-find-copy" else "中文",
                                    "refocus_before": [2, 2] if phase == "system-find-refocus" else None,
                                    "raw_before": {"bottom": "", "main": ""},
                                    "returned_terminal": phase == "system-find-refocus", "editor_gone": phase == "system-find-refocus"} if phase in SYSTEM_FIND_PHASES else None,
            "raw": {"bottom": "", "main": raw}, "overwrite": overwrite,
            "clipboard": "q" if phase == "system-find-copy" else "中文" if phase in SYSTEM_FIND_PHASES[1:] else ALL_TEXT if phase == "system-copy" else SHIFT if phase == "system-shift-insert" else "DEFAULT_ENTER 中文\r\nNEXT" if phase in ("system-default-enter-prompt", "system-default-enter-accepted") else "CTX 中文 e\u0301\r\nNEXT" if phase == "system-paste-prompt" else None,
            "popup": popup(*actual_popups[phase]) if phase in actual_popups else None, "sessions": sessions})
    record = {"result": {"valid": True, "mode": "ui-actions-system" if system else "ui-actions", "sessions_closed": 2,
                         "active_children_after": 0, "presented_done": None}, "timed_out": False, "exit_code": 0,
              "desktop_gone": True, "job_after": {"active": 0}}
    return data, record


class ActionOracleTest(unittest.TestCase):
    def test_literal_local_and_system_contracts(self):
        for system, count in ((False, 26), (True, 36)):
            data, record = synthetic_actions(system)
            summary = validate_actions(data, system, record)
            self.assertEqual(summary["phases"], count)
            self.assertIs(summary["native_find_clipboard_verified"], system)
            self.assertFalse(summary["complete_r1_07"])

    def test_old_system_33_phases_do_not_cover_find_editor_clipboard(self):
        data, record = synthetic_actions(True)
        data["snapshots"] = [row for row in data["snapshots"] if row["phase"] not in SYSTEM_FIND_PHASES]
        self.assertEqual(len(data["snapshots"]), 33)
        with self.assertRaisesRegex(ValueError, "incomplete or reordered"):
            validate_actions(data, True, record)

    def test_paste_selection_is_observed_before_explicit_refocus_caret_preparation(self):
        data, record = synthetic_actions(True)
        pasted = next(row for row in data["snapshots"] if row["phase"] == "system-find-paste")
        pasted["find_edit_clipboard"].update(selection=[0, 2], selected="中文")
        self.assertEqual(validate_actions(data, True, record)["phases"], 36)

    def test_independent_counterexamples(self):
        data, record = synthetic_actions(True)
        def row(value, phase):
            return next(item for item in value["snapshots"] if item["phase"] == phase)
        changes = {
            "missing_phase": lambda d, r: d["snapshots"].pop(15),
            "command_without_popup": lambda d, r: row(d, "context-all").__setitem__("popup", None),
            "popup_not_visible": lambda d, r: row(d, "context-all")["popup"].__setitem__("popup_visible_observed", False),
            "popup_wrong_return": lambda d, r: row(d, "context-all")["popup"].__setitem__("actual_return_command", 4),
            "popup_bool_return": lambda d, r: row(d, "terminal-empty")["popup"].__setitem__("actual_return_command", False),
            "popup_busy_paste_enabled": lambda d, r: row(d, "busy-terminal")["popup"]["enabled_commands"].append(2),
            "busy_public_enabled": lambda d, r: row(d, "busy-prompt")["workspace_actions"]["paste"].update(enabled=True, reason="none"),
            "busy_shortcut_read_clipboard": lambda d, r: row(d, "busy-shortcut-rejected").__setitem__("clipboard", "HOLD"),
            "find_key_leaked": lambda d, r: row(d, "shortcut-find")["raw"].__setitem__("main", "\x06"),
            "find_wrong_session": lambda d, r: row(d, "shortcut-find")["sessions"][0].update(find=True, query="TF_FIND_075"),
            "find_refocus_missing": lambda d, r: row(d, "find-editor").__setitem__("find_refocus", None),
            "find_refocus_not_selected": lambda d, r: row(d, "find-editor")["find_refocus"].update(after=[1, 1], selected=""),
            "find_refocus_boolean_range": lambda d, r: row(d, "find-editor")["find_refocus"].__setitem__("after", [False, True]),
            "find_refocus_no_full_replacement": lambda d, r: row(d, "find-editor")["sessions"][0].__setitem__("query", "zq"),
            "find_refocus_control_character": lambda d, r: row(d, "find-editor")["sessions"][0].__setitem__("query", "q\x06"),
            "find_refocus_pty_leak": lambda d, r: row(d, "find-editor")["raw"].__setitem__("bottom", "\x06"),
            "native_find_copy_kept_sentinel": lambda d, r: row(d, "system-find-copy").__setitem__("clipboard", "TF_FIND_EDIT_SENTINEL 中文"),
            "native_find_copy_borrowed_terminal": lambda d, r: row(d, "system-find-copy").__setitem__("clipboard", ALL_TEXT),
            "native_find_copy_preexisting_q": lambda d, r: row(d, "system-find-copy")["find_edit_clipboard"].__setitem__("clipboard_before_copy", "q"),
            "native_find_copy_not_selected": lambda d, r: row(d, "system-find-copy")["find_edit_clipboard"].update(selection=[1, 1], selected=""),
            "native_find_editor_wrong_focus": lambda d, r: row(d, "system-find-copy")["find_edit_clipboard"].__setitem__("focused_before_escape", False),
            "native_find_editor_changed": lambda d, r: row(d, "system-find-paste")["find_edit_clipboard"].__setitem__("editor_hwnd", 322),
            "native_find_editor_not_edit": lambda d, r: row(d, "system-find-copy")["find_edit_clipboard"].__setitem__("editor_class", "TermForgeShell"),
            "native_find_copy_boolean_range": lambda d, r: row(d, "system-find-copy")["find_edit_clipboard"].__setitem__("selection", [False, True]),
            "native_find_paste_appended": lambda d, r: row(d, "system-find-paste")["sessions"][0].__setitem__("query", "q中文"),
            "native_find_paste_wrong_source": lambda d, r: row(d, "system-find-paste")["find_edit_clipboard"].__setitem__("clipboard_before_paste", "wrong"),
            "native_find_paste_selection_outside_query": lambda d, r: row(d, "system-find-paste")["find_edit_clipboard"].__setitem__("selection", [0, 3]),
            "native_find_paste_selection_inconsistent": lambda d, r: row(d, "system-find-paste")["find_edit_clipboard"].update(selection=[0, 1], selected="文"),
            "native_find_copy_raw_before": lambda d, r: row(d, "system-find-copy")["find_edit_clipboard"]["raw_before"].__setitem__("main", "q"),
            "native_find_paste_raw_after": lambda d, r: row(d, "system-find-paste")["raw"].__setitem__("bottom", "中文\x16"),
            "native_find_chinese_not_selected": lambda d, r: row(d, "system-find-refocus")["find_edit_clipboard"].update(selection=[2, 2], selected=""),
            "native_find_refocus_was_already_selected": lambda d, r: row(d, "system-find-refocus")["find_edit_clipboard"].__setitem__("refocus_before", [0, 2]),
            "native_find_escape_not_returned": lambda d, r: row(d, "system-find-refocus")["find_edit_clipboard"].__setitem__("returned_terminal", False),
            "native_find_escape_editor_left": lambda d, r: row(d, "system-find-refocus")["find_edit_clipboard"].__setitem__("editor_gone", False),
            "home_not_top": lambda d, r: row(d, "history-top")["sessions"][1].__setitem__("top", 1),
            "native_editor_block_lost": lambda d, r: row(d, "rename-editor")["workspace_actions"]["find"].update(enabled=True, reason="none"),
            "ctrl_a_not_selecting_all": lambda d, r: row(d, "find-editor")["sessions"][0].__setitem__("query", "zTF_FIND_077"),
            "dpi_physical_crop": lambda d, r: row(d, "geometry-144").__setitem__("physical_client", [1360, 860]),
            "geometry_focus_stolen": lambda d, r: row(d, "geometry-144").__setitem__("focus", "bottom"),
            "geometry_edit_font_not_scaled": lambda d, r: row(d, "geometry-144")["sessions"][0]["find_geometry"].__setitem__("font_height", -12),
            "geometry_selection_lost": lambda d, r: row(d, "geometry-192")["sessions"][0].update(selected=False, selection="", selection_status="invalidated"),
            "geometry_current_lost": lambda d, r: row(d, "geometry-96")["sessions"][0].__setitem__("find_range", None),
            "geometry_current_stale": lambda d, r: row(d, "geometry-96")["sessions"][0].__setitem__("valid_find_ranges", []),
            "geometry_overlay_lost": lambda d, r: row(d, "geometry-zoom75")["sessions"][0].__setitem__("overlay_cells", 0),
            "zoom_leaked_to_main": lambda d, r: row(d, "geometry-zoom75")["sessions"][1].__setitem__("zoom_percent", 75),
            "rename_pty_leak": lambda d, r: row(d, "rename-editor")["raw"].__setitem__("main", "R"),
            "copy_wrong_bytes": lambda d, r: row(d, "system-copy").__setitem__("clipboard", "TF_FIND_079"),
            "paste_sent_early": lambda d, r: row(d, "system-paste-prompt")["raw"].__setitem__("main", SHIFT + PASTE),
            "default_enter_sent_early": lambda d, r: row(d, "system-default-enter-prompt")["raw"].__setitem__("main", SHIFT + DEFAULT),
            "default_enter_wrong_focus": lambda d, r: row(d, "system-default-enter-prompt")["sessions"][1].__setitem__("prompt_focus", 2),
            "default_enter_cancelled": lambda d, r: row(d, "system-default-enter-accepted")["raw"].__setitem__("main", SHIFT),
            "default_enter_duplicated": lambda d, r: row(d, "system-default-enter-accepted")["raw"].__setitem__("main", SHIFT + DEFAULT*2),
            "paste_retargeted": lambda d, r: row(d, "system-confirmed")["raw"].__setitem__("bottom", PASTE),
            "invalidated_stale_copy": lambda d, r: row(d, "invalidated-copy-rejected")["sessions"][1].update(selected=True, selection="TF_STREAM_TICK_", selection_status="ok"),
            "invalidated_feedback_hidden": lambda d, r: row(d, "invalidated-copy-rejected")["sessions"][1].__setitem__("status", "No selected text"),
            "different_overwrite_id": lambda d, r: row(d, "invalidated-copy-rejected")["overwrite"].__setitem__("after_id", 999),
            "overwrite_version_unchanged": lambda d, r: row(d, "invalidated-copy-rejected")["overwrite"].__setitem__("after_version", 40),
            "closing_still_enabled": lambda d, r: row(d, "closing")["workspace_actions"]["paste"].update(enabled=True, reason="none"),
            "no_session_enabled": lambda d, r: row(d, "no-session")["workspace_actions"]["all"].update(enabled=True, reason="none"),
            "frame_stale": lambda d, r: row(d, "context-all")["sessions"][0].__setitem__("frame_matches_full", False),
            "job_leaked": lambda d, r: r["job_after"].__setitem__("active", 1),
            "desktop_left": lambda d, r: r.__setitem__("desktop_gone", False),
        }
        for name, change in changes.items():
            with self.subTest(name=name):
                altered, actual = copy.deepcopy(data), copy.deepcopy(record); change(altered, actual)
                with self.assertRaises(ValueError):
                    validate_actions(altered, True, actual)

    def test_actual_journal_raw_and_overwrite_transport_are_independent(self):
        data, record = synthetic_actions()
        with tempfile.TemporaryDirectory() as path:
            directory = Path(path)
            (directory / "actions.json").write_text(json.dumps(data), encoding="utf-8")
            journal = "".join(json.dumps(row) + "\n" for row in data["snapshots"])
            (directory / "actions-progress.jsonl").write_text(journal, encoding="utf-8")
            (directory / "actions-bottom.bin").write_bytes(b"")
            (directory / "actions-main.bin").write_bytes(b"~-x")
            (directory / "actions-overwrite.transport.bin").write_bytes(b"TF_STREAM_STARTED\r\n\rTF_STREAM_TICK_00000001\r\x1b[2KTF_STREAM_STOPPED\r\n")
            for group in ("bottom", "main"):
                (directory / f"actions-geometry-{group}.transport.bin").write_bytes(b"")
            for row in data["snapshots"]:
                if row["phase"] in GEOMETRY_PHASES:
                    width, height = row["physical_client"]; frame = bytearray(54 + width*height*4)
                    frame[:2] = b"BM"; frame[2:6] = len(frame).to_bytes(4, "little"); frame[10:14] = (54).to_bytes(4, "little")
                    frame[14:18] = (40).to_bytes(4, "little"); frame[18:22] = width.to_bytes(4, "little"); frame[22:26] = (-height).to_bytes(4, "little", signed=True)
                    frame[26:28] = (1).to_bytes(2, "little"); frame[28:30] = (32).to_bytes(2, "little")
                    (directory / (row["phase"] + ".bmp")).write_bytes(frame)
            validate_actions_workflow(directory, False, record)
            for name, value in (("actions-main.bin", b"~-x\x06"), ("actions-bottom.bin", b"x"),
                                ("actions-progress.jsonl", (journal + "{}\n").encode()),
                                ("geometry-144.bmp", b"BM" + bytes(53)),
                                ("actions-geometry-main.transport.bin", bytes(65537)),
                                ("actions-overwrite.transport.bin", b"TF_STREAM_STARTED TF_STREAM_STOPPED")):
                target = directory / name; original = target.read_bytes()
                with self.subTest(file=name):
                    target.write_bytes(value)
                    with self.assertRaises(ValueError):
                        validate_actions_workflow(directory, False, record)
                    target.write_bytes(original)

    def test_workflow_keeps_prior_evidence_and_adds_actions(self):
        workflow = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
        for directory in ("paste", "shell", "selection-bytes", "selection-context", "text-actions", "raw-console-receiver"):
            self.assertEqual(workflow.count(f"build/test-artifacts/{directory}/"), 1)
        self.assertIn("TERMFORGE_REQUIRE_SYSTEM_CLIPBOARD:", workflow)
        self.assertIn("RESOURCE_LOCK system_clipboard", (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))

    def test_python_system_guard_precedes_directory_and_native_calls(self):
        with tempfile.TemporaryDirectory() as path, mock.patch.dict(os.environ, {}, clear=True):
            output = Path(path) / "must-not-exist"
            with self.assertRaisesRegex(ValueError, "disposable GitHub-hosted"):
                execute(Path(path) / "missing-shell.exe", "ui-actions-system", output)
            self.assertFalse(output.exists())


@unittest.skipUnless(os.name == "nt", "action windows require Windows ConPTY")
class ActionWindowTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if os.environ.get("TERMFORGE_REQUIRE_SYSTEM_CLIPBOARD") == "1":
            require_hosted_clipboard(os.environ)
        cls.exe = binary("termforge_shell")
        parent = Path(os.environ.get("TERMFORGE_TEST_ARTIFACTS", ROOT / "build/test-artifacts")) / "text-actions"
        parent.mkdir(parents=True, exist_ok=True)
        cls.out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
        files = ("experiments/shell/shell.cpp", "experiments/shell/workspace.hpp", "experiments/shell/text_actions.hpp",
                 "experiments/shell/text_interactions.hpp", "experiments/shell/search_interactions.hpp", "experiments/shell/workspace_interactions.hpp", "experiments/shell/edit_actions.hpp", "experiments/shell/find_bar.hpp", "experiments/shell/paste_prompt.hpp",
                 "experiments/shell/workspace_actions_scenario.hpp", "experiments/shell/menu_gesture.hpp", "experiments/shell/text_client.cpp",
                 "experiments/shell/test_launcher.cpp", "experiments/shell/action_checks.py", "experiments/shell/test_actions.py", "experiments/shell/run.py", "CMakeLists.txt", ".github/workflows/build.yml")
        cls.identity = {"schema": "termforge-actions-test-identity-v1", "source_sha256": {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in files},
                        "binary_sha256": {name: hashlib.sha256(cls.exe.with_name(name).read_bytes()).hexdigest() for name in
                                          (cls.exe.name, "shell_test_launcher.exe", "shell_text_client.exe", "shell_clipboard_reader.exe")}}
        (cls.out / "identity.json").write_text(json.dumps(cls.identity, indent=2) + "\n", encoding="utf-8")

    def test_actual_local_actions_twice_without_clipboard(self):
        for repeat in range(2):
            with self.subTest(repeat=repeat):
                record = execute(self.exe, "ui-actions", self.out / f"local-{repeat}")
                self.assertEqual(record["actions_workflow"]["phases"], 26)
                self.assertFalse(record["actions_workflow"]["system_clipboard"])
                self.assertFalse(record["actions_workflow"]["native_find_clipboard_verified"])

    @unittest.skipUnless(os.environ.get("GITHUB_ACTIONS") == "true" and os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted",
                         "actual system Copy/Paste requires a disposable hosted VM")
    def test_actual_system_copy_paste_and_shortcuts_twice(self):
        for repeat in range(2):
            with self.subTest(repeat=repeat):
                record = execute(self.exe, "ui-actions-system", self.out / f"system-{repeat}")
                self.assertEqual(record["actions_workflow"]["phases"], 36)
                self.assertTrue(record["actions_workflow"]["system_clipboard"])
                self.assertTrue(record["actions_workflow"]["native_find_clipboard_verified"])

    @unittest.skipIf(os.environ.get("GITHUB_ACTIONS") == "true" and os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted",
                     "hosted VM is authorized for actual clipboard")
    def test_native_and_python_guards_before_output(self):
        frame = self.out / "blocked-native.bmp"
        result = subprocess.run([str(self.exe), "--test-owned", "ui-actions-system", str(frame)], capture_output=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"disposable GitHub-hosted", result.stderr)
        self.assertFalse(frame.exists())
        blocked = self.out / "blocked-python"
        with self.assertRaises(ValueError):
            execute(self.exe, "ui-actions-system", blocked)
        self.assertFalse(blocked.exists())


if __name__ == "__main__":
    unittest.main()
