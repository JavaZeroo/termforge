"""Independent ownership and literal text oracles for real selection contexts."""
from __future__ import annotations

import json
import os
from pathlib import Path
import re

from experiments.shell.clipboard_checks import require_hosted_clipboard

PHASES = ("bottom-selected", "main-a-selected", "main-b-selected", "main-a-hidden",
          "main-a-restored", "live-before", "live-overwritten", "main-a-reselected",
          "rename-editor", "rename-copy", "rename-cancelled", "main-b-restored",
          "main-b-close-prepared", "main-b-closed", "remaining-copy", "input-after")
FOCUS = ("A", "B", "C", "C", "B", "B", "B", "B", "editor", "editor", "B", "C", "C", "B", "B", "B")
ACTIVE = tuple("B" if slot == "editor" else slot for slot in FOCUS)
MAIN_VISIBLE = ("C", "B", "C", "C", "B", "B", "B", "B", "B", "B", "B", "C", "C", "B", "B", "B")
EDITOR = "Context rename 中文"
SENTINEL = "TF_CONTEXT_SENTINEL 中文 e\u0301"
PREFIX = "TF_STREAM_TICK_"
COPY_PHASES = {"main-a-restored": "TF_FIND_075", "live-overwritten": SENTINEL,
               "main-b-restored": "TF_FIND_079", "remaining-copy": "TF_FIND_075"}


def _expected_selection(index: int, slot: str) -> str:
    if slot == "A":
        return "TF_FIND_077"
    if slot == "C":
        return "TF_FIND_079" if index >= 2 else ""
    return "" if index == 0 or index == 6 else PREFIX if index == 5 else "TF_FIND_075"


def _raw(index: int) -> dict[str, str]:
    return {"A": "", "B": "" if index < 5 else "~" if index == 5 else "~-x" if index == 15 else "~-", "C": ""}


def _visible_fixture(index: int, slot: str, page: int) -> list[str | None]:
    lines: list[str | None] = [f"TF_FIND_{number:03d} needle 中文 e\u0301" for number in range(80)] + ["TF_FIND_READY 080"]
    if slot != "B" or index < 5:
        lines.append("")
    elif index == 5:
        lines.extend(("TF_STREAM_STARTED", None))
    else:
        lines.extend(("TF_STREAM_STARTED", "TF_STREAM_STOPPED", ""))
    return lines[-page:]


def _cleanup(record: dict, system: bool) -> None:
    if not isinstance(record, dict):
        raise ValueError("selection context requires actual collector evidence")
    result = record.get("result")
    if result is None:
        try:
            result = json.loads(record.get("stdout", ""))
        except (ValueError, TypeError):
            raise ValueError("selection context lacks its actual native result") from None
    if (not isinstance(result, dict) or result.get("valid") is not True or
            result.get("mode") != ("ui-copy-context-system" if system else "ui-selection-context") or
            type(result.get("sessions_closed")) is not int or result["sessions_closed"] != 3 or
            type(result.get("active_children_after")) is not int or result["active_children_after"] != 0 or
            "presented_done" not in result or result["presented_done"] is not None or
            record.get("timed_out") is not False or type(record.get("exit_code")) is not int or record["exit_code"] != 0 or
            record.get("desktop_gone") is not True or not isinstance(record.get("job_after"), dict) or
            type(record["job_after"].get("active")) is not int or record["job_after"]["active"] != 0 or record.get("cleanup_errors")):
        raise ValueError("selection context three-session close, native completion, Job or desktop cleanup failed")


