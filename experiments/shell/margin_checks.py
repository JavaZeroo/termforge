"""Real margin observations; reproducing a ConPTY difference is not capability acceptance."""
import hashlib
import json
from pathlib import Path

from experiments.shell.workflow_checks import validate_console_window


STAGES = {
    "before": ("main-before", "candidate-margins.before"),
    "alt": ("alt-region", "candidate-margins.alt"),
    "restored": ("main-restored", "candidate-margins.restored"),
    "final": ("main-final", "candidate-margins.final"),
}


def _require(condition, message):
    if not condition:
        raise ValueError(message)


def expected_source():
    return (
        "\x1b[2J\x1b[HTOP_FIXED\r\nREGION_A\r\nREGION_B\r\nBOTTOM_FIXED\r\n\x1b[2;3r"
        "\x1b[?1049hALT_OUTSIDE\r\n\x1b[4;1HALT_A\r\nALT_B\x1b[4;5r\x1b[5;1H\nALT_C\nALT_D"
        "\x1b[?1049l"
        "\x1b[3;1H\nREGION_C\nREGION_D\x1b[6;1HTF_MARGIN_DONE\r\n"
    ).encode("ascii")


def validate_candidate_margins(data, consoles, source, transport):
    _require(source == expected_source(), "margins source differs")
    _require(type(transport) is bytes and 0 < len(transport) <= 8192, "margins transport bound")
    _require(transport.count(b"\x1b[?1049h") == 1 and transport.count(b"\x1b[?1049l") == 1,
             "margins alternate enter/exit transport")
    _require(type(consoles) is dict and set(consoles) == set(STAGES), "margins native stage missing")
    before, alt, restored, final = (consoles[name] for name in ("before", "alt", "restored", "final"))
    _require(before["lines"][:4] == ["TOP_FIXED", "REGION_A", "REGION_B", "BOTTOM_FIXED"] and
             not any(before["lines"][4:]), "margins initial fixture differs")
    _require(alt["lines"][:5] == ["ALT_OUTSIDE", "", "", "ALT_C", "ALT_D"] and
             not any(alt["lines"][5:]) and not any("ALT_A" in line or "ALT_B" in line for line in alt["lines"]) and
             not any("TOP_FIXED" in line or "REGION_A" in line or "REGION_B" in line for line in alt["lines"]),
             "alternate margins, fixed content or discarded rows differ")
    _require(alt["top"] == 0 and alt["cursor_row"] == 4 and alt["cursor_column"] == len("ALT_D"),
             "alternate margin cursor differs")
    _require(before["lines"] == restored["lines"] and before["cells"] == restored["cells"] and
             before["columns"] == restored["columns"] and before["rows"] == restored["rows"] and
             before["top"] == restored["top"] and before["cursor_column"] == restored["cursor_column"] and
             before["cursor_row"] == restored["cursor_row"], "native main restoration differs")
    observed = ["TOP_FIXED", "REGION_A", "REGION_B", "REGION_CIXED", "REGION_D", "TF_MARGIN_DONE"]
    required = ["TOP_FIXED", "REGION_C", "REGION_D", "BOTTOM_FIXED", "", "TF_MARGIN_DONE"]
    _require(final["lines"][:6] == observed and not any(final["lines"][6:]) and
             not any("ALT_" in line for line in final["lines"]),
             "observed ConPTY main-margin loss or completion differs")
    _require(final["lines"][:6] != required, "ConPTY margin behavior changed; compare it before accepting")
    _require(final["top"] == 0 and final["cursor_row"] == 6 and final["cursor_column"] == 0,
             "main final margin cursor differs")
    _require(type(data) is dict and data.get("schema") == "termforge-candidate-margins-v1" and
             data.get("columns") == final["columns"] and data.get("rows") == final["rows"] and
             data.get("alternate_enabled") is False and data.get("default_fg") == -1 and data.get("default_bg") == -1 and
             data.get("frame_matches_full") is True and data.get("presented_done") is None and
             type(data.get("cursor_row")) is int and type(data.get("cursor_column")) is int and
             data["cursor_row"] == final["cursor_row"] - final["top"] and
             data["cursor_column"] == final["cursor_column"],
             "candidate mode, style, cursor or frame differs")
    visible, document, cells = data.get("visible_lines"), data.get("document_lines"), data.get("styled_cells")
    _require(type(visible) is list and visible == final["lines"], "candidate visible lines differ from native final")
    _require(type(document) is list and len(document) <= data["rows"] + 100 and
             all(type(line) is str for line in document) and type(cells) is list,
             "candidate retained document or styles missing")
    for text in observed:
        _require(sum(line == text for line in document) == 1,
                 "candidate main text missing or duplicated: " + text)
    _require(not any("ALT_" in line for line in document), "alternate text polluted main history")
    _require(all(type(cell) is dict and cell.get("fg") == -1 and cell.get("bg") == -1 and cell.get("bold") is False
                 for cell in cells), "candidate default style differs")
    return {"scoped_validation_passed": True, "observation_only": True,
            "margins_per_buffer_verified": False, "native_main_margins_restored": False,
            "conpty_margin_difference_observed": True, "candidate_matches_native": True,
            "alternate_scroll_region_verified": True, "scroll_region_verified": False,
            "outside_preserved": False, "native_main_restored": True,
            "native_viewport_top": final["top"], "windterm_runtime_equivalence_verified": False,
            "origin_and_wrap_modes_verified": False, "resize_cross_verified": False,
            "full_r2_contract_verified": False, "presented_done": None,
            "source_sha256": hashlib.sha256(source).hexdigest(),
            "transport_sha256": hashlib.sha256(transport).hexdigest(),
            "transport_bytes": len(transport)}


def _bounded(path, limit):
    _require(path.is_file() and not path.is_symlink(), "missing or linked margins evidence")
    with path.open("rb") as stream:
        value = stream.read(limit + 1)
    _require(len(value) <= limit, "oversized margins evidence")
    return value


def validate_margins_output(directory):
    directory = Path(directory)
    data = json.loads(_bounded(directory / "candidate-margins.json", 512000).decode("utf-8"))
    consoles = {stage: validate_console_window(directory, expected_role=role, expected_generation=1, prefix=prefix)
                for stage, (role, prefix) in STAGES.items()}
    source = _bounded(directory / "candidate-margins.source.bin", 4096)
    transport = _bounded(directory / "candidate-margins.transport.bin", 8192)
    return validate_candidate_margins(data, consoles, source, transport)
