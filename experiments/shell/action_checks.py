"""Literal menu, focus, input and cleanup oracles for the R1 action slice."""
from __future__ import annotations

import json
import hashlib
from pathlib import Path

BASE_COMMON = ("ready", "terminal-empty", "empty-copy-rejected", "context-all", "context-find", "find-editor",
          "shortcut-find", "history-top", "history-bottom", "rename-editor", "busy-prompt", "busy-shortcut-rejected", "busy-terminal",
          "busy-session", "busy-cancelled")
GEOMETRY = ("geometry-144", "geometry-192", "geometry-96", "geometry-zoom75", "geometry-restored")
COMMON = BASE_COMMON[:5] + GEOMETRY + BASE_COMMON[5:]
SYSTEM = ("system-copy", "system-shift-insert", "system-default-enter-prompt", "system-default-enter-accepted", "system-paste-prompt", "system-target-switched", "system-confirmed")
FINAL = ("live-selected", "invalidated-copy-rejected", "ordinary-input", "closing", "no-session", "empty-session-menu")
ALL_TEXT = "".join(f"TF_FIND_{index:03d} needle 中文 e\u0301\r\n" for index in range(80)) + "TF_FIND_READY 080\r\n"
SHIFT = "SHIFT 中文 e\u0301"
DEFAULT = "DEFAULT_ENTER 中文\rNEXT"
PASTE = "CTX 中文 e\u0301\rNEXT"
ACTIONS = {"copy", "paste", "all", "find", "top", "bottom", "cancel"}
ENTRIES = dict(zip(BASE_COMMON, ("none", "terminal-context-inspect", "queued-Ctrl-Shift-C-empty", "terminal-context-all",
    "terminal-context-find", "native-edit-Ctrl-A-char-and-Ctrl-Shift-F-refocus", "queued-Ctrl-Shift-F", "queued-Ctrl-Shift-Home",
    "queued-Ctrl-Shift-End", "native-rename-Ctrl-A-and-char", "synthetic-prompt-state-fixture", "queued-Ctrl-Shift-V-busy", "terminal-context-inspect",
    "session-menu-inspect", "session-menu-cancel"))) | dict(zip(SYSTEM, ("terminal-context-copy", "queued-Shift-Insert",
    "terminal-context-default-paste", "queued-default-Enter", "terminal-context-paste", "tab-switch-during-prompt", "queued-prompt-Tab-Enter"))) | dict(zip(FINAL, (
    "mouse-drag-live-prefix", "public-copy-invalidated", "queued-plain-character", "close-focused-session", "none", "session-menu-inspect")))
ENTRIES.update({phase: "controlled-DPI-with-Find-selection" if phase in GEOMETRY[:3] else "session-zoom-with-Find-selection" for phase in GEOMETRY})


def _check(condition, message):
    if not condition:
        raise ValueError(message)


def _popup(value, command, terminal, enabled):
    _check(isinstance(value, dict) and value.get("schema") == "termforge-owned-popup-menu-v1" and
           value.get("target_command") == command and value.get("actual_return_command") == command and
           type(value.get("target_command")) is int and type(value.get("actual_return_command")) is int and value.get("inspection_only") is (command == 0),
           "action did not return from the requested actual popup command")
    expected_path = "queued-context-message-and-popup-keys" if terminal else "queued-owner-mouse-and-popup-keys"
    _check(value.get("opening_message") == ("WM_CONTEXTMENU" if terminal else "WM_LBUTTONDOWN") and
           value.get("input_path") == expected_path and value.get("owned_non_input_desktop") is True and
           value.get("popup_class") == "#32768" and type(value.get("popup_hwnd")) is int and value["popup_hwnd"] > 0 and
           all(value.get(flag) is True for flag in ("popup_visible_observed", "returned", "popup_closed")) and
           all(value.get(flag) is False for flag in ("timed_out", "escape_cleanup", "endmenu_cleanup")) and
           value.get("failure") is None, "actual popup visibility, input path or bounded cleanup failed")
    if command:
        _check(value.get("highlighted") is True and type(value.get("navigation_keys")) is int and
               value["navigation_keys"] > 0 and value.get("initial_state", 3) & 3 == 0 and
               value.get("highlighted_state", 0) & 0x80 and value["highlighted_state"] & 3 == 0,
               "actual popup did not visibly highlight an enabled command")
    _check(isinstance(value.get("enabled_commands"), list) and all(type(command) is int for command in value["enabled_commands"]) and set(value["enabled_commands"]) == set(enabled) and
           len(value["enabled_commands"]) == len(set(enabled)), "actual popup availability differs from the independent contract")
    for field in ("popup_rect", "item_rect"):
        rect = value.get(field)
        _check(isinstance(rect, list) and len(rect) == 4 and all(type(n) is int for n in rect) and
               rect[0] < rect[2] and rect[1] < rect[3], "actual popup geometry was not observed")