def validate_selection_context(data: dict, system_clipboard: bool, run_record: dict) -> dict:
    if (not isinstance(data, dict) or data.get("schema") != "termforge-window-selection-context-v1" or
            type(data.get("system_clipboard")) is not bool or data["system_clipboard"] != system_clipboard or
            data.get("input_source") != "owned-tab-mouse-and-queued-workspace-edit-keys" or
            data.get("fixture") != "shell_text_client-80-tilde-minus" or data.get("local_cf_actions") is not False or
            not isinstance(data.get("snapshots"), list) or len(data["snapshots"]) != len(PHASES) or
            any(not isinstance(row, dict) for row in data["snapshots"]) or
            [row.get("phase") for row in data["snapshots"]] != list(PHASES)):
        raise ValueError("selection context actual fixture, input path or sixteen phases are incomplete")
    _cleanup(run_record, system_clipboard)
    previous_ticks = {"A": 0, "B": 0, "C": 0}
    overwrite_identity = None
    for index, row in enumerate(data["snapshots"]):
        phase = PHASES[index]
        expected_cf = COPY_PHASES.get(phase) if system_clipboard else None
        if system_clipboard and phase == "rename-copy":
            expected_cf = EDITOR
        entry = ("queued-Ctrl-Shift-C" if phase in COPY_PHASES else "queued-Ctrl-C-native-EDIT" if phase == "rename-copy" else "none") if system_clipboard else "none"
        if (row.get("focus") != FOCUS[index] or row.get("active") != ACTIVE[index] or row.get("entry") != entry or
                row.get("clipboard") != expected_cf or row.get("raw") != _raw(index) or
                type(row.get("main_tab_count")) is not int or row["main_tab_count"] != (1 if index >= 13 else 2)):
            raise ValueError(f"selection context actual focus, Copy target, CF or exact raw input differs: {phase}")
        editor = row.get("rename")
        present = index in (8, 9)
        if (not isinstance(editor, dict) or editor.get("present") is not present or editor.get("focused") is not present or
                editor.get("old_editor_gone") is not (index >= 10) or
                editor.get("text") != (EDITOR if present else None) or editor.get("class") != ("Edit" if present else None) or
                editor.get("selection_begin") != (0 if present else None) or editor.get("selection_end") != (17 if present else None)):
            raise ValueError("selection context actual EDIT focus, text, selection or cancellation differs")
        if present and any(type(editor.get(key)) is not int for key in ("selection_begin", "selection_end")):
            raise ValueError("selection context EDIT range lacks native integer positions")
        changed = row.get("overwrite")
        fields = ("before_id", "before_version", "after_id", "after_version")
        if not isinstance(changed, dict) or set(changed) != set(fields) or any(type(changed.get(key)) is not int for key in fields):
            raise ValueError("selection context lacks actual same-line version observations")
        if index < 5:
            if any(changed.values()):
                raise ValueError("selection context invented an overwrite before its producer event")
        else:
            identity = (changed["before_id"], changed["before_version"])
            if min(identity) <= 0 or (overwrite_identity is not None and identity != overwrite_identity):
                raise ValueError("selection context original live line identity/version changed retrospectively")
            overwrite_identity = identity
            if index == 5:
                if changed["after_id"] or changed["after_version"]:
                    raise ValueError("selection context invented completion before the actual overwrite")
            elif changed["after_id"] != changed["before_id"] or changed["after_version"] <= changed["before_version"]:
                raise ValueError("selection context did not observe a real version increment on the selected live line")
        sessions = row.get("sessions")
        if (not isinstance(sessions, list) or len(sessions) != 3 or any(not isinstance(s, dict) for s in sessions) or
                [s.get("slot") for s in sessions] != ["A", "B", "C"]):
            raise ValueError("selection context three owned session identities are incomplete")
        for session in sessions:
            slot = session["slot"]
            if slot == "C" and index >= 13:
                if session != {"slot": "C", "present": False, "closed_hwnd_gone": True}:
                    raise ValueError("selection context closed selected session still exposes a live HWND or state")
                continue
            ints = ("columns", "rows", "ticks", "overlay_cells", "top", "total", "maximum")
            flags = ("present", "visible", "focused", "workspace_active", "closing", "captured", "selected", "frame_matches_full")
            if (any(type(session.get(field)) is not int for field in ints) or any(type(session.get(field)) is not bool for field in flags) or
                    session["present"] is not True or session["closing"] is not False or session["captured"] is not False or
                    session["frame_matches_full"] is not True or not 20 <= session["columns"] <= 500 or not 5 <= session["rows"] <= 200 or
                    session["ticks"] <= 0 or session["ticks"] < previous_ticks[slot] or
                    not 0 <= session["maximum"] <= 100 or session["total"] != session["rows"] + session["maximum"] or
                    not 0 <= session["top"] <= session["maximum"] or
                    not 0 <= session["overlay_cells"] <= session["columns"] * session["rows"] or
                    session["tab_title"] != {"A": "cmd 1", "B": "cmd 2", "C": "cmd 3"}[slot]):
                raise ValueError(f"selection context native bounds, validity, complete frame or tab title differs: {phase}/{slot}")
            previous_ticks[slot] = session["ticks"]
            expected = _expected_selection(index, slot)
            if (session.get("text") != expected or session["selected"] != bool(expected) or
                    session["focused"] != (slot == FOCUS[index]) or session["workspace_active"] != (slot == ACTIVE[index]) or
                    session["visible"] != (slot == "A" or slot == MAIN_VISIBLE[index]) or bool(session["overlay_cells"]) != bool(expected)):
                raise ValueError(f"selection context literal selected text, focus, visibility or overlay differs: {phase}/{slot}")
            allowed_status = ("invalidated", "empty") if slot == "B" and index == 6 else ("ok",) if expected else ("empty",)
            if session.get("status") not in allowed_status:
                raise ValueError("selection context live overwrite did not clear old text or a valid range was lost")
            lines = session.get("visible_lines")
            literal = _visible_fixture(index, slot, session["rows"])
            if not isinstance(lines, list) or len(lines) != len(literal):
                raise ValueError("selection context visible fixture rows are incomplete")
            for actual, expected_line in zip(lines, literal):
                if (not isinstance(actual, dict) or any(type(actual.get(key)) is not int or actual[key] <= 0 for key in ("id", "version")) or
                        not isinstance(actual.get("text"), str) or
                        (actual["text"] != expected_line if expected_line is not None else re.fullmatch(r"TF_STREAM_TICK_[0-9]{8}", actual["text"]) is None)):
                    raise ValueError(f"selection context independent producer fixture or line metadata differs: {phase}/{slot}")
            if slot == "B" and index >= 5:
                actual = lines[-1] if index == 5 else lines[-2]
                if (actual["id"] != changed["before_id"] or
                        (actual["version"] != changed["before_version"] if index == 5 else actual["version"] < changed["after_version"])):
                    raise ValueError("selection context overwritten visible line is not the originally selected stable identity")
            if system_clipboard and phase in COPY_PHASES and slot == ACTIVE[index]:
                status = session.get("product_status")
                if (phase == "live-overwritten" and status not in ("No selected text", "Selected text changed; select it again")) or (
                        phase != "live-overwritten" and status != "Selected text copied"):
                    raise ValueError("selection context Copy did not take its actual product status path")
    for current, previous in ((3, 2), (4, 3), (6, 5), (9, 8), (11, 10), (14, 13), (15, 14)):
        if (data["snapshots"][current]["sessions"][1]["ticks"] -
                data["snapshots"][previous]["sessions"][1]["ticks"] < 3):
            raise ValueError("selection context held/copy observation did not wait for actual product timer progress")
    return {"system_clipboard": system_clipboard, "phases": len(PHASES), "same_group_tab_restore": True,
            "same_live_line_overwrite_invalidates": True, "rename_edit_copy": system_clipboard,
            "selected_session_close_target_isolated": True, "raw_input_isolated": True, "cleanup_verified": True}


def validate_selection_context_workflow(directory: Path, system_clipboard: bool, run_record: dict) -> dict:
    if system_clipboard:
        require_hosted_clipboard(os.environ)
    directory = Path(directory)
    data = json.loads((directory / "selection-context.json").read_text(encoding="utf-8"))
    summary = validate_selection_context(data, system_clipboard, run_record)
    stages = [json.loads(line) for line in (directory / "selection-context-progress.jsonl").read_text(encoding="utf-8").splitlines()]
    if stages != data["snapshots"]:
        raise ValueError("selection context final snapshots do not equal the original per-phase journal")
    for slot, expected in {"A": b"", "B": b"~-x", "C": b""}.items():
        if (directory / f"selection-context-{slot}.bin").read_bytes() != expected:
            raise ValueError("selection context actual raw receiver contains leaked Copy/EDIT/close input or a wrong target")
    transport = (directory / "selection-context-overwrite.transport.bin").read_bytes()
    if (not 1 <= len(transport) < 8192 or b"TF_STREAM_STARTED" not in transport or
            b"TF_STREAM_TICK_" not in transport or b"TF_STREAM_STOPPED" not in transport):
        raise ValueError("selection context actual CR-output transport is absent, truncated or lacks both event boundaries")
    return summary
