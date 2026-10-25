"""Independent DECSTBM region, fixed-content and retained-history checks."""
import hashlib
import json
from pathlib import Path

from experiments.shell.workflow_checks import validate_console_window


STAGES = {
    "before": ("main-before", "candidate-scroll.before"),
    "region": ("main-region", "candidate-scroll.region"),
    "final": ("main-final", "candidate-scroll.final"),
}


def _require(condition, message):
    if not condition:
        raise ValueError(message)


def expected_source():
    return (
        "\x1b[2J\x1b[HTOP_FIXED\r\nREGION_A\r\nREGION_B\r\nBOTTOM_FIXED\r\n"
        "\x1b[2;3r\x1b[3;1H\nREGION_C\nREGION_D"
        "\x1b[r\x1b[5;1HTF_SCROLL_DONE\r\n"
    ).encode("ascii")


def validate_candidate_scroll(data, consoles, source, transport):
    _require(source == expected_source(), "scroll source differs")
    _require(type(transport) is bytes and 0 < len(transport) <= 8192, "scroll transport bound")
    _require(type(consoles) is dict and set(consoles) == set(STAGES), "scroll native stage missing")
    before = consoles["before"]
    _require(before["lines"][:4] == ["TOP_FIXED", "REGION_A", "REGION_B", "BOTTOM_FIXED"] and
             not any(before["lines"][4:]), "scroll initial fixture differs")
    region = consoles["region"]
    _require(region["lines"][0] == "TOP_FIXED" and region["lines"][1] == "REGION_C" and
             region["lines"][2] == "REGION_D" and region["lines"][3] == "BOTTOM_FIXED" and
             not any("REGION_A" in line or "REGION_B" in line for line in region["lines"]),
             "native scroll region, fixed content or discarded rows differ")
    _require(region["top"] == 0 and region["cursor_row"] == 2 and region["cursor_column"] == 8,
             "native scroll-region cursor differs")
    final = consoles["final"]
    _require(final["lines"][:5] == ["TOP_FIXED", "REGION_C", "REGION_D", "BOTTOM_FIXED", "TF_SCROLL_DONE"] and
             not any(final["lines"][5:]) and not any("REGION_A" in line or "REGION_B" in line for line in final["lines"]),
             "native final scroll content or discarded rows differ")
    _require(final["top"] == 0 and final["cursor_row"] == 5 and final["cursor_column"] == 0,
             "native final scroll cursor differs")
    # ConPTY may repaint rather than serialize DECSTBM. Replay checks the actual output state, not a command echo.
    from importlib.metadata import version as package_version
    _require(package_version("pyte") == "0.8.2", "scroll independent model version")
    import pyte
    model = pyte.Screen(final["columns"], final["rows"])
    pyte.ByteStream(model).feed(transport)
    model_lines = [line.rstrip(" ") for line in model.display]
    _require(model_lines == final["lines"], "transport replay differs from native final screen")
    _require(model.cursor.x == final["cursor_column"] and model.cursor.y == final["cursor_row"] - final["top"],
             "transport replay cursor differs")
    _require(type(data) is dict and data.get("schema") == "termforge-candidate-scroll-v1" and
             data.get("columns") == final["columns"] and data.get("rows") == final["rows"] and
             data.get("alternate_enabled") is False and data.get("default_fg") == -1 and data.get("default_bg") == -1 and
             data.get("frame_matches_full") is True and data.get("presented_done") is None and
             type(data.get("cursor_row")) is int and type(data.get("cursor_column")) is int and
             data["cursor_row"] == final["cursor_row"] - final["top"] and
             data["cursor_column"] == final["cursor_column"],
             "candidate mode, style, cursor or frame differs")
    visible = data.get("visible_lines")
    document = data.get("document_lines")
    cells = data.get("styled_cells")
    _require(type(visible) is list and visible == final["lines"], "candidate visible lines differ from native final")
    _require(type(document) is list and len(document) <= data["rows"] + 100 and
             all(type(line) is str for line in document) and type(cells) is list,
             "candidate retained document or cell projection")
    for text in ("TOP_FIXED", "REGION_C", "REGION_D", "BOTTOM_FIXED", "TF_SCROLL_DONE"):
        _require(sum(line == text for line in document) == 1,
                 "candidate retained main text missing or duplicated: " + text)
    _require(not any("REGION_A" in line or "REGION_B" in line for line in document),
             "local-scroll source was incorrectly restored into history")
    _require(all(type(cell) is dict and cell.get("fg") == -1 and cell.get("bg") == -1 and cell.get("bold") is False
                 for cell in cells), "candidate default style differs")
    return {"scoped_validation_passed": True, "scroll_region_verified": True,
            "outside_preserved": True, "local_scroll_history_verified": True,
            "native_viewport_top": final["top"], "origin_and_wrap_modes_verified": False,
            "margins_per_buffer_verified": False, "resize_cross_verified": False,
            "full_r2_contract_verified": False, "presented_done": None,
            "source_sha256": hashlib.sha256(source).hexdigest(),
            "transport_sha256": hashlib.sha256(transport).hexdigest(),
            "transport_bytes": len(transport)}


def _bounded(path, limit):
    _require(path.is_file() and not path.is_symlink(), "missing or linked scroll evidence")
    with path.open("rb") as stream:
        value = stream.read(limit + 1)
    _require(len(value) <= limit, "oversized scroll evidence")
    return value


def validate_scroll_output(directory):
    directory = Path(directory)
    data = json.loads(_bounded(directory / "candidate-scroll.json", 512000).decode("utf-8"))
    consoles = {stage: validate_console_window(directory, expected_role=role, expected_generation=1, prefix=prefix)
                for stage, (role, prefix) in STAGES.items()}
    source = _bounded(directory / "candidate-scroll.source.bin", 4096)
    transport = _bounded(directory / "candidate-scroll.transport.bin", 8192)
    return validate_candidate_scroll(data, consoles, source, transport)
