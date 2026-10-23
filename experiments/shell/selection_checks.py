"""Independent literal text/focus/clipboard oracles for the real selection UI."""
from __future__ import annotations

import json
from pathlib import Path


PHASES = ("empty", "forward", "reverse", "word", "line", "zoom-before", "zoom-after", "main-selected",
          "bottom-restored", "append-retained", "evicted", "invalid-copy")
ALPHA = "ALPHA  tail"
BETA = "BETA 中文 e\u0301"
OTHER = "OTHER  text"
SENTINEL = "TF_COPY_SENTINEL 中文 e\u0301"
COPY = {"empty": ("WM_COPY", SENTINEL), "forward": ("WM_COPY", ALPHA + "\r\n" + BETA),
        "reverse": ("workspace-command-copy", ALPHA + "\r\n" + BETA), "word": ("WM_COPY", "tail"),
        "line": ("workspace-command-copy", ALPHA), "main-selected": ("WM_COPY", OTHER),
        "bottom-restored": ("workspace-command-copy", ALPHA), "invalid-copy": ("WM_COPY", SENTINEL)}


def validate_selection(data: dict, system_clipboard: bool, run_record: dict) -> dict:
    if (not isinstance(data, dict) or data.get("schema") != "termforge-window-selection-v1" or
            type(data.get("system_clipboard")) is not bool or data["system_clipboard"] != system_clipboard or
            data.get("input_source") != "physical-pixel-mouse-messages" or
            not isinstance(data.get("snapshots"), list) or len(data["snapshots"]) != len(PHASES) or
            any(not isinstance(row, dict) for row in data["snapshots"]) or
            [row.get("phase") for row in data["snapshots"]] != list(PHASES)):
        raise ValueError("selection UI phases, actual input path or clipboard scope are incomplete")
    mode = "ui-copy-system" if system_clipboard else "ui-selection"
    if not isinstance(run_record, dict):
        raise ValueError("selection requires collector process and cleanup observations")
    result = run_record.get("result")
    if result is None:
        try:
            result = json.loads(run_record.get("stdout", ""))
        except (ValueError, TypeError):
            raise ValueError("selection collector lacks its native result") from None
    if (not isinstance(result, dict) or result.get("valid") is not True or result.get("mode") != mode or
            type(result.get("sessions_closed")) is not int or result["sessions_closed"] < 2 or
            type(result.get("active_children_after")) is not int or result["active_children_after"] != 0 or
            "presented_done" not in result or result["presented_done"] is not None or
            run_record.get("timed_out") is not False or type(run_record.get("exit_code")) is not int or run_record["exit_code"] != 0 or
            run_record.get("desktop_gone") is not True or not isinstance(run_record.get("job_after"), dict) or
            type(run_record["job_after"].get("active")) is not int or run_record["job_after"]["active"] != 0 or
            run_record.get("cleanup_errors")):
        raise ValueError("selection process, two-session closure, Job or desktop cleanup failed")
    final = {}
    for index, row in enumerate(data["snapshots"]):
        phase = PHASES[index]
        sessions = row.get("sessions")
        if (not isinstance(sessions, list) or len(sessions) != 2 or any(not isinstance(s, dict) for s in sessions) or
                [s.get("group") for s in sessions] != ["bottom", "main"]):
            raise ValueError("selection needs observations from both actual regions")
        bottom, main = sessions
        expected_bottom = ("" if phase in ("empty", "evicted", "invalid-copy") else
                           ALPHA + "\r\n" + BETA if phase in ("forward", "reverse") else
                           "tail" if phase == "word" else ALPHA)
        expected_main = OTHER if index >= PHASES.index("main-selected") else ""
        focus = "main" if phase in ("main-selected", "append-retained", "evicted") else "bottom"
        for session, expected in ((bottom, expected_bottom), (main, expected_main)):
            if (any(type(session.get(field)) is not bool for field in ("selected", "focused", "workspace_active", "frame_matches_full")) or
                    session["focused"] != (session["group"] == focus) or session["workspace_active"] != session["focused"] or
                    session["selected"] != bool(expected) or session.get("text") != expected or
                    session.get("status") != ("ok" if expected else "invalidated" if phase in ("evicted", "invalid-copy") and
                                              session["group"] == "bottom" else "empty") or
                    session["frame_matches_full"] is not True):
                raise ValueError(f"selection literal text, focused target, validity or dirty drawing mismatch: {phase}")
            integer_fields = ("overlay_cells", "zoom", "top", "total", "page", "maximum", "anchor")
            if (any(type(session.get(field)) is not int for field in integer_fields) or
                    not 1 <= session["page"] <= 200 or not 0 <= session["maximum"] <= 100 or
                    session["total"] != session["page"] + session["maximum"] or
                    not 0 <= session["top"] <= session["maximum"] or session["anchor"] <= 0 or
                    not 0 <= session["overlay_cells"] <= session["page"] * 500 or
                    session["zoom"] != (125 if session["group"] == "bottom" and index >= PHASES.index("zoom-after") else 100)):
                raise ValueError("selection bounds, stable viewport or independent session zoom failed")
            lines = session.get("visible_lines")
            if not isinstance(lines, list) or len(lines) != session["page"] or any(not isinstance(line, str) for line in lines):
                raise ValueError("selection visible output observation is incomplete")
            if not expected and session["overlay_cells"]:
                raise ValueError("empty or invalid selection left visible highlight cells")
            if (expected and not (session["group"] == "bottom" and phase == "append-retained") and
                    session["overlay_cells"] <= 0):
                raise ValueError("visible selection has no actual overlay cells")
        if phase in ("empty", "forward", "reverse", "word", "line"):
            lines = bottom["visible_lines"]
            if ALPHA not in lines or BETA not in lines or lines.index(BETA) != lines.index(ALPHA) + 1:
                raise ValueError("selection did not use adjacent full cmd output lines")
        if phase == "main-selected" and OTHER not in main["visible_lines"]:
            raise ValueError("second region selection did not use its actual output")
        if phase == "append-retained" and "TF_SELECT_APPEND" not in bottom["visible_lines"]:
            raise ValueError("selected session did not receive actual background output")
        if phase == "evicted" and (bottom["maximum"] != 100 or "TF_SEL_500" not in bottom["visible_lines"]):
            raise ValueError("selection eviction was not backed by the bounded 500-line output")
        expected_entry, expected_clipboard = COPY[phase] if system_clipboard and phase in COPY else ("none", None)
        if row.get("copy_entry") != expected_entry or row.get("clipboard") != expected_clipboard:
            raise ValueError("actual system clipboard content or copy action differs from its literal oracle")
        final[phase] = {"bottom": bottom["text"], "main": main["text"], "focus": focus}
    return {"system_clipboard": system_clipboard, "phases": len(PHASES), "sessions_closed": result["sessions_closed"],
            "cleanup_verified": True, "observations": final}


def validate_selection_workflow(directory: Path, system_clipboard: bool, run_record: dict) -> dict:
    return validate_selection(json.loads((Path(directory) / "selection.json").read_text(encoding="utf-8")),
                              system_clipboard, run_record)
