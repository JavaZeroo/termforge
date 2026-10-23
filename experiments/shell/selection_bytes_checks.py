"""Independent ConPTY byte, physical-drag, DPI and raw-receiver oracles."""
from __future__ import annotations

import hashlib
import json
import os
import re
from pathlib import Path

from experiments.shell.clipboard_checks import require_hosted_clipboard


PHASES = ("forward", "reverse", "dpi-96", "dpi-144", "dpi-192", "dpi-96-restored",
          "cleared", "empty-copy", "input-after")
DPIS = (96, 96, 96, 144, 192, 96, 96, 96, 96)
CONT = "CONT 中文 e\u0301  "
HARD = "HARD 中文 e\u0301  "
READY = "TF_SELECT_BYTES_READY"
SENTINEL = "TF_BYTES_SENTINEL 中文 e\u0301"


def intended_output(columns: int, fixture_style: str = "plain-v1") -> bytes:
    """An independent formula; columns is the real producer-reported width."""
    if type(columns) is not int or not 20 <= columns <= 500:
        raise ValueError("selection producer width is outside its bounded fixture")
    if fixture_style not in ("plain-v1", "styled-tail-v1"):
        raise ValueError("selection producer fixture identity is unknown")
    tail = "\x1b[43m  \x1b[0m" if fixture_style == "styled-tail-v1" else "  "
    return ("\x1b[2J\x1b[H" + "SOFT_" + "s" * (columns - 7) + "  " +
            CONT[:-2] + tail + "\r\n" + HARD[:-2] + tail + "\r\n" + READY + "\r\n").encode("utf-8")


def expected_multiline(columns: int) -> str:
    # The first W cells wrap automatically; the next CRLF is explicit.
    return "SOFT_" + "s" * (columns - 7) + "  " + CONT + "\r\n" + HARD


def expected_physical_lines(initial_columns: int, current_columns: int, rows: int) -> list[str]:
    """The fixed fixture's ConPTY repaint at the observed current width."""
    result = []
    for logical in ("SOFT_" + "s" * (initial_columns - 7) + "  " + CONT, HARD, READY):
        physical, used = "", 0
        for char in logical:
            cells = 0 if char == "\u0301" else 2 if char in "中文" else 1
            if cells and used + cells > current_columns:
                result.append(physical.rstrip(" ")); physical, used = "", 0
            physical += char; used += cells
        result.append(physical.rstrip(" "))
    if len(result) > rows:
        raise ValueError("selection fixture no longer fits its bounded visible page")
    return result + [""] * (rows - len(result))


def _validate_cleanup(record: dict, system: bool) -> None:
    if not isinstance(record, dict):
        raise ValueError("selection bytes requires actual collector cleanup observations")
    result = record.get("result")
    if result is None:
        try:
            result = json.loads(record.get("stdout", ""))
        except (ValueError, TypeError):
            raise ValueError("selection bytes collector lacks its native result") from None
    mode = "ui-copy-bytes-system" if system else "ui-selection-bytes"
    if (not isinstance(result, dict) or result.get("valid") is not True or result.get("mode") != mode or
            type(result.get("sessions_closed")) is not int or result["sessions_closed"] != 2 or
            type(result.get("active_children_after")) is not int or result["active_children_after"] != 0 or
            "presented_done" not in result or result["presented_done"] is not None or
            record.get("timed_out") is not False or type(record.get("exit_code")) is not int or record["exit_code"] != 0 or
            record.get("desktop_gone") is not True or not isinstance(record.get("job_after"), dict) or
            type(record["job_after"].get("active")) is not int or record["job_after"]["active"] != 0 or record.get("cleanup_errors")):
        raise ValueError("selection bytes native run, two sessions, Job or desktop cleanup failed")


