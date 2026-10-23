"""Independent literal, stable-position and raw-byte gates for the actual find UI."""
from __future__ import annotations

import json
from pathlib import Path

PHASES = ("initial-last", "selection-coexists", "previous-78", "next-79", "wrap-next-0", "wrap-previous-79",
          "case-sensitive-none", "case-fold-last", "unicode-replacement", "no-result", "main-active",
          "background-append", "evicted", "escape-before-input", "input-after-escape")
BOTTOM_CURRENT = (79, 79, 78, 79, 0, 79, None, 79, 79, None, 79, 79, None, None, None)


def _line(number: int) -> str:
    return f"TF_FIND_{number:03d} needle 中文 e\u0301"


def validate_find(data: dict, run_record: dict) -> dict:
    if (not isinstance(data, dict) or data.get("schema") != "termforge-window-find-v1" or
            data.get("input_path") != "queued-WM_KEYDOWN-through-workspace-loop" or
            data.get("fixture") != "shell_text_client-v1-80-plus20-evict500" or
            type(data.get("initial_running_checks")) is not int or data["initial_running_checks"] <= 0 or
            not isinstance(data.get("snapshots"), list) or len(data["snapshots"]) != len(PHASES) or
            any(not isinstance(row, dict) for row in data["snapshots"]) or
            [row.get("phase") for row in data["snapshots"]] != list(PHASES)):
        raise ValueError("find UI phases, fixture or actual queued input path are incomplete")
    if not isinstance(run_record, dict):
        raise ValueError("find requires collector process and cleanup observations")
    result = run_record.get("result")
    if result is None:
        try:
            result = json.loads(run_record.get("stdout", ""))
        except (ValueError, TypeError):
            raise ValueError("find collector lacks its native result") from None
    if (not isinstance(result, dict) or result.get("valid") is not True or result.get("mode") != "ui-find" or
            type(result.get("sessions_closed")) is not int or result["sessions_closed"] != 2 or
            type(result.get("active_children_after")) is not int or result["active_children_after"] != 0 or
            "presented_done" not in result or result["presented_done"] is not None or
            run_record.get("timed_out") is not False or type(run_record.get("exit_code")) is not int or
            run_record["exit_code"] != 0 or run_record.get("desktop_gone") is not True or
            not isinstance(run_record.get("job_after"), dict) or
            type(run_record["job_after"].get("active")) is not int or run_record["job_after"]["active"] != 0 or
            run_record.get("cleanup_errors")):
        raise ValueError("find workflow, two-session closure, process Job or desktop cleanup failed")
    bottom_79 = main_5 = reading_anchor = None
    observations = {}
    for index, row in enumerate(data["snapshots"]):
        phase = PHASES[index]
        expected_bottom_capture = "" if index < PHASES.index("background-append") else "+" if phase == "background-append" else "+#"
        expected_main_capture = "x" if phase == "input-after-escape" else ""
        if row.get("captures") != {"bottom": expected_bottom_capture, "main": expected_main_capture}:
            raise ValueError(f"find text/navigation/Escape leaked or fixture input was reordered: {phase}")
        sessions = row.get("sessions")
        if (not isinstance(sessions, list) or len(sessions) != 2 or any(not isinstance(s, dict) for s in sessions) or
                [s.get("group") for s in sessions] != ["bottom", "main"]):
            raise ValueError("find requires observations from both actual session regions")
        focus = "main" if index >= PHASES.index("main-active") else "bottom"
        for session in sessions:
            bottom = session["group"] == "bottom"
            exists = bottom or PHASES.index("main-active") <= index < PHASES.index("escape-before-input")
            terminal_focus = session["group"] == focus and index >= PHASES.index("escape-before-input")
            find_focus = session["group"] == focus and not terminal_focus
            edit_focus = find_focus and phase != "case-fold-last"
            focus_control = (4101 if edit_focus else 4105 if find_focus else 0)
            query = (("NEEDLE" if phase in ("case-sensitive-none", "case-fold-last") else
                      "中文" if phase == "unicode-replacement" else "TF_FIND_MISSING" if phase == "no-result" else "needle")
                     if bottom else "TF_FIND_005" if exists else None)
            expected_case = not bottom or index < PHASES.index("case-fold-last")
            expected_status = "complete" if bottom or exists else "cancelled" if index >= PHASES.index("escape-before-input") else "idle"
            boolean = ("workspace_active", "terminal_focused", "edit_focused", "find_focused", "find_exists", "find_visible",
                       "case_sensitive", "frame_matches_full", "append_ready", "evict_ready")
            if (any(type(session.get(field)) is not bool for field in boolean) or
                    session["workspace_active"] != (session["group"] == focus) or
                    session["terminal_focused"] != terminal_focus or session["edit_focused"] != edit_focus or
                    session["find_focused"] != find_focus or type(session.get("focus_control")) is not int or session["focus_control"] != focus_control or
                    session["find_exists"] != exists or session["find_visible"] != exists or
                    session["case_sensitive"] != expected_case or session.get("query") != query or
                    session.get("status") != expected_status or session["frame_matches_full"] is not True):
                raise ValueError(f"find actual controls/query/focus/case/lifecycle mismatch: {phase}/{session['group']}")
            integer = ("yellow_cells", "blue_cells", "top", "total", "page", "maximum", "anchor")
            if (any(type(session.get(field)) is not int for field in integer) or
                    not 1 <= session["page"] <= 200 or not 0 <= session["maximum"] <= 100 or
                    session["total"] != session["page"] + session["maximum"] or
                    not 0 <= session["top"] <= session["maximum"] or session["anchor"] <= 0 or
                    not 0 <= session["yellow_cells"] <= session["page"] * 500 or
                    not 0 <= session["blue_cells"] <= session["page"] * 500):
                raise ValueError("find viewport, overlay or retained resource bounds failed")
            lines = session.get("visible_lines")
            if (not isinstance(lines, list) or len(lines) != session["page"] or
                    any(not isinstance(value, dict) or type(value.get("id")) is not int or value["id"] <= 0 or
                        type(value.get("version")) is not int or value["version"] <= 0 or
                        not isinstance(value.get("text"), str) for value in lines)):
                raise ValueError("find actual visible line identities/content missing")
            if any(a["id"] >= b["id"] for a, b in zip(lines, lines[1:])):
                raise ValueError("find visible stable rows are not in retained order")
            number = BOTTOM_CURRENT[index] if bottom else 5 if exists else None
            current = session.get("current")
            if number is None:
                if current is not None or session["yellow_cells"] != 0 or session.get("current_cells") != []:
                    raise ValueError("no-result/cancelled find retains an actual match/highlight")
            else:
                begin, end = (19, 23) if bottom and phase == "unicode-replacement" else (12, 18) if bottom else (0, 11)
                if (not isinstance(current, dict) or
                        any(type(current.get(field)) is not int or current[field] <= 0 for field in
                            ("line_id", "version", "end_line_id", "end_version")) or
                        current["line_id"] != current["end_line_id"] or current["version"] != current["end_version"] or
                        type(current.get("begin")) is not int or current["begin"] != begin or
                        type(current.get("end")) is not int or current["end"] != end or current.get("line") != _line(number) or
                        not any(value == {"id": current["line_id"], "version": current["version"], "text": _line(number)} for value in lines)):
                    raise ValueError(f"find literal match, stable cell mapping or actual revealed row wrong: {phase}")
                cells = session.get("current_cells")
                if (not isinstance(cells, list) or not end <= len(cells) <= 500 or
                        any(not isinstance(cell, dict) or type(cell.get("column")) is not int or cell["column"] != column or
                            type(cell.get("bg")) is not int or type(cell.get("fg")) is not int for column, cell in enumerate(cells)) or
                        any(cells[column]["bg"] != 11 or cells[column]["fg"] != 0 for column in range(begin, end))):
                    raise ValueError("current search hit is not actually rendered in the independent yellow style")
                identity = current["line_id"], current["version"]
                if bottom and number == 79:
                    if bottom_79 is not None and identity != bottom_79:
                        raise ValueError("unchanged historical match lost its stable identity/version")
                    bottom_79 = identity
                if not bottom:
                    if main_5 is not None and identity != main_5:
                        raise ValueError("background output changed the other session's current match")
                    main_5 = identity
            expected_selection = "TF_FIND_079" if bottom and 1 <= index < PHASES.index("evicted") else ""
            if session.get("selected_text") != expected_selection:
                raise ValueError("find replaced or corrupted the independent selection")
        bottom, main = sessions
        if phase == "selection-coexists":
            if bottom["blue_cells"] < 11 or any(cell["bg"] != 4 or cell["fg"] != 15 for cell in bottom["current_cells"][:11]):
                raise ValueError("independent blue selection is missing beside yellow search highlight")
        if phase == "background-append" and not bottom["append_ready"]:
            raise ValueError("background append lacks an actual 100-row fixture completion marker")
        if phase == "main-active":
            if bottom["top"] >= bottom["maximum"]:
                raise ValueError("background append requires an explicitly paused reading position")
            reading_anchor = bottom["top"], bottom["anchor"]
        if phase == "background-append" and (bottom["top"], bottom["anchor"]) != reading_anchor:
            raise ValueError("background append pulled the stable paused find position away")
        if phase == "evicted" and (not bottom["evict_ready"] or bottom["maximum"] != 100 or
                                    bottom["blue_cells"] != 0 or any("TF_FIND_" in line["text"] for line in bottom["visible_lines"])):
            raise ValueError("500-row eviction did not invalidate old find/selection content")
        observations[phase] = {"bottom": bottom.get("current"), "main": main.get("current"), "focus": focus}
    return {"phases": len(PHASES), "sessions_closed": 2, "cleanup_verified": True, "raw_inputs_verified": True,
            "observations": observations}


