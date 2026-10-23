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
INTERACTIONS = ("autoscroll-up-start", "autoscroll-up-held", "autoscroll-up-release", "autoscroll-up-stopped",
                "autoscroll-down-start", "autoscroll-down-held", "autoscroll-focus-release", "autoscroll-focus-stopped",
                "autoscroll-capture-start", "autoscroll-capture-held", "autoscroll-capture-release", "autoscroll-capture-stopped",
                "select-all-shortcut", "select-all-shortcut-cleared", "select-all-command", "select-all-command-cleared",
                "input-after-select-all")


def _validate_popup(menu: dict, command: int = 15) -> None:
    expected_index = 8 if command == 14 else 9
    expected_enabled = [8, 1, 2, 6, 5, 12, 14, 15, 16] if command == 14 else [8, 1, 2, 6, 5, 12, 15, 16]
    if (not isinstance(menu, dict) or menu.get("schema") != "termforge-owned-popup-menu-v1" or
            menu.get("input_path") != "queued-owner-mouse-and-popup-keys" or menu.get("owned_non_input_desktop") is not True or
            any(type(menu.get(key)) is not int for key in ("target_command", "target_index", "initial_state", "highlighted_state",
                                                          "actual_return_command", "popup_hwnd", "navigation_keys", "last_highlighted_command")) or
            command not in (14, 15) or menu["target_command"] != command or menu["target_index"] != expected_index or menu["actual_return_command"] != command or
            menu["popup_hwnd"] <= 0 or menu.get("popup_class") != "#32768" or menu["initial_state"] & 3 or
            menu["highlighted_state"] & 128 != 128 or menu["highlighted_state"] & 3 or
            menu["last_highlighted_command"] != command or not 1 <= menu["navigation_keys"] <= len(expected_enabled) + 1 or
            menu.get("enabled_commands") != expected_enabled or
            any(menu.get(key) is not True for key in ("popup_visible_observed", "highlighted", "returned", "popup_closed")) or
            any(menu.get(key) is not False for key in ("timed_out", "escape_cleanup", "endmenu_cleanup")) or menu.get("failure") is not None):
        raise ValueError("actual product popup HWND, enabled/highlighted target, returned command or cleanup is unverified")
    popup, item = menu.get("popup_rect"), menu.get("item_rect")
    if (any(not isinstance(rect, list) or len(rect) != 4 or any(type(n) is not int for n in rect) for rect in (popup, item)) or
            not popup[0] <= item[0] < item[2] <= popup[2] or not popup[1] <= item[1] < item[3] <= popup[3]):
        raise ValueError("target menu item was not observed inside the actual visible popup")


def _validate_copy_popup(data: dict) -> bool:
    fields = ("copy_popup_verified", "copy_popup_observer_idle", "copy_popup_sequence_before", "copy_popup_sequence_after", "copy_popup_menu")
    if data.get("schema") == "termforge-window-selection-v3":
        if any(field in data for field in fields):
            raise ValueError("actual Copy popup evidence requires its explicit v4 identity")
        return False
    if (data.get("schema") != "termforge-window-selection-v4" or data.get("system_clipboard") is not True or
            data.get("copy_popup_verified") is not True or data.get("copy_popup_observer_idle") is not True or
            any(type(data.get(field)) is not int or not 1 <= data[field] <= 0xffffffff for field in fields[2:4]) or
            data["copy_popup_sequence_before"] == data["copy_popup_sequence_after"]):
        raise ValueError("actual Copy popup clipboard replacement or observer cleanup is unverified")
    _validate_popup(data.get("copy_popup_menu"), 14)
    rows = data.get("interactions")
    if not isinstance(rows, list) or any(not isinstance(row, dict) for row in rows):
        raise ValueError("actual Copy popup lacks native before/after target observations")
    before = [row for row in rows if row.get("phase") == "system-copy-popup-prepared"]
    after = [row for row in rows if row.get("phase") == "system-copy-popup"]
    if len(before) != 1 or len(after) != 1:
        raise ValueError("actual Copy popup before/after phases are missing or repeated")
    all_text = "\r\n".join([f"TF_FIND_{index:03d} needle 中文 e\u0301" for index in range(80)] + ["TF_FIND_READY 080", ""])
    for row, action, cf in ((before[0], "prepare-Copy-popup", SENTINEL), (after[0], "actual-Session-popup-Copy", all_text)):
        if (row.get("action") != action or row.get("clipboard") != cf or row.get("text") != all_text or
                any(row.get(field) is not True for field in ("main_focused", "main_active", "selected", "frame_matches_full")) or
                row.get("raw_bottom") != "" or row.get("raw_main") != "x" or row.get("status") != "ok" or
                row.get("captured") is not False or type(row.get("overlay_cells")) is not int or row["overlay_cells"] <= 0):
            raise ValueError("actual Copy popup changed selection/focus/target/raw input or copied the wrong literal CF")
    if after[0].get("product_status") != "Selected text copied":
        raise ValueError("native Copy popup did not complete the actual product copy action")
    return True