def validate_selection_bytes(data: dict, system_clipboard: bool, run_record: dict) -> dict:
    if (not isinstance(data, dict) or data.get("schema") not in ("termforge-window-selection-bytes-v1", "termforge-window-selection-bytes-v2") or
            type(data.get("system_clipboard")) is not bool or data["system_clipboard"] != system_clipboard or
            data.get("producer") != "real-ConPTY-console-writefile" or
            data.get("input_source") != "physical-pixel-drag-and-queued-workspace-keys" or
            data.get("controlled_dpi_only") is not True or type(data.get("transport_observation_limit")) is not int or
            data["transport_observation_limit"] != 8192 or not isinstance(data.get("snapshots"), list) or
            len(data["snapshots"]) != len(PHASES) or any(not isinstance(row, dict) for row in data["snapshots"]) or
            [row.get("phase") for row in data["snapshots"]] != list(PHASES)):
        raise ValueError("selection bytes real producer, actual input path or nine phases are incomplete")
    fixture_style = "plain-v1" if data["schema"] == "termforge-window-selection-bytes-v1" else data.get("fixture_style")
    if ((data["schema"] == "termforge-window-selection-bytes-v2" and fixture_style != "styled-tail-v1") or
            (data["schema"] == "termforge-window-selection-bytes-v1" and data.get("fixture_style", "plain-v1") != "plain-v1")):
        raise ValueError("selection bytes window schema and exact fixture identity disagree")
    _validate_cleanup(run_record, system_clipboard)
    initial = None
    previous_ticks = [0, 0]
    os_dpi = None
    for phase_index, (row, phase, dpi) in enumerate(zip(data["snapshots"], PHASES, DPIS)):
        if (type(row.get("controlled_dpi")) is not int or row["controlled_dpi"] != dpi or
                type(row.get("os_window_dpi")) is not int or not 48 <= row["os_window_dpi"] <= 768):
            raise ValueError("selection bytes controlled DPI phase was not observed")
        if os_dpi is None:
            os_dpi = row["os_window_dpi"]
        if row["os_window_dpi"] != os_dpi:
            raise ValueError("controlled DPI fixture unexpectedly changed actual OS window DPI")
        sessions = row.get("sessions")
        if (not isinstance(sessions, list) or len(sessions) != 2 or any(not isinstance(s, dict) for s in sessions) or
                [s.get("group") for s in sessions] != ["bottom", "main"]):
            raise ValueError("selection bytes requires observations from both real session regions")
        if initial is None:
            initial = [(s.get("initial_columns"), s.get("initial_rows")) for s in sessions]
        for session_index, session in enumerate(sessions):
            ints = ("columns", "rows", "initial_columns", "initial_rows", "cell_width", "cell_height",
                    "ticks", "overlay_cells", "display_dpi", "zoom")
            flags = ("selected", "focused", "workspace_active", "captured", "frame_matches_full")
            if (any(type(session.get(field)) is not int for field in ints) or
                    any(type(session.get(field)) is not bool for field in flags) or
                    not 20 <= session["columns"] <= 500 or not 5 <= session["rows"] <= 200 or
                    not 20 <= session["initial_columns"] <= 500 or not 5 <= session["initial_rows"] <= 200 or
                    not 1 <= session["cell_width"] <= 256 or not 1 <= session["cell_height"] <= 256 or
                    session["ticks"] <= previous_ticks[session_index] or
                    (phase_index and session["ticks"] - previous_ticks[session_index] < 3) or
                    session["display_dpi"] != dpi or session["zoom"] != 100 or
                    (session["initial_columns"], session["initial_rows"]) != initial[session_index] or
                    session["captured"] is not False or session["frame_matches_full"] is not True or
                    not 0 <= session["overlay_cells"] <= session["columns"] * session["rows"]):
                raise ValueError(f"selection bytes grid, timer, DPI, capture or complete redraw failed: {phase}")
            previous_ticks[session_index] = session["ticks"]
            expected = (expected_multiline(session["initial_columns"]) if phase_index < 2 else HARD) if session_index and phase_index < 6 else ""
            if (session["text"] if "text" in session else None) != expected:
                raise ValueError(f"selection bytes literal text differs (soft join, CRLF or explicit tail): {phase}")
            if (session.get("status") != ("ok" if expected else "empty") or session["selected"] != bool(expected) or
                    session["focused"] != bool(session_index) or session["workspace_active"] != bool(session_index) or
                    bool(session["overlay_cells"]) != bool(expected)):
                raise ValueError(f"selection bytes target, validity, focus or overlay differs: {phase}")
            lines = session.get("visible_lines")
            if (not isinstance(lines, list) or len(lines) != session["rows"] or
                    any(not isinstance(line, str) or len(line.encode("utf-8")) > 4096 for line in lines)):
                raise ValueError("selection bytes visible physical rows are missing or unbounded")
            if lines != expected_physical_lines(session["initial_columns"], session["columns"], session["rows"]):
                raise ValueError("selection bytes fixed fixture physical rows differ after actual ConPTY/DPI repaint")
            if session_index and ((system_clipboard and phase_index < 6 and session.get("product_status") != "Selected text copied") or
                                  (phase == "empty-copy" and session.get("product_status") != "No selected text")):
                raise ValueError("selection bytes Copy shortcut did not reach its actual product action")
        selected_copy = system_clipboard and phase_index < 6
        expected_entry = "queued-Ctrl-Shift-C" if selected_copy else "queued-Ctrl-Shift-C-empty" if phase == "empty-copy" else "none"
        expected_cf = (expected_multiline(initial[1][0]) if phase_index < 2 else HARD) if selected_copy else SENTINEL if system_clipboard and phase == "empty-copy" else None
        if row.get("copy_entry") != expected_entry or row.get("clipboard") != expected_cf:
            raise ValueError(f"selection bytes real system clipboard or queued Copy entry differs: {phase}")
        if row.get("raw_bottom") != "" or row.get("raw_main") != ("x" if phase == "input-after" else ""):
            raise ValueError("selection bytes Copy/Escape leaked input or plain input reached another session")
    return {"system_clipboard": system_clipboard, "phases": len(PHASES),
            "fixture_style": fixture_style,
            "soft_wrap_join": True, "hard_crlf": True, "explicit_trailing_spaces": 2,
            "controlled_dpi": [96, 144, 192, 96], "dpi_selection_preserved": True,
            "raw_input_isolated": True, "cleanup_verified": True}