def validate_actions(data, system, record):
    _check(isinstance(data, dict) and data.get("schema") == "termforge-window-actions-v1" and
           data.get("fixture") == "shell_text_client-80" and data.get("system_clipboard") is system,
           "action evidence identity differs")
    mode = "ui-actions-system" if system else "ui-actions"
    result = record.get("result")
    if result is None:
        try:
            result = json.loads(record.get("stdout", ""))
        except (TypeError, ValueError):
            result = None
    _check(result == {"valid": True, "mode": mode, "sessions_closed": 2, "active_children_after": 0, "presented_done": None} and
           result.get("valid") is True and type(result.get("sessions_closed")) is int and type(result.get("active_children_after")) is int and
           record.get("timed_out") is False and type(record.get("exit_code")) is int and record["exit_code"] == 0 and
           record.get("desktop_gone") is True and type(record.get("job_after", {}).get("active")) is int and record["job_after"]["active"] == 0 and not record.get("cleanup_errors"),
           "action session, native result, owned Job or desktop cleanup failed")
    phases = COMMON + (SYSTEM if system else ()) + FINAL
    rows = data.get("snapshots")
    _check(isinstance(rows, list) and all(isinstance(row, dict) for row in rows) and
           [row.get("phase") for row in rows] == list(phases), "action snapshots are incomplete or reordered")
    previous_ticks = {}
    previous_generations = {}
    baseline_find = baseline_cell = None
    for index, row in enumerate(rows):
        phase = row["phase"]
        _check(row.get("entry") == ENTRIES[phase], "action entry path differs from its original observation")
        dpi = 144 if phase == "geometry-144" else 192 if phase == "geometry-192" else 96
        _check(type(row.get("controlled_dpi")) is int and row["controlled_dpi"] == dpi and
               type(row.get("os_window_dpi")) is int and 48 <= row["os_window_dpi"] <= 768 and
               row.get("logical_client") == [1360, 860] and row.get("physical_client") == [1360*dpi//96, 860*dpi//96],
               "controlled DPI geometry did not preserve its independently specified logical client")
        count = 0 if phase in ("no-session", "empty-session-menu") else 2
        _check(row.get("session_count") == count and type(row.get("session_count")) is int and
               isinstance(row.get("sessions"), list) and len(row["sessions"]) == count,
               "action session target disappeared or a hidden replacement was created")
        state = row.get("workspace_actions")
        _check(isinstance(state, dict) and set(state) == ACTIONS and all(isinstance(value, dict) and
               type(value.get("enabled")) is bool and isinstance(value.get("reason"), str) for value in state.values()),
               "public action availability was not observed")
        block = "no-session" if not count else "closing" if phase == "closing" else "native-editor" if phase == "rename-editor" else None
        for action in ACTIONS:
            reason = block or ("no-selection" if action == "copy" and phase not in ("context-all", "context-find", "find-editor", "system-copy", "system-target-switched", "system-confirmed", "live-selected") + GEOMETRY else
                               "paste-busy" if action == "paste" and phase in ("busy-prompt", "busy-shortcut-rejected", "busy-terminal", "busy-session", "system-default-enter-prompt", "system-paste-prompt") else
                               "no-paste" if action == "cancel" and phase not in ("busy-prompt", "busy-shortcut-rejected", "busy-terminal", "busy-session", "system-default-enter-prompt", "system-paste-prompt") else "none")
            _check(state[action] == {"enabled": reason == "none", "reason": reason}, f"action availability reason differs: {phase}/{action}")
        expected_main = (SHIFT if phase in ("system-shift-insert", "system-default-enter-prompt") else
                         SHIFT + DEFAULT if phase in ("system-default-enter-accepted", "system-paste-prompt", "system-target-switched") else
                         SHIFT + DEFAULT + PASTE if phase == "system-confirmed" or system and phase in FINAL else "")
        if phase in FINAL:
            expected_main += "~" if phase == "live-selected" else "~-" if phase == "invalidated-copy-rejected" else "~-x"
        _check(row.get("raw") == {"bottom": "", "main": expected_main}, "action key, rename, selection or paste bytes leaked to a different PTY")
        cf = ALL_TEXT if phase == "system-copy" else SHIFT if phase == "system-shift-insert" else "DEFAULT_ENTER 中文\r\nNEXT" if phase in ("system-default-enter-prompt", "system-default-enter-accepted") else "CTX 中文 e\u0301\r\nNEXT" if phase == "system-paste-prompt" else None
        _check(row.get("clipboard") == cf, "actual system clipboard or local CF isolation differs")
        expected_focus = ("other" if not count else "rename" if phase == "rename-editor" else "bottom-find" if phase in ("context-find", "find-editor") else
                          "main-find" if phase == "shortcut-find" else "main-prompt" if phase in ("busy-prompt", "system-default-enter-prompt", "system-paste-prompt") else
                          "bottom" if phase in ("ready", "terminal-empty", "empty-copy-rejected", "context-all", "system-copy", "system-target-switched", "system-confirmed") else "bottom-find" if phase in GEOMETRY else "main")
        _check(row.get("focus") == expected_focus and row.get("rename_text") == ("R" if phase == "rename-editor" else None),
               "native EDIT, prompt or terminal focus was stolen")
        for session_index, session in enumerate(row["sessions"]):
            group = ("bottom", "main")[session_index]
            _check(session.get("group") == group and all(type(session.get(flag)) is bool for flag in
                   ("closing", "active", "focused", "selected", "paste_busy", "prompt", "find", "frame_matches_full")) and
                   session["frame_matches_full"] is True, "action dirty frame or session observations failed")
            active_group = "bottom" if expected_focus.startswith("bottom") else "main"
            _check(session["active"] is (group == active_group) and session["focused"] is (expected_focus == group),
                   "workspace focus and action target do not refer to the observed session")
            selected = group == "bottom" and index >= phases.index("context-all") or group == "main" and phase == "live-selected"
            selection = ALL_TEXT if selected and group == "bottom" else "TF_STREAM_TICK_" if selected else ""
            selection_status = "ok" if selected else "invalidated" if group == "main" and phase in FINAL[1:] else "empty"
            _check(session["selected"] is selected and session.get("selection") == selection and session.get("selection_status") == selection_status,
                   "action changed or borrowed another session's terminal selection")
            zoom = 75 if group == "bottom" and phase == "geometry-zoom75" else 100
            _check(type(session.get("display_dpi")) is int and session["display_dpi"] == dpi and
                   type(session.get("zoom_percent")) is int and session["zoom_percent"] == zoom and
                   type(session.get("overlay_cells")) is int and session["overlay_cells"] >= 0,
                   "session DPI, zoom or actual overlay observation differs")
            if selected:
                _check(session["overlay_cells"] > 0, "actual selected session lost its visible selection overlay")
            for field in ("grid", "cell_pixels"):
                _check(isinstance(session.get(field), list) and len(session[field]) == 2 and all(type(value) is int and value > 0 for value in session[field]),
                       "session actual grid/cell dimensions are missing")
            if group == "main" and phase == "invalidated-copy-rejected":
                _check(session.get("status") == "Selected text changed; select it again", "invalidated Copy hid the changed-text diagnostic")
            if group == "bottom" and phase == "empty-copy-rejected":
                _check(session.get("status") == "No selected text", "empty Copy did not reject without CF")
            if group == "main" and phase == "busy-shortcut-rejected":
                _check(session.get("status") == "Paste already in progress", "busy Paste shortcut did not reject before reading CF")
            _check(session["closing"] is (phase == "closing" and group == "main"), "closing target was not observed before collection")
            old = previous_ticks.get(group, -1)
            _check(type(session.get("ticks")) is int and session["ticks"] >= old and
                   type(session.get("top")) is int and type(session.get("maximum")) is int and 0 <= session["top"] <= session["maximum"],
                   "action timer or retained viewport observations are invalid")
            previous_ticks[group] = session["ticks"]
            generation, ranges = session.get("search_generation"), session.get("valid_find_ranges")
            _check(type(generation) is int and generation >= previous_generations.get(group, 0) and
                   isinstance(ranges, list) and len(ranges) <= 1024 and all(isinstance(value, dict) and
                   set(value) == {"id", "version", "begin", "end"} and all(type(number) is int for number in value.values()) and
                   value["id"] > 0 and value["version"] > 0 and 0 <= value["begin"] < value["end"] <= 500 for value in ranges),
                   "bounded live Find ranges/generation are invalid")
            previous_generations[group] = generation
            if session.get("find_range") is not None:
                _check(session["find_range"] in ranges, "current Find match was not among actually live result ranges")
            if group == "main" and phase == "history-top":
                _check(session["top"] == 0 and session["maximum"] > 0, "Ctrl-Shift-Home did not reach retained history top")
            if group == "main" and phase == "history-bottom":
                _check(session["top"] == session["maximum"] and session["maximum"] > 0, "Ctrl-Shift-End did not reach actual bottom")
            query = "TF_FIND_077" if group == "bottom" and phase in ("context-find",) + GEOMETRY else "q" if group == "bottom" and phase == "find-editor" else "TF_FIND_075" if group == "main" and phase == "shortcut-find" else None
            _check(session.get("query") == query and session["find"] is (query is not None), "Find shortcut/editor used a different session/query")
            if group == "bottom" and phase == "find-editor":
                refocus = row.get("find_refocus")
                _check(isinstance(refocus, dict) and refocus.get("input_path") == "queued-Ctrl-Shift-F-in-Find-EDIT" and
                       refocus.get("query_before") == "z" and refocus.get("selected") == "z" and refocus.get("focused") is True and
                       refocus.get("before") == [1, 1] and refocus.get("after") == [0, 1] and
                       all(type(value) is int for field in ("before", "after") for value in refocus[field]),
                       "Find refocus did not select the actual existing query before ordinary replacement")
            if phase in ("context-find", "shortcut-find") + GEOMETRY and query:
                _check(session.get("find_line") == query + " needle 中文 e\u0301", "actual Find jump did not show the independent matching line")
                match = session.get("find_range")
                _check(isinstance(match, dict) and all(type(match.get(name)) is int for name in ("id", "version", "begin", "end")) and
                       match["id"] > 0 and match["version"] > 0 and match["begin"] == 0 and match["end"] == 11,
                       "current Find match is not the literal fixture word")
                geometry = session.get("find_geometry")
                _check(isinstance(geometry, dict) and type(geometry.get("font_height")) is int and geometry["font_height"] == -(9*dpi//72) and
                       type(geometry.get("os_dpi")) is int and geometry["os_dpi"] > 0,
                       "actual Find EDIT font/OS geometry was not recorded")
                for field in ("window", "edit"):
                    bounds = geometry.get(field)
                    _check(isinstance(bounds, list) and len(bounds) == 4 and all(type(value) is int for value in bounds) and
                           bounds[0] < bounds[2] and bounds[1] < bounds[3], "Find/EDIT window bounds are invalid")
                outer, inner = geometry["window"], geometry["edit"]
                _check(outer[0] <= inner[0] < inner[2] <= outer[2] and outer[1] <= inner[1] < inner[3] <= outer[3], "Find EDIT escaped its actual parent geometry")
                if phase == "context-find":
                    baseline_find = match.copy(); baseline_cell = session["cell_pixels"][:]
                elif phase in GEOMETRY:
                    _check(match["id"] == baseline_find["id"] and session["overlay_cells"] > 0,
                           "controlled geometry lost the selected text/current match identity or visible selection overlay")
                    if phase in ("geometry-96", "geometry-restored"):
                        _check(session["cell_pixels"] == baseline_cell, "DPI/zoom restoration changed original cell metrics")
                    elif phase == "geometry-zoom75":
                        _check(all(value < before for value, before in zip(session["cell_pixels"], baseline_cell)), "zoom did not change actual cell metrics")
                    else:
                        _check(all(value > before for value, before in zip(session["cell_pixels"], baseline_cell)), "controlled DPI did not change actual cell metrics")
            busy = group == "main" and phase in ("busy-prompt", "busy-shortcut-rejected", "busy-terminal", "busy-session", "system-default-enter-prompt", "system-paste-prompt", "system-target-switched")
            _check(session["paste_busy"] is busy and session["prompt"] is busy, "paste confirmation/busy state changed or sent early")
            _check(type(session.get("prompt_focus")) is int and session["prompt_focus"] == (1 if group == "main" and phase in ("busy-prompt", "system-default-enter-prompt", "system-paste-prompt") else 0),
                   "actual prompt default focus was not the Paste button or escaped the observed control")
        popup = row.get("popup")
        popup_contract = {
            "terminal-empty": (0, True, (2, 3, 4)), "context-all": (3, True, (2, 3, 4)),
            "context-find": (4, True, (1, 2, 3, 4)), "busy-terminal": (0, True, (3, 4)),
            "busy-session": (0, False, (8, 1, 2, 6, 5, 13, 15, 16)),
            "busy-cancelled": (13, False, (8, 1, 2, 6, 5, 13, 15, 16)),
            "system-copy": (1, True, (1, 2, 3, 4)), "system-paste-prompt": (2, True, (2, 3, 4)),
            "system-default-enter-prompt": (2, True, (2, 3, 4)),
            "empty-session-menu": (0, False, (8, 1, 2, 6, 5)),
        }
        if phase in popup_contract:
            _popup(popup, *popup_contract[phase])
        else:
            _check(popup is None, "another action borrowed stale popup evidence")
        overwrite = row.get("overwrite")
        _check(isinstance(overwrite, dict) and set(overwrite) == {"before_id", "before_version", "after_id", "after_version"} and
               all(type(value) is int and value >= 0 for value in overwrite.values()), "actual overwrite identities are missing")
        if phase in FINAL:
            _check(overwrite["before_id"] > 0 and overwrite["before_version"] > 0 and
                   (overwrite["after_id"] == 0 and overwrite["after_version"] == 0 if phase == "live-selected" else
                    overwrite["after_id"] == overwrite["before_id"] and overwrite["after_version"] > overwrite["before_version"]),
                   "Copy invalidation was not an actual same-line version change")
        else:
            _check(not any(overwrite.values()), "action reused an unrelated overwrite identity")
    return {"phases": len(phases), "actual_terminal_popups": 7 if system else 4, "actual_session_popups": 3,
            "system_clipboard": system, "raw_input_isolated": True, "cleanup_verified": True,
            "complete_r1_07": False}


def validate_actions_workflow(directory, system, record):
    directory = Path(directory)
    data = json.loads((directory / "actions.json").read_bytes())
    summary = validate_actions(data, system, record)
    journal = [json.loads(line) for line in (directory / "actions-progress.jsonl").read_bytes().splitlines()]
    _check(journal == data["snapshots"], "action final manifest differs from its original ordered journal")
    expected = (SHIFT + DEFAULT + PASTE if system else "") + "~-x"
    _check((directory / "actions-bottom.bin").read_bytes() == b"" and
           (directory / "actions-main.bin").read_bytes() == expected.encode("utf-8"), "independent action receiver files differ")
    transport = (directory / "actions-overwrite.transport.bin").read_bytes()
    _check(len(transport) <= 16384 and b"TF_STREAM_STARTED" in transport and b"TF_STREAM_TICK_" in transport and
           b"TF_STREAM_STOPPED" in transport and transport.index(b"TF_STREAM_STARTED") < transport.index(b"TF_STREAM_TICK_") < transport.index(b"TF_STREAM_STOPPED"),
           "actual overwrite transport does not support selected-row invalidation")
    for group in ("bottom", "main"):
        _check((directory / f"actions-geometry-{group}.transport.bin").stat().st_size <= 65536, "controlled geometry transport exceeded its bound")
    summary["geometry_frames_sha256"] = {}
    for row in data["snapshots"]:
        if row["phase"] not in GEOMETRY:
            continue
        frame = (directory / (row["phase"] + ".bmp")).read_bytes()
        width, height = row["physical_client"]
        _check(len(frame) == 54 + width*height*4 and frame[:2] == b"BM" and int.from_bytes(frame[2:6], "little") == len(frame) and
               int.from_bytes(frame[10:14], "little") == 54 and int.from_bytes(frame[14:18], "little") == 40 and
               int.from_bytes(frame[18:22], "little", signed=True) == width and int.from_bytes(frame[22:26], "little", signed=True) == -height and
               int.from_bytes(frame[26:28], "little") == 1 and int.from_bytes(frame[28:30], "little") == 32 and int.from_bytes(frame[30:34], "little") == 0,
               "actual controlled geometry frame dimensions or pixel payload differ")
        summary["geometry_frames_sha256"][row["phase"]] = hashlib.sha256(frame).hexdigest()
    return summary