def validate_find_workflow(directory: Path, run_record: dict) -> dict:
    directory = Path(directory)
    result = validate_find(json.loads((directory / "find.json").read_text(encoding="utf-8")), run_record)
    if (directory / "find-bottom.bin").read_bytes() != b"+#" or (directory / "find-main.bin").read_bytes() != b"x":
        raise ValueError("actual raw ConPTY receiver bytes contain leaked find text, navigation, Escape or duplicate input")
    return result


STREAM_PHASES = ("running-before-navigation", "partial-next-0", "partial-no-wrap", "partial-next-1",
                 "replaced-running-empty", "partial-chinese-0", "cancelled-output-continues", "explicitly-stopped",
                 "target-closing-running", "target-closed-other-input")


def validate_find_stream(data: dict, run_record: dict) -> dict:
    if (not isinstance(data, dict) or data.get("schema") != "termforge-window-find-stream-v1" or
            data.get("input_path") != "queued-WM_KEYDOWN-through-workspace-loop" or
            type(data.get("search_work_budget")) is not int or data["search_work_budget"] != 64 or
            type(data.get("worker_deadline_ms")) is not int or data["worker_deadline_ms"] != 5000 or
            not isinstance(data.get("snapshots"), list) or len(data["snapshots"]) != len(STREAM_PHASES) or
            any(not isinstance(row, dict) for row in data["snapshots"]) or
            [row.get("phase") for row in data["snapshots"]] != list(STREAM_PHASES)):
        raise ValueError("continuous find phases, actual queued path or bounded worker identity missing")
    if not isinstance(run_record, dict):
        raise ValueError("continuous find needs actual process cleanup")
    result = run_record.get("result")
    if result is None:
        try:
            result = json.loads(run_record.get("stdout", ""))
        except (ValueError, TypeError):
            raise ValueError("continuous find result missing") from None
    if (not isinstance(result, dict) or result.get("valid") is not True or result.get("mode") != "ui-find-stream" or
            type(result.get("sessions_closed")) is not int or result["sessions_closed"] != 2 or
            type(result.get("active_children_after")) is not int or result["active_children_after"] != 0 or
            "presented_done" not in result or result["presented_done"] is not None or
            run_record.get("timed_out") is not False or type(run_record.get("exit_code")) is not int or
            run_record["exit_code"] != 0 or run_record.get("desktop_gone") is not True or
            not isinstance(run_record.get("job_after"), dict) or type(run_record["job_after"].get("active")) is not int or
            run_record["job_after"]["active"] != 0 or run_record.get("cleanup_errors")):
        raise ValueError("continuous find target/worker/session closure, Job or desktop gate failed")
    first_identity = cancelled_tail_id = initial_tail = None
    for index, row in enumerate(data["snapshots"]):
        bottom_bytes = "~" if index < 6 else "~y" if index == 6 else "~y-" if index == 7 else "~y-~"
        if row.get("captures") != {"bottom": bottom_bytes, "main": "x" if index == 9 else ""}:
            raise ValueError("partial query/navigation/cancel leaked into the real console receiver")
        if (type(row.get("closed_find_hwnd_gone")) is not bool or row["closed_find_hwnd_gone"] != (index == 9) or
                type(row.get("closed_terminal_hwnd_gone")) is not bool or row["closed_terminal_hwnd_gone"] != (index == 9)):
            raise ValueError("closing an active query did not destroy its actual native HWNDs")
        sessions = row.get("sessions")
        if (not isinstance(sessions, list) or len(sessions) != 2 or any(not isinstance(s, dict) for s in sessions) or
                [s.get("group") for s in sessions] != ["bottom", "main"]):
            raise ValueError("continuous find needs both independently observed session regions")
        for session in sessions:
            bottom = session["group"] == "bottom"
            if type(session.get("exists")) is not bool or session["exists"] != (not bottom or index != 9):
                raise ValueError("continuous find session existence observation differs from its closure phase")
            if bottom and index == 9:
                continue
            finding = bottom and index not in (6, 7)
            active = bottom if index != 9 else not bottom
            terminal_focus = bottom and index in (6, 7) or not bottom and index == 9
            fields = ("find_exists", "edit_focused", "terminal_focused", "workspace_active", "frame_matches_full",
                      "running", "cancelled", "source_tick_seen")
            if (any(type(session.get(field)) is not bool for field in fields) or
                    session["find_exists"] != finding or session["edit_focused"] != finding or
                    session["terminal_focused"] != terminal_focus or session["workspace_active"] != active or
                    session["frame_matches_full"] is not True or session["running"] != finding or
                    session["cancelled"] != (bottom and index in (6, 7)) or session["source_tick_seen"] != bottom):
                raise ValueError("continuous find actual focus/running/cancel/output observation failed")
            query = "中文" if bottom and index in (4, 5) else "needle" if finding else None
            if session.get("query") != query:
                raise ValueError("an in-flight query was not actually replaced or cancelled")
            status = session.get("status_label")
            if (not isinstance(status, str) or finding and ("Searching" not in status or "wrapped" in status) or
                    not finding and status != ""):
                raise ValueError("incomplete search was advertised as a full-document end or wrap")
            if bottom and index in (1, 3, 5) and ("partial" not in status or "no wrap" not in status):
                raise ValueError("explicit incomplete navigation lacks its partial/no-wrap scope")
            if bottom and index == 2 and ("no known" not in status or "no wrap" not in status):
                raise ValueError("partial prefix boundary falsely wrapped or lost its pending direction")
            for field in ("needle_highlight_cells", "chinese_highlight_cells"):
                if type(session.get(field)) is not int or not 0 <= session[field] <= 100000:
                    raise ValueError("continuous find overlay bound failed")
            if (not bottom or index >= 4) and session["needle_highlight_cells"] != 0:
                raise ValueError("old needle query retained visible highlights after replacement/cancel")
            if (not bottom or index != 5) and index != 4 and session["chinese_highlight_cells"] != 0:
                raise ValueError("cancel/old query retained unexpected Chinese highlights")
            if bottom and (index in (1, 2, 3) and session["needle_highlight_cells"] < 6 or
                           index == 5 and session["chinese_highlight_cells"] < 4):
                raise ValueError("the actual partial current match has no visible search highlight")
            number = 0 if bottom and index in (1, 2, 5) else 1 if bottom and index == 3 else None
            current = session.get("current")
            lines = session.get("lines")
            if (not isinstance(lines, list) or not lines or len(lines) > 200 or
                    any(not isinstance(line, dict) or type(line.get("id")) is not int or line["id"] <= 0 or
                        type(line.get("version")) is not int or line["version"] <= 0 or
                        not isinstance(line.get("text"), str) for line in lines)):
                raise ValueError("continuous find lacks independent actual visible lines")
            if number is None:
                if current is not None:
                    raise ValueError("cancelled/replaced/other session acquired an old current match")
            else:
                begin, end = (19, 23) if index == 5 else (12, 18)
                if (not isinstance(current, dict) or
                        any(type(current.get(field)) is not int or current[field] <= 0 for field in
                            ("line_id", "version", "end_line_id", "end_version")) or
                        current["line_id"] != current["end_line_id"] or current["version"] != current["end_version"] or
                        type(current.get("begin")) is not int or current["begin"] != begin or
                        type(current.get("end")) is not int or current["end"] != end or current.get("line") != _line(number) or
                        not any(line == {"id": current["line_id"], "version": current["version"], "text": _line(number)} for line in lines)):
                    raise ValueError("partial match location is stale or its revealed literal content is wrong")
                if number == 0:
                    identity = current["line_id"], current["version"]
                    if first_identity is not None and identity != first_identity:
                        raise ValueError("stable history changed during continuous live overwrite")
                    first_identity = identity
            if not isinstance(session.get("tail"), dict) or session["tail"] != lines[-1]:
                raise ValueError("actual current output row observation missing")
        if index == 0:
            initial_tail = sessions[0]["tail"]
        if index == 6 and (not sessions[0]["tail"]["text"].startswith("TF_STREAM_TICK_") or
                           sessions[0]["tail"]["id"] != initial_tail["id"] or
                           sessions[0]["tail"]["version"] <= initial_tail["version"] or
                           sessions[0]["tail"]["text"] == initial_tail["text"]):
            raise ValueError("cancellation was not checked while real output continued")
        if index == 6:
            cancelled_tail_id = sessions[0]["tail"]["id"]
        if index == 7 and not any(line["text"] == "TF_STREAM_STOPPED" for line in sessions[0]["lines"]):
            raise ValueError("explicit stop lacks the joined output worker's actual completion marker")
        if index == 8 and (not sessions[0]["tail"]["text"].startswith("TF_STREAM_TICK_") or
                           sessions[0]["tail"]["id"] <= cancelled_tail_id):
            raise ValueError("target close was not checked against a new active output worker")
    return {"phases": len(STREAM_PHASES), "sessions_closed": 2, "partial_navigation_verified": True,
            "in_flight_replace_cancel_close_verified": True, "raw_inputs_verified": True, "cleanup_verified": True}


def validate_find_stream_workflow(directory: Path, run_record: dict) -> dict:
    directory = Path(directory)
    result = validate_find_stream(json.loads((directory / "find-stream.json").read_text(encoding="utf-8")), run_record)
    if (directory / "stream-bottom.bin").read_bytes() != b"~y-~" or (directory / "stream-main.bin").read_bytes() != b"x":
        raise ValueError("continuous find real receiver contains old queries, control keys or duplicate input")
    return result