def _validate_producer(facts: dict, intended: bytes, columns: int, rows: int, fixture_style: str) -> None:
    numeric = ("columns", "rows", "input_mode_before", "input_mode_requested", "input_mode_readback",
               "output_mode_before", "output_mode_requested", "output_mode_readback", "input_code_page",
               "output_code_page", "payload_bytes")
    if (not isinstance(facts, dict) or facts.get("schema") != "termforge-selection-producer-v1" or
            facts.get("console_input") is not True or facts.get("console_output") is not True or
            facts.get("fixture_style", "plain-v1") != fixture_style or
            any(type(facts.get(field)) is not int or not 0 <= facts[field] <= 0xffffffff for field in numeric) or
            facts["columns"] != columns or facts["rows"] != rows or facts["input_mode_requested"] != 512 or
            facts["input_mode_readback"] != 512 or facts["output_mode_requested"] != (facts["output_mode_before"] | 15) or
            facts["output_mode_readback"] != facts["output_mode_requested"] or
            facts["input_code_page"] != 65001 or facts["output_code_page"] != 65001 or
            facts["payload_bytes"] != len(intended) or intended != intended_output(columns, fixture_style)):
        raise ValueError("selection actual ConPTY producer dimensions/modes or original intended bytes differ")


def validate_selection_bytes_workflow(directory: Path, system_clipboard: bool, run_record: dict) -> dict:
    if system_clipboard:
        require_hosted_clipboard(os.environ)
    directory = Path(directory)
    data = json.loads((directory / "selection-bytes.json").read_text(encoding="utf-8"))
    summary = validate_selection_bytes(data, system_clipboard, run_record)
    # Inspect identity and raw evidence independently; never feed it to the UI.
    identities = {}
    for index, region in enumerate(("bottom", "main")):
        prefix = directory / f"selection-bytes-{region}"
        facts = json.loads(prefix.with_suffix(".producer.json").read_text(encoding="utf-8"))
        intended = prefix.with_suffix(".producer.bin").read_bytes()
        initial = data["snapshots"][0]["sessions"][index]
        _validate_producer(facts, intended, initial["initial_columns"], initial["initial_rows"], summary["fixture_style"])
        transport = prefix.with_suffix(".transport.bin").read_bytes()
        if not 1 <= len(transport) < 8192 or READY.encode() not in transport:
            raise ValueError("actual ConPTY transport is missing, possibly truncated or lacks the completion marker")
        # A styled-tail fixture must prove actual literal space bytes survived
        # the console serialization; an EL sequence cannot stand in for them.
        styled_tails = len(re.findall(rb"e\xcc\x81\x1b\[[0-9;]*m  \x1b\[[0-9;]*m(?:\x1b\[(?:0)?K)?\r\n", transport))
        if summary["fixture_style"] == "styled-tail-v1" and styled_tails < 2:
            raise ValueError("styled trailing spaces are not independently visible as two literal transport bytes")
        raw = prefix.with_suffix(".bin").read_bytes()
        if raw != (b"x" if region == "main" else b""):
            raise ValueError("selection bytes actual receiver contains Copy/Escape leakage or wrong-session input")
        identities[region] = {"producer_sha256": hashlib.sha256(intended).hexdigest(),
                              "transport_sha256": hashlib.sha256(transport).hexdigest(),
                              "transport_bytes": len(transport), "transport_equals_producer": transport == intended,
                              "literal_styled_tail_occurrences": styled_tails,
                              "raw_bytes": len(raw)}
    summary["evidence"] = identities
    return summary
