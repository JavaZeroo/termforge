"""Validate one controlled UI probe and export only allowlisted geometry/fonts.

The result establishes UI observations, not terminal correctness or performance.
Raw records, pointers, process IDs, paths and session text never enter the output.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from research.ui.launch import EXPECTED, EXPECTED_FILES

PHASES = ("initial", "empty-1360", "session-1360", "session-2048")
FAMILIES = {"Roboto Mono", "RobotoMono Nerd Font", "Consolas"}
ROLE_TYPES = {
    "MainWindow": ("MainWindow", ""), "TerminalWindow": ("TerminalWindow", "TerminalWindow"),
    "ViewGroup": ("TabGroup", "ViewGroup"), "MainTabBar": ("TabBar", ""),
    "MainToolbar": ("QToolBar", "ToolBar"), "MainAddressBar": ("AddressBar", "AddressBar"),
    "TerminalView": ("TerminalView", "TerminalView"), "TerminalEditor": ("ScintillaEditBase", ""),
    "TerminalViewport": ("QWidget", "qt_scrollarea_viewport"), "ShellPane": ("ShellPane", "ShellPane"),
    "MenuBar": ("QMenuBar", "MenuBar"), "QuickBar": ("QuickBar", "QuickBar"),
    "StatusBar": ("StatusBar", "StatusBar"),
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def number(value, low, high):
    require(type(value) in (int, float) and math.isfinite(value) and low <= value <= high,
            "Invalid numeric observation")
    return value


def font_spec(font):
    require(isinstance(font, dict), "Missing font observation")
    result = {}
    for key in ("family", "resolved_family"):
        require(font.get(key) in FAMILIES, "Unreviewed font family")
        result[key] = font[key]
    for key, low, high in (("point_size", -1, 256), ("pixel_size", -1, 1024),
                           ("resolved_point_size", .1, 256), ("resolved_pixel_size", 1, 1024)):
        result[key] = number(font.get(key), low, high)
    for key in ("bold", "exact_match"):
        require(type(font.get(key)) in (int, bool) and font[key] in (0, 1), "Invalid font flag")
        result[key] = bool(font[key])
    return result


def dpi_spec(dpi):
    require(isinstance(dpi, dict), "Missing DPI observation")
    result = {"dpr": number(dpi.get("dpr"), .5, 8)}
    for key in ("logical", "physical"):
        require(isinstance(dpi.get(key), list) and len(dpi[key]) == 2, "Invalid DPI pair")
        result[key] = [number(value, 1, 1000) for value in dpi[key]]
    return result


def rect_xywh(value):
    require(isinstance(value, list) and len(value) == 4 and all(type(v) is int for v in value), "Invalid rectangle")
    x, y, right, bottom = value
    width, height = right - x + 1, bottom - y + 1
    require(0 <= width <= 20000 and 0 <= height <= 20000, "Invalid rectangle dimensions")
    return [x, y, width, height]


def snapshot_spec(snapshot, draw_fonts):
    require(snapshot.get("first_run_import_flag") == 0 and type(snapshot.get("first_run_import_flag")) is int,
            "First-run import guard failed")
    phase = snapshot.get("phase")
    rows = snapshot.get("widgets")
    require(isinstance(rows, list) and 1 <= len(rows) <= 2500, "Invalid widget inventory")
    require(type(snapshot.get("total_widgets")) is int and len(rows) <= snapshot["total_widgets"] <= 2500,
            "Invalid widget count")
    require(all(isinstance(row, dict) for row in rows), "Invalid widget entry")
    by = {}
    for row in rows:
        pointer = row.get("ptr")
        require(isinstance(pointer, str) and re.fullmatch(r"0x[0-9a-fA-F]+", pointer) and int(pointer, 16) != 0,
                "Invalid widget identity")
        require(pointer not in by, "Duplicate widget identity")
        by[pointer] = row

    def one(role, predicate=lambda row: True):
        cls, name = ROLE_TYPES[role]
        matches = [r for r in rows if r.get("class") == cls and r.get("name") == name and predicate(r)]
        require(len(matches) == 1, "Missing or ambiguous UI role")
        return matches[0]

    main = one("MainWindow")
    require(main.get("parent") == "0x0", "Invalid MainWindow root")
    chains = {}
    for pointer in by:
        chain, visited, current = [], set(), pointer
        while current != main["ptr"]:
            require(current in by, "Broken widget parent chain")
            require(current not in visited, "Cyclic widget parent chain")
            visited.add(current)
            chain.append(current)
            current = by[current].get("parent")
        chains[pointer] = chain
    terminal = one("TerminalWindow")

    def position(row):
        x = y = 0
        for pointer in chains[row["ptr"]]:
            rect = rect_xywh(by[pointer].get("rect"))
            x += rect[0]
            y += rect[1]
        return x, y

    origin_x, origin_y = position(terminal)
    terminal_rect = rect_xywh(terminal.get("rect"))
    width, height = terminal_rect[2:]
    require(width >= 640 and height >= 480, "Unexpected window dimensions")
    expected_size = {"empty-1360": [1360, 860], "session-1360": [1360, 860], "session-2048": [2048, 1120]}
    require(phase not in expected_size or [width, height] == expected_size[phase], "Unexpected phase dimensions")
    require(rect_xywh(main.get("rect"))[2:] == [width, height], "MainWindow size mismatch")
    require(not any(row.get("class") == "MessageBar" for row in rows), "Unexpected error MessageBar")
    view_group = one("ViewGroup")
    roles = {"MainWindow": main, "TerminalWindow": terminal, "ViewGroup": view_group,
             "MainTabBar": one("MainTabBar", lambda r: r.get("parent") == view_group["ptr"]),
             "MainToolbar": one("MainToolbar", lambda r: r.get("parent") == view_group.get("parent"))}
    roles["MainAddressBar"] = one("MainAddressBar", lambda r: r.get("parent") == roles["MainToolbar"]["ptr"])
    for role in ("ShellPane", "MenuBar", "QuickBar", "StatusBar"):
        roles[role] = one(role)
    connected = phase.startswith("session-")
    if connected:
        view = one("TerminalView")
        require(view_group.get("parent") in chains[view["ptr"]], "Terminal outside main view group")
        roles["TerminalView"] = view
        roles["TerminalEditor"] = one("TerminalEditor", lambda r: view["ptr"] in chains[r["ptr"]])
        roles["TerminalViewport"] = one("TerminalViewport", lambda r: r.get("parent") == roles["TerminalEditor"]["ptr"])
    else:
        require(not any(row.get("class") == "TerminalView" for row in rows), "Unexpected terminal in empty phase")

    result = {}
    for role, row in roles.items():
        x, y = position(row)
        rw, rh = rect_xywh(row.get("rect"))[2:]
        if row is not main:
            local = rect_xywh(row.get("rect"))
            parent_size = rect_xywh(by[row["parent"]].get("rect"))[2:]
            require(local[0] >= 0 and local[1] >= 0 and local[0] + rw <= parent_size[0] and
                    local[1] + rh <= parent_size[1], "UI role outside parent")
        rect = [x - origin_x, y - origin_y, rw, rh]
        require(rect[0] >= 0 and rect[1] >= 0 and rect[0] + rw <= width and rect[1] + rh <= height,
                "UI role outside TerminalWindow")
        require(rw > 0 and (rh > 0 or (role == "MainTabBar" and not connected)), "Empty UI role")
        cls, name = ROLE_TYPES[role]
        result[role] = {"class": cls, "name": name, "rect": rect,
                        "dpi": dpi_spec(row.get("dpi")), "font": font_spec(row.get("font"))}
    tabs = roles["MainTabBar"].get("tabs")
    require(isinstance(tabs, list) and len(tabs) == int(connected), "Unexpected main tab count")
    tab_rects = []
    bx, by_value, bw, bh = result["MainTabBar"]["rect"]
    for value in tabs:
        tx, ty, tw, th = rect_xywh(value)
        require(tx >= 0 and ty >= 0 and tw > 0 and th > 0 and tx + tw <= bw and ty + th <= bh,
                "Invalid main tab rectangle")
        tab_rects.append([bx + tx, by_value + ty, tw, th])
    result["MainTabBar"]["tab_rects"] = tab_rects
    if connected:
        viewport = roles["TerminalViewport"]
        observed = []
        for draw in draw_fonts:
            if draw.get("phase") == phase and draw.get("widget", {}).get("ptr") == viewport["ptr"]:
                window_size = draw.get("window_size")
                require(isinstance(window_size, list) and len(window_size) == 2 and
                        all(type(value) is int for value in window_size) and window_size == [width, height],
                        "Text draw window dimensions do not match phase snapshot")
                require(draw["widget"].get("class") == "QWidget" and draw["widget"].get("name") == "qt_scrollarea_viewport",
                        "Mismatched text draw widget")
                value = font_spec(draw.get("font"))
                if value not in observed:
                    observed.append(value)
        require(observed, "Missing terminal viewport text draw observation")
        result["TerminalViewport"]["text_draw_fonts"] = observed
    screen = snapshot.get("screen")
    require(isinstance(screen, dict), "Missing screen observation")
    return {"phase": phase, "screen": {key: number(screen.get(key), low, high)
            for key, low, high in (("dpr", .5, 8), ("logical_dpi_x", 1, 1000), ("physical_dpi_x", 1, 1000))}, "roles": result}


def _json(path, limit):
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    require(len(data) <= limit, "Input exceeds size bound")
    return json.loads(data)


def process_names(run_dir, root_pid):
    """An optional owned-tree snapshot observes process names, not shell health."""
    path = run_dir / "process-tree.json"
    if not path.exists():
        return None
    rows = _json(path, 65536)
    require(isinstance(rows, list) and 1 <= len(rows) <= 16 and all(isinstance(row, dict) for row in rows),
            "Invalid owned process tree")
    by = {}
    allowed = {"cmd.exe", "conhost.exe", "winpty-agent.exe"}
    for row in rows:
        pid, parent = row.get("ProcessId"), row.get("ParentProcessId")
        require(type(pid) is int and pid > 0 and pid != root_pid and pid not in by and
                type(parent) is int and parent > 0 and row.get("Name") in allowed, "Unreviewed owned process observation")
        by[pid] = row
    for pid in by:
        seen = set()
        while pid != root_pid:
            require(pid in by and pid not in seen, "Invalid owned process ancestry")
            seen.add(pid)
            pid = by[pid]["ParentProcessId"]
    return sorted({row["Name"] for row in rows})


def summarize_run(run_dir: Path, evidence_id: str):
    require(isinstance(evidence_id, str) and re.fullmatch(r"ev_[0-9a-f]{64}", evidence_id), "Invalid evidence ID")
    run_dir = Path(run_dir)
    run = _json(run_dir / "run.json", 65536)
    cleanup = _json(run_dir / "cleanup.json", 65536)
    profile = _json(run_dir / "profile-check.json", 65536)
    require(isinstance(run, dict) and run.get("sha256") == EXPECTED and run.get("files_sha256") == EXPECTED_FILES,
            "Pinned target identity mismatch")
    require(run.get("home") == "owned-empty" and run.get("profile") == "synthetic-local-only" and
            run.get("desktop") == "owned-non-input" and run.get("probe_kind") == "typography", "Unexpected probe scope")
    require(run.get("qt_scale_factor") in (None, "1", "1.5"), "Unexpected scale factor")
    require(isinstance(cleanup, dict) and type(cleanup.get("active")) is int and cleanup["active"] == 0,
            "Owned process cleanup incomplete")
    require(isinstance(profile, dict) and profile.get("only_synthetic_session") is True and
            type(profile.get("session_count")) is int and profile["session_count"] == 1, "Unexpected profile scope")
    with (run_dir / "probe.js").open("rb") as stream:
        script = stream.read(1024 * 1024 + 1)
    require(len(script) <= 1024 * 1024, "Probe exceeds size bound")
    script_hash = hashlib.sha256(script).hexdigest()
    require(run.get("script_sha256") == script_hash, "Probe script hash mismatch")
    bundle = _json(run_dir / "evidence.json", 32 * 1024 * 1024)
    require(isinstance(bundle, dict) and isinstance(bundle.get("records"), list), "Invalid evidence bundle")
    matches = [record for record in bundle["records"] if isinstance(record, dict) and record.get("evidence_id") == evidence_id]
    require(len(matches) == 1, "Missing or duplicate evidence record")
    record = matches[0]
    require(record.get("operation") == "instrument_with_frida", "Unexpected evidence operation")
    parameters, normalized = record.get("parameters"), record.get("normalized_result")
    require(isinstance(parameters, dict) and isinstance(normalized, dict), "Invalid instrument evidence")
    require("error" not in record and "error" not in normalized, "Instrument error result")
    pid = run.get("pid")
    require(type(pid) is int and pid > 0 and type(parameters.get("pid")) is int and type(normalized.get("pid")) is int and
            parameters["pid"] == pid and normalized["pid"] == pid,
            "Instrument process identity mismatch")
    require(parameters.get("mode") == "attach" and normalized.get("mode") == "attach" and
            parameters.get("device_id") == "local" and normalized.get("device_id") == "local", "Unexpected instrumentation scope")
    require(normalized.get("source_sha256") == script_hash and
            parameters.get("source_sha256", script_hash) == script_hash, "Evidence script hash mismatch")
    require(normalized.get("source_kind") == "file" and isinstance(run.get("probe"), str) and
            isinstance(normalized.get("source_path"), str) and
            normalized["source_path"].replace("\\", "/").casefold() == run["probe"].replace("\\", "/").casefold(),
            "Evidence script path mismatch")
    require("cleanup_error" in normalized and normalized["cleanup_error"] is None and
            normalized.get("messages_truncated") is False, "Incomplete instrumentation evidence")
    messages = normalized.get("messages")
    require(isinstance(messages, list) and 1 <= len(messages) <= 1000, "Invalid instrumentation messages")
    payloads = []
    for message in messages:
        require(isinstance(message, dict) and message.get("type") == "send", "Instrument error message")
        payload = message.get("payload")
        require(isinstance(payload, dict) and "error" not in payload and "error" not in message,
                "Probe error message")
        require(not message.get("truncated") and not payload.get("truncated"), "Truncated probe message")
        require(payload.get("kind") in ("probe_ready", "snapshot", "text_draw_font", "scenario_complete"), "Unknown probe message")
        payloads.append(payload)
    require(sum(p["kind"] == "probe_ready" for p in payloads) == 1 and payloads[0]["kind"] == "probe_ready",
            "Missing probe readiness")
    require(sum(p["kind"] == "scenario_complete" for p in payloads) == 1 and payloads[-1]["kind"] == "scenario_complete",
            "Incomplete UI scenario")
    snapshots = [p for p in payloads if p["kind"] == "snapshot"]
    require([p.get("phase") for p in snapshots] == list(PHASES), "Missing, repeated or unordered phase")
    draws = [p for p in payloads if p["kind"] == "text_draw_font"]
    require(all(p.get("phase") in PHASES for p in draws), "Unknown font observation phase")
    return {"schema_version": 1, "scope": "ui_geometry_and_font_observation",
            "coordinates": "TerminalWindow_origin_xywh_Qt_logical",
            "target_sha256": EXPECTED, "files_sha256": EXPECTED_FILES, "script_sha256": script_hash,
            "evidence_id": evidence_id, "qt_scale_factor": run["qt_scale_factor"],
            "observed_process_names": process_names(run_dir, pid),
            "phases": [snapshot_spec(snapshot, draws) for snapshot in snapshots]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-dir", type=Path, required=True)
    parser.add_argument("--evidence-id", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = summarize_run(args.run_dir, args.evidence_id)
    except (OSError, ValueError, KeyError, TypeError):
        parser.error("UI evidence validation failed; no summary was written")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2, ensure_ascii=True, allow_nan=False) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