def _validate_interactions(data: dict) -> dict:
    rows = data.get("interactions")
    system = data["system_clipboard"]
    actual_copy_popup = _validate_copy_popup(data)
    phases = INTERACTIONS + ("empty-copy-shortcut", "select-all-popup") + (("system-copy-shortcut",) if system else ()) + (
        ("system-copy-popup-prepared", "system-copy-popup") if actual_copy_popup else ()) + (
        "select-all-popup-cleared", "input-after-popup")
    if (data.get("interaction_input") != "queued-keys-through-workspace-loop-and-native-timer-capture" or
            data.get("menu_popup_verified") is not True or data.get("popup_observer_idle") is not True or
            not isinstance(rows, list) or len(rows) != len(phases) or
            any(not isinstance(row, dict) for row in rows) or [row.get("phase") for row in rows] != list(phases)):
        raise ValueError("selection timer/capture/queued-shortcut/actual popup phases are incomplete")
    _validate_popup(data.get("popup_menu"))
    lines = [f"TF_FIND_{index:03d} needle 中文 e\u0301" for index in range(80)] + ["TF_FIND_READY 080", ""]
    all_text = "\r\n".join(lines)
    for index, row in enumerate(rows):
        phase = phases[index]
        integers = ("ticks", "top", "maximum", "page", "scrollbar_top", "anchor_line", "direction", "overlay_cells")
        flags = ("captured", "capture_transferred", "main_focused", "main_active", "selected", "frame_matches_full")
        if any(type(row.get(field)) is not int for field in integers) or any(type(row.get(field)) is not bool for field in flags):
            raise ValueError("invalid selection interaction counter or native-state observation type")
        if (row["ticks"] <= 0 or not 1 <= row["page"] <= 82 or row["maximum"] != len(lines) - row["page"] or
                not 0 <= row["top"] <= row["maximum"] or row["scrollbar_top"] != row["top"] or
                row.get("visible_lines") != lines[row["top"]:row["top"] + row["page"]] or
                row["frame_matches_full"] is not True or not 0 <= row["overlay_cells"] <= row["page"] * 500):
            raise ValueError("autoscroll did not expose independently expected actual fixture rows or pixels")
        if index < 12:
            anchor, direction = (75, -1) if index < 4 else (50, 1) if index < 8 else (40, -1)
            first, last = (row["top"], anchor) if direction < 0 else (anchor, row["top"] + row["page"] - 1)
            if not 0 <= first < last <= 80:
                raise ValueError("autoscroll selection endpoint is outside the literal fixture")
            expected_text = "".join(line + "\r\n" for line in lines[first:last])
            if row["anchor_line"] != anchor or row["direction"] != direction:
                raise ValueError("autoscroll direction or drag anchor changed")
        else:
            expected_text = all_text if phase in ("select-all-shortcut", "select-all-command", "select-all-popup", "system-copy-shortcut",
                                                  "system-copy-popup-prepared", "system-copy-popup") else ""
        held = phase.endswith(("-start", "-held"))
        main_focused = phase not in ("autoscroll-focus-release", "autoscroll-focus-stopped")
        action = {"autoscroll-up-release": "mouse-up", "autoscroll-focus-release": "focus-loss",
                  "autoscroll-capture-release": "capture-loss", "select-all-shortcut": "queued-Ctrl-Shift-A",
                  "select-all-shortcut-cleared": "queued-Escape", "select-all-command": "workspace-command-15",
                  "select-all-command-cleared": "queued-Escape", "empty-copy-shortcut": "queued-Ctrl-Shift-C-empty",
                  "select-all-popup": "actual-Session-popup-Select-All", "system-copy-shortcut": "queued-Ctrl-Shift-C",
                  "system-copy-popup-prepared": "prepare-Copy-popup", "system-copy-popup": "actual-Session-popup-Copy",
                  "select-all-popup-cleared": "queued-Escape"}.get(phase, "none")
        if (row["captured"] != held or row["capture_transferred"] != (phase == "autoscroll-capture-release") or
                row["main_focused"] != main_focused or row["main_active"] != main_focused or row.get("action") != action or
                row["selected"] != bool(expected_text) or row.get("status") != ("ok" if expected_text else "empty") or
                row.get("text") != expected_text or bool(row["overlay_cells"]) != bool(expected_text) or
                row.get("raw_bottom") != "" or row.get("raw_main") != ("x" if index >= 16 else "")):
            raise ValueError("selection interaction text/focus/capture/raw-byte oracle differs")
        expected_cf = (SENTINEL if phase in ("empty-copy-shortcut", "system-copy-popup-prepared") else all_text) if system and phase in (
            "empty-copy-shortcut", "system-copy-shortcut", "system-copy-popup-prepared", "system-copy-popup") else None
        if row.get("clipboard") != expected_cf:
            raise ValueError("real queued Copy shortcut CF_UNICODETEXT differs from its exact independent oracle")
        if phase == "empty-copy-shortcut" and row.get("product_status") != "No selected text":
            raise ValueError("empty Copy shortcut did not take the no-selection product path")
        if phase == "system-copy-shortcut" and row.get("product_status") != "Selected text copied":
            raise ValueError("queued Copy shortcut did not complete the actual copy action")
    for offset, direction in ((0, -1), (4, 1), (8, -1)):
        start, held, release, stopped = rows[offset:offset + 4]
        ticks = held["ticks"] - start["ticks"]
        if (not 3 <= ticks <= 12 or held["top"] - start["top"] != ticks * direction or
                not 0 < held["top"] < held["maximum"] or
                release["top"] != held["top"] or release["ticks"] != held["ticks"] or
                stopped["ticks"] - release["ticks"] < 3 or stopped["top"] != release["top"] or
                not held["text"] == release["text"] == stopped["text"]):
            raise ValueError("autoscroll moved outside its per-timer bound or continued after mouse/focus/capture release")
    return {"native_timer_autoscroll": True, "stop_reasons": ["mouse-up", "focus-loss", "capture-loss"],
            "select_all_shortcut": True, "select_all_command": True, "actual_menu_popup": True,
            "empty_copy_shortcut": True, "system_copy_shortcut": system, "actual_copy_popup": actual_copy_popup, "phases": len(rows)}


