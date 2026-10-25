"""Independent real-console alternate-screen restoration and history checks."""
import hashlib
import json
from pathlib import Path

from experiments.shell.workflow_checks import validate_console_window


STAGES = {
    "before": ("main-before", "candidate-alt.before"),
    "alt1": ("alt-one", "candidate-alt.alt1"),
    "restored1": ("main-restored", "candidate-alt.restored1"),
    "alt2": ("alt-two", "candidate-alt.alt2"),
    "final": ("main-final", "candidate-alt.final"),
}


def _require(condition, message):
    if not condition:
        raise ValueError(message)


def expected_source():
    return (
        "\x1b[2J\x1b[H\x1b[32mMAIN_GREEN\x1b[0m\r\n"
        "\x1b[?1049h\x1b[34mALT_ONE\x1b[0m\x1b[?1049l"
        "MAIN_AFTER\r\n"
        "\x1b[?1049h\x1b[31mALT_TWO\x1b[0m\x1b[?1049l"
        "TF_ALT_DONE\r\n"
    ).encode("ascii")


def _row(lines, text):
    matches = [(row, line.find(text)) for row, line in enumerate(lines) if text in line]
    _require(len(matches) == 1 and matches[0][1] >= 0, "console text missing or duplicated: " + text)
    return matches[0]


def _native_style(console, text, foreground):
    row, column = _row(console["lines"], text)
    for index, char in enumerate(text):
        codepoint, attributes = console["cells"][row][column + index]
        _require(codepoint == ord(char) and attributes & 0xf == foreground,
                 "native character or style differs: " + text)


def validate_candidate_alternate(data, consoles, source, transport):
    _require(source == expected_source(), "alternate source differs")
    _require(type(transport) is bytes and 0 < len(transport) <= 8192, "alternate transport bound")
    _require(transport.count(b"\x1b[?1049h") == 2 and transport.count(b"\x1b[?1049l") == 2,
             "alternate enter/exit transport count")
    position = 0
    for text in ("MAIN_GREEN", "ALT_ONE", "MAIN_AFTER", "ALT_TWO", "TF_ALT_DONE"):
        position = transport.find(text.encode("ascii"), position)
        _require(position >= 0, "alternate transport text or ordering")
        position += len(text)
    _require(type(consoles) is dict and set(consoles) == set(STAGES), "alternate console stage missing")
    before, restored = consoles["before"], consoles["restored1"]
    _require(before["lines"] == restored["lines"] and before["cells"] == restored["cells"] and
             before["columns"] == restored["columns"] and before["rows"] == restored["rows"] and
             before["top"] == restored["top"] and
             before["cursor_column"] == restored["cursor_column"] and before["cursor_row"] == restored["cursor_row"],
             "native main screen, style or cursor was not restored")
    _native_style(before, "MAIN_GREEN", 2)
    _native_style(consoles["alt1"], "ALT_ONE", 1)
    _native_style(restored, "MAIN_GREEN", 2)
    _native_style(consoles["alt2"], "ALT_TWO", 4)
    for stage in ("alt1", "alt2"):
        _require("MAIN_GREEN" not in consoles[stage]["lines"] and
                 "MAIN_AFTER" not in consoles[stage]["lines"], "main text leaked into alternate screen")
    final = consoles["final"]
    _native_style(final, "MAIN_GREEN", 2)
    main_rows = [_row(final["lines"], text)[0] for text in ("MAIN_GREEN", "MAIN_AFTER", "TF_ALT_DONE")]
    _require(main_rows[0] < main_rows[1] < main_rows[2], "main text order or completion differs")
    _require(not any("ALT_ONE" in line or "ALT_TWO" in line for line in final["lines"]),
             "alternate text leaked into native main screen")
    _require(not any(final["lines"][main_rows[2] + 1:]), "unexpected native output after completion")
    _require(final["cursor_column"] == 0 and final["cursor_row"] == main_rows[2] + 1,
             "native final cursor differs")
    _require(type(data) is dict and data.get("schema") == "termforge-candidate-alt-v1" and
             data.get("columns") == final["columns"] and data.get("rows") == final["rows"] and
             data.get("alternate_enabled") is False and data.get("frame_matches_full") is True and
             data.get("presented_done") is None and data.get("default_fg") == -1 and data.get("default_bg") == -1 and
             type(data.get("cursor_row")) is int and 0 <= data["cursor_row"] < data["rows"] and
             type(data.get("cursor_column")) is int and 0 <= data["cursor_column"] < data["columns"] and
             data["cursor_row"] == final["cursor_row"] - final["top"] and
             data["cursor_column"] == final["cursor_column"],
             "candidate mode, style, cursor or frame differs")
    visible = data.get("visible_lines")
    document = data.get("document_lines")
    cells = data.get("styled_cells")
    _require(type(visible) is list and len(visible) == data["rows"] and all(type(line) is str for line in visible) and
             type(document) is list and len(document) <= data["rows"] + 100 and
             all(type(line) is str for line in document) and type(cells) is list,
             "candidate visible/document/style projection missing")
    for text in ("MAIN_GREEN", "MAIN_AFTER", "TF_ALT_DONE"):
        _require(sum(line == text for line in visible) == 1 and sum(line == text for line in document) == 1,
                 "candidate main text missing or duplicated: " + text)
    _require(not any("ALT_ONE" in line or "ALT_TWO" in line for line in visible + document),
             "alternate text polluted candidate history")
    row, column = _row(visible, "MAIN_GREEN")
    for index, char in enumerate("MAIN_GREEN"):
        matches = [cell for cell in cells if type(cell) is dict and cell.get("row") == row and
                   cell.get("column") == column + index]
        _require(len(matches) == 1 and matches[0].get("text") == char and matches[0].get("fg") == 2 and
                 matches[0].get("bg") == -1 and matches[0].get("bold") is False,
                 "candidate restored main style differs")
    return {"scoped_validation_passed": True, "native_restore_verified": True,
            "history_isolated": True, "repeated_alternate_switch_count": 2,
            "alternate_reentry_previous_content_retained": "ALT_ONE" in consoles["alt2"]["lines"],
            "scroll_region_verified": False, "resize_cross_verified": False,
            "full_r2_contract_verified": False, "presented_done": None,
            "source_sha256": hashlib.sha256(source).hexdigest(),
            "transport_sha256": hashlib.sha256(transport).hexdigest()}


def _bounded(path, limit):
    _require(path.is_file() and not path.is_symlink(), "missing or linked alternate evidence")
    with path.open("rb") as stream:
        value = stream.read(limit + 1)
    _require(len(value) <= limit, "oversized alternate evidence")
    return value


def validate_alternate_output(directory):
    directory = Path(directory)
    data = json.loads(_bounded(directory / "candidate-alt.json", 512000).decode("utf-8"))
    consoles = {stage: validate_console_window(directory, expected_role=role, expected_generation=1, prefix=prefix)
                for stage, (role, prefix) in STAGES.items()}
    source = _bounded(directory / "candidate-alt.source.bin", 4096)
    transport = _bounded(directory / "candidate-alt.transport.bin", 8192)
    return validate_candidate_alternate(data, consoles, source, transport)
