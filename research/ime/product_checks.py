"""Pure product IME oracles; synthetic schema tests never establish hosted input."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import struct

MODES = ("ui-ime-basic", "ui-ime-geometry", "ui-ime-focus", "ui-ime-close")
NIHAO = "你好".encode("utf-8")
CHECKPOINTS = {
    MODES[0]: ("bottom-preedit", "bottom-moved", "bottom-commit", "bottom-cancel", "main-preedit", "main-commit", "main-cancel"),
    MODES[1]: ("before-output", "after-output", "after-resize", "after-zoom", "after-dpi", "committed", "cancelled"),
    MODES[2]: ("bottom-preedit", "bottom-hidden", "main-commit", "bottom-resumed", "hidden-preedit"),
    MODES[3]: ("before-close", "survivor-commit", "survivor-cancel"),
}
ACTIONS = {
    MODES[0]: {"candidate_followed_workspace", "bottom-cancel", "main-cancel"},
    MODES[1]: {"twenty_output_rows", "resize", "zoom_125", "controlled_dpi_120", "cancelled"},
    MODES[2]: {"hidden_cancelled", "rename_cancelled_old", "rename_text_verified"},
    MODES[3]: {"closed_target_destroyed", "closed_receiver_reaped", "closed_candidate_gone", "survivor-cancel"},
}


def _check(condition, reason):
    if not condition:
        raise ValueError(reason)


def _integer(value, minimum=0, maximum=0xffffffff):
    return type(value) is int and minimum <= value <= maximum


def _rect(value):
    return (isinstance(value, list) and len(value) == 4 and
            all(_integer(n, -1000000, 1000000) for n in value) and value[0] < value[2] and value[1] < value[3])


def _hex(value):
    _check(isinstance(value, str) and len(value) <= 32768 and re.fullmatch(r"(?:[0-9a-f]{2})*", value) is not None,
           "IME captured byte encoding is malformed or exceeds its bound")
    return bytes.fromhex(value)


def _candidates(value):
    _check(isinstance(value, list) and len(value) <= 32, "actual candidate observations missing or unbounded")
    identities = set()
    for row in value:
        _check(isinstance(row, dict) and _integer(row.get("instance"), 1) and
               isinstance(row.get("class"), str) and 0 < len(row["class"]) <= 256 and _rect(row.get("rect")),
               "candidate HWND identity/class/physical rectangle is invalid")
        _check(row["instance"] not in identities, "a candidate HWND was duplicated in one observation")
        identities.add(row["instance"])
    return [row for row in value if "candidate" in row["class"].lower()]


def _moved(before, after):
    a, b = before["caret_screen"], after["caret_screen"]
    dx, dy = b[0] - a[0], b[3] - a[3]
    _check((dx or dy) and after["caret_updates"] > before["caret_updates"], "product caret did not actually move/update")
    _check(any(first["instance"] == last["instance"] and first["class"] == last["class"] and
               abs((last["rect"][0] - first["rect"][0]) - dx) <= 8 and
               abs((last["rect"][1] - first["rect"][1]) - dy) <= 8
               for first in _candidates(before["candidates"]) for last in _candidates(after["candidates"])),
           "no same real candidate HWND followed the measured physical product caret")


def _anchored(row):
    # Fixed synthetic test windows must place the candidate near their actual
    # caret. Permit upward placement and horizontal work-area clamping.
    work, caret = row.get("monitor_work"), row["caret_screen"]
    _check(_rect(work) and _integer(row.get("os_dpi"), 48, 768), "actual caret monitor work area or OS DPI missing")
    border = (8 * row["os_dpi"] + 95) // 96
    cell_width, cell_height = row["cell_pixels"]
    def near(candidate):
        rect = candidate["rect"]
        width = rect[2] - rect[0]
        if width > work[2] - work[0]:
            return False
        x = min(max(caret[0], work[0]), work[2] - width)
        return (rect[0] >= work[0] - border and rect[1] >= work[1] - border and
                rect[2] <= work[2] + border and rect[3] <= work[3] + border and
                abs(rect[0] - x) <= cell_width + border and
                min(abs(rect[1] - caret[3]), abs(rect[3] - caret[1])) <= cell_height + border)
    _check(any(near(candidate) for candidate in _candidates(row["candidates"])),
           "visible real candidate is not anchored near the physical product caret")


def _read_json(path, limit=1024 * 1024):
    with Path(path).open("rb") as stream:
        raw = stream.read(limit + 1)
    _check(len(raw) <= limit, "IME report exceeds its bounded schema size")
    def unique(pairs):
        result = {}
        for name, value in pairs:
            _check(name not in result, "IME JSON has a duplicate member")
            result[name] = value
        return result
    return json.loads(raw.decode("utf-8-sig"), object_pairs_hook=unique)


def _job(value, minimum_total):
    return (isinstance(value, dict) and _integer(value.get("active")) and value["active"] == 0 and
            _integer(value.get("total"), minimum_total) and _integer(value.get("terminated")) and
            value["terminated"] <= value["total"])


def validate_product_manifest(data, mode, captures):
    """Check by-value native observations against independent literal bytes."""
    _check(mode in MODES and isinstance(data, dict) and data.get("schema") == "termforge-product-ime-v1" and data.get("mode") == mode,
           "product IME manifest identity is missing")
    desktop = data.get("input_desktop")
    _check(isinstance(desktop, dict) and desktop.get("station") == "WinSta0" and desktop.get("query_only") is True and
           isinstance(desktop.get("input_name"), str) and 0 < len(desktop["input_name"]) < 256 and
           desktop.get("thread_name") == desktop["input_name"], "product did not observe its existing shared input desktop")
    provider = data.get("provider")
    expected_provider = {"clsid": "{81D4E9C9-1D3B-41BC-9E6C-4B40BF79E35E}",
                         "profile": "{FA550B04-5AD7-411F-A5AC-CA038EC515D7}", "langid": 2052,
                         "language_hresult": 0, "activation_hresult": 0}
    _check(isinstance(provider, dict) and provider == expected_provider and
           all(type(provider[field]) is int for field in ("langid", "language_hresult", "activation_hresult")),
           "fixed Microsoft Pinyin identity or activation differs from the contract")
    expected_batches = dict(zip(MODES, (18, 9, 25, 14)))[mode]
    _check(type(data.get("key_batches")) is int and data["key_batches"] == expected_batches and
           type(data.get("guarded_key_batches")) is int and data["guarded_key_batches"] == expected_batches,
           "real input batches were missing, duplicated or unguarded")
    names = ["bottom", "main"] + (["hidden"] if mode == MODES[2] else [])
    expected_bytes = {name: NIHAO for name in names}
    if mode != MODES[0]:
        expected_bytes["bottom" if mode != MODES[2] else "hidden"] = b""
    _check(isinstance(captures, dict) and set(captures) == set(names) and
           all(type(captures[name]) is bytes and captures[name] == expected_bytes[name] for name in names),
           "real receiver contains preedit, cancellation keys, duplicates, missing commit or cross-session bytes")
    receivers = data.get("receivers")
    _check(isinstance(receivers, list) and len(receivers) == len(names) and
           all(isinstance(row, dict) for row in receivers) and [row.get("name") for row in receivers] == names,
           "independent product IME receivers are missing")
    for generation, row in enumerate(receivers, 1):
        name = row["name"]; by_close = mode == MODES[3] and name == "bottom"
        _check(type(row.get("generation")) is int and row["generation"] == generation and row.get("capture") == name + ".bin" and
               type(row.get("bytes")) is int and row["bytes"] == len(captures[name]) and _hex(row.get("hex")) == captures[name] and
               row.get("process_signaled") is True and row.get("stopped_by_close") is by_close and
               type(row.get("stopped_event")) is bool and row["stopped_event"] is (not by_close) and
               _integer(row.get("exit_code")) and (row["exit_code"] != 259 if by_close else row["exit_code"] == 0),
               "actual receiver generation, raw bytes or event/process/exit cleanup failed")
    checkpoints = data.get("checkpoints")
    _check(isinstance(checkpoints, list) and len(checkpoints) == len(CHECKPOINTS[mode]) and
           all(isinstance(row, dict) for row in checkpoints) and [row.get("name") for row in checkpoints] == list(CHECKPOINTS[mode]),
           "actual product IME checkpoints are missing or reordered")
    rows = {row["name"]: row for row in checkpoints}
    last = {}
    for row in checkpoints:
        session = row.get("session")
        _check(session in names and type(row.get("generation")) is int and row["generation"] == names.index(session) + 1,
               "a checkpoint was attributed to a stale or different session generation")
        _check(type(row.get("composing")) is bool and isinstance(row.get("preedit"), str) and len(row["preedit"]) <= 4096 and
               row.get("ime_error") == "" and row.get("character_units") == [] and row.get("caret_positioned") is True and
               _integer(row.get("composition_events"), 1, 4096) and _integer(row.get("result_events"), 0, 1) and
               _integer(row.get("caret_updates"), 1, 1000000) and _rect(row.get("caret_client")) and _rect(row.get("caret_screen")),
               "real composition, no-WM_CHAR, native caret or IME error observation failed")
        a, b = row["caret_client"], row["caret_screen"]
        _check(a[2] - a[0] == b[2] - b[0] and a[3] - a[1] == b[3] - b[1], "actual client/screen caret size differs")
        cells = row.get("cell_pixels")
        _check(_integer(row.get("display_dpi"), 48, 768) and _integer(row.get("zoom_percent"), 50, 200) and
               isinstance(cells, list) and len(cells) == 2 and all(_integer(n, 1, 512) for n in cells) and
               cells == [a[2] - a[0], a[3] - a[1]], "actual native caret is not the measured product cell size")
        _candidates(row.get("candidates"))
        old = last.get(session)
        if old:
            _check(all(row[field] >= old[field] for field in ("composition_events", "result_events", "caret_updates")),
                   "cumulative product IME observations regressed")
        last[session] = row
        expected_session = "bottom" if row["name"].startswith("bottom") or row["name"] == "before-close" else "hidden" if row["name"] == "hidden-preedit" else "main"
        _check(session == expected_session, "a checkpoint came from the wrong product region")
        committed = (mode == MODES[0] and row["name"] in ("bottom-commit", "bottom-cancel", "main-commit", "main-cancel") or
                     mode == MODES[1] and row["name"] in ("committed", "cancelled") or
                     mode == MODES[2] and row["name"] in ("main-commit", "bottom-resumed") or
                     mode == MODES[3] and row["name"] in ("survivor-commit", "survivor-cancel"))
        hidden = row["name"] == "bottom-hidden"
        _check(_hex(row.get("received_hex")) == (NIHAO if committed else b"") and
               row.get("committed") == ("你好" if committed else "") and row["result_events"] == int(committed) and
               row["composing"] is (not committed and not hidden) and bool(row["preedit"]) is row["composing"],
               "preedit was sent early, commit was not exact-once, or cancel/focus loss retained composition")
        if row["composing"]:
            _check(bool(_candidates(row["candidates"])), "real product preedit lacks a visible actual candidate HWND")
            _anchored(row)
    if mode == MODES[0]:
        _moved(rows["bottom-preedit"], rows["bottom-moved"])
        for region in names:
            _check(rows[region + "-cancel"]["composition_events"] > rows[region + "-commit"]["composition_events"],
                   "NI cancellation was not preceded by new actual composition")
    elif mode == MODES[1]:
        for first, second in zip(CHECKPOINTS[mode][:4], CHECKPOINTS[mode][1:5]):
            _moved(rows[first], rows[second])
        initial_dpi = rows["before-output"]["display_dpi"]
        _check(all(rows[name]["zoom_percent"] == 100 and rows[name]["display_dpi"] == initial_dpi for name in
                   ("before-output", "after-output", "after-resize")) and
               rows["after-zoom"]["zoom_percent"] == 125 and rows["after-zoom"]["display_dpi"] == initial_dpi and
               all(rows[name]["zoom_percent"] == 125 and rows[name]["display_dpi"] == 120 for name in
                   ("after-dpi", "committed", "cancelled")) and
               all(a < b for a, b in zip(rows["after-resize"]["cell_pixels"], rows["after-zoom"]["cell_pixels"])),
               "actual product metrics do not reflect zoom 125 and controlled DPI 120")
        _check(rows["cancelled"]["composition_events"] > rows["committed"]["composition_events"], "geometry case lacked real NI cancellation")
    elif mode == MODES[2]:
        before = {row["instance"] for row in _candidates(rows["bottom-preedit"]["candidates"])}
        _check(not before.intersection(row["instance"] for row in _candidates(rows["bottom-hidden"]["candidates"])),
               "hidden product session retained its visible candidate HWND")
    else:
        before = {row["instance"] for row in _candidates(rows["before-close"]["candidates"])}
        after = data.get("after_close_candidates")
        _check(not before.intersection(row["instance"] for row in _candidates(after)), "closed target retained a visible candidate HWND")
        _check(rows["survivor-cancel"]["composition_events"] > rows["survivor-commit"]["composition_events"], "survivor lacked real NI cancellation")
    rename = data.get("rename")
    _check(isinstance(rename, dict), "actual rename observations missing")
    rename_candidates = _candidates(rename.get("candidates"))
    _check(isinstance(rename.get("preedit"), str) and len(rename["preedit"]) <= 4096 and
           rename.get("text") == ("你好" if mode == MODES[2] else "") and
           bool(rename["preedit"]) is (mode == MODES[2]) and bool(rename_candidates) is (mode == MODES[2]),
           "rename EDIT lacked real composition/candidate/literal commit or contaminated another case")
    _check(mode == MODES[2] or rename["candidates"] == [], "another case unexpectedly observed rename input")
    actions = data.get("actions")
    _check(isinstance(actions, dict) and set(actions) == ACTIONS[mode] and all(value is True for value in actions.values()),
           "product output/resize/zoom/focus/close action gates failed")
    output = data.get("terminal_output")
    _check(isinstance(output, dict) and set(output) == set(names), "actual terminal output observations missing")
    expected_lines = ["TF_IME_OUTPUT " + str(index) for index in range(20)] + ["TF_IME_OUTPUT_DONE"] if mode == MODES[1] else []
    _check(data.get("output_screen_lines") == expected_lines,
           "actual retained terminal text lacks twenty ordered exact-once output rows and completion marker")
    for name, value in output.items():
        raw = _hex(value)
        if mode == MODES[1] and name == "main":
            # ConPTY may serialize cursor VT or repaint after a resize. Exact
            # row counts belong to the actual retained decoded text above,
            # rather than assuming producer CRLF survives the console server.
            _check(b"TF_IME_OUTPUT_DONE" in raw, "raw geometry output completion observation missing")
        else:
            _check(b"TF_IME_OUTPUT" not in raw, "geometry output was attributed to another session/case")
    return {"checkpoints": len(checkpoints), "receivers": len(receivers), "raw_receiver_bytes_verified": True,
            "actual_candidates_verified": True, "product_workflow_verified": True}


def validate_product_case(directory, mode, run_record):
    directory = Path(directory)
    _check(isinstance(run_record, dict) and run_record.get("mode") == mode and run_record.get("status") in ("collected", "passed") and
           run_record.get("assigned") is True and run_record.get("gate_released") is True and
           run_record.get("process_reaped") is True and run_record.get("timed_out") is False and
           type(run_record.get("returncode")) is int and run_record["returncode"] == 0 and
           run_record.get("forced_job_termination") is False and run_record.get("cleanup_errors") == [] and
           run_record.get("evidence_errors") == [] and run_record.get("error") is None and
           _job(run_record.get("outer_job_before_cleanup"), 1) and _job(run_record.get("outer_job_after"), 1),
           "product IME process/outer Job cleanup did not pass")
    _check("desktop_gone" not in run_record, "shared input desktop must never be deleted or claimed as gone")
    lifecycle = run_record.get("lifecycle")
    _check(isinstance(lifecycle, list) and all(isinstance(row, dict) for row in lifecycle) and
           [row.get("stage") for row in lifecycle] == ["starting", "awaiting-go", "job-assigned", "releasing-go", "finished"],
           "launcher GO was not preceded by owned Job assignment")
    streams = run_record.get("streams")
    _check(isinstance(streams, dict) and set(streams) == {"stdout.jsonl", "stderr.txt"}, "raw stdio identity missing")
    raw_streams = {}
    for name in streams:
        with (directory / name).open("rb") as stream:
            raw = stream.read(1024 * 1024 + 1)
        _check(len(raw) <= 1024 * 1024 and streams[name] == {"bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()},
               "raw native stream was lost, changed or exceeded its schema bound")
        raw_streams[name] = raw
    lines = raw_streams["stdout.jsonl"].splitlines()
    _check(len(lines) == 2 and all(lines), "native shell/inner Job JSONL reports are incomplete")
    shell, launcher = (json.loads(line.decode("utf-8")) for line in lines)
    count = 3 if mode == MODES[2] else 2
    _check(shell == {"valid": True, "mode": mode, "sessions_closed": count, "active_children_after": 0, "presented_done": None} and shell.get("valid") is True and
           type(shell["sessions_closed"]) is int and type(shell["active_children_after"]) is int,
           "native product sessions did not close normally")
    _check(isinstance(launcher, dict) and launcher.get("schema") == "termforge-hosted-ime-launcher-v1" and launcher.get("mode") == mode and
           type(launcher.get("child_exit_code")) is int and launcher["child_exit_code"] == 0 and launcher.get("child_signaled") is True and
           launcher.get("timed_out") is False and _job(launcher.get("inner_job_after"), 1),
           "launcher did not observe its actual child and inner Job empty")
    captures = {}
    for name in ["bottom", "main"] + (["hidden"] if mode == MODES[2] else []):
        with (directory / (name + ".bin")).open("rb") as stream:
            captures[name] = stream.read(16385)
        _check(len(captures[name]) <= 16384, "real receiver exceeded its independent byte bound")
    result = validate_product_manifest(_read_json(directory / "product-ime.json"), mode, captures)
    with (directory / "frame.bmp").open("rb") as stream:
        header = stream.read(54)
    _check(len(header) == 54 and header[:2] == b"BM", "actual product window frame missing")
    size, offset, dib, width, height = struct.unpack_from("<IxxxxIIii", header, 2)
    planes, bits, compression = struct.unpack_from("<HHI", header, 26)
    _check(dib == 40 and 0 < width <= 8192 and 0 < abs(height) <= 8192 and planes == 1 and bits == 32 and compression == 0 and
           offset == 54 and size == (directory / "frame.bmp").stat().st_size and size == offset + width * abs(height) * 4,
           "actual product window frame is truncated or has unexpected geometry/format")
    result["inner_and_outer_job_zero_verified"] = True
    result["inner_job_after"] = launcher["inner_job_after"]
    result["raw_capture_identities"] = {name + ".bin": {"bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}
                                        for name, raw in captures.items()}
    return result