def validate_selection(data: dict, system_clipboard: bool, run_record: dict) -> dict:
    if (not isinstance(data, dict) or data.get("schema") not in ("termforge-window-selection-v3", "termforge-window-selection-v4") or
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
    interactions = _validate_interactions(data)
    return {"system_clipboard": system_clipboard, "phases": len(PHASES), "sessions_closed": result["sessions_closed"],
            "cleanup_verified": True, "observations": final, "interactions": interactions}


def validate_selection_workflow(directory: Path, system_clipboard: bool, run_record: dict) -> dict:
    directory = Path(directory)
    data = json.loads((directory / "selection.json").read_text(encoding="utf-8"))
    summary = validate_selection(data, system_clipboard, run_record)
    _validate_popup(json.loads((directory / "selection-popup.json").read_text(encoding="utf-8")))
    if summary["interactions"]["actual_copy_popup"]:
        menu = json.loads((directory / "selection-copy-popup.json").read_text(encoding="utf-8"))
        _validate_popup(menu, 14)
        if menu != data["copy_popup_menu"]:
            raise ValueError("actual native Copy popup journal differs from the final manifest")
    if (directory / "selection-bottom.bin").read_bytes() != b"" or (directory / "selection-main.bin").read_bytes() != b"x":
        raise ValueError("queued Select All/Escape leaked bytes or input reached the wrong raw receiver")
    return summary
