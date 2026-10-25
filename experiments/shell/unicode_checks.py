"""Independent complex-text console, candidate-cell and DWrite evidence checks."""
import hashlib
import json
import math
from pathlib import Path
import struct

from experiments.shell.workflow_checks import validate_console_window


SAMPLES = (
    ("TF_U_DECOMPOSE", "e\u0301", 1),
    ("TF_U_EMOJI", "🙂", 2),
    ("TF_U_ZWJ", "👩\u200d💻", None),
    ("TF_U_KEYCAP", "1\ufe0f\u20e3", None),
)
DONE = "TF_U_DONE"


def _require(condition, message):
    if not condition:
        raise ValueError(message)


def _utf16_units(text):
    payload = text.encode("utf-16-le")
    return list(struct.unpack("<" + "H" * (len(payload) // 2), payload))


def expected_source():
    return b"".join((label + " " + text + "\r\n").encode("utf-8")
                    for label, text, _ in SAMPLES) + (DONE + "\r\n").encode("ascii")


def _native_console_text(text):
    return "".join("\ufffd" if ord(char) > 0xffff else char for char in text)


def _cell_observations(cells, columns, dpi, cell_width):
    _require(type(cells) is list, "candidate cells are not a list")
    for cell in cells:
        _require(type(cell) is dict and cell.get("written") is True and
                 type(cell.get("column")) is int and 0 <= cell["column"] < columns and
                 type(cell.get("width")) is int and cell["width"] in (0, 1, 2) and
                 type(cell.get("marks")) is int and cell["marks"] >= 0 and
                 type(cell.get("text")) is str, "candidate cell identity or text")
        if not cell["width"]:
            _require(cell["text"] in ("", " "), "wide trailing cell contains unexpected text")
            continue
        if cell["text"] == " ":
            continue
        faces = cell.get("faces")
        width = cell.get("width_dip")
        _require(type(cell.get("glyphs")) is int and cell["glyphs"] > 0 and
                 type(cell.get("missing")) is int and cell["missing"] == 0 and
                 type(faces) is list and faces and all(type(face) is str and face for face in faces) and
                 type(width) in (int, float) and math.isfinite(width) and 0 < width <= cell["width"] * cell_width * 96 / dpi + 0.01,
                 "actual DWrite glyph, missing-glyph, font or grid-width check")


def validate_candidate_unicode(data, console, source, transport):
    _require(source == expected_source(), "Unicode source literal differs")
    _require(type(transport) is bytes and 0 < len(transport) <= 8192, "Unicode transport bound")
    position = 0
    for label, text, _ in SAMPLES:
        position = transport.find(label.encode("ascii"), position)
        _require(position >= 0, "Unicode transport label missing")
        position = transport.find(text.encode("utf-8"), position + len(label))
        _require(position >= 0, "Unicode transport scalar sequence missing")
    _require(transport.find(DONE.encode("ascii"), position) >= 0, "Unicode transport completion missing")
    actual_lines = console["lines"]
    for row, (label, text, _) in enumerate(SAMPLES):
        _require(actual_lines[row] in (label + " " + text, label + " " + _native_console_text(text)),
                 "native console literal differs from both known projections")
    _require(actual_lines[len(SAMPLES)] == DONE and
             not any(actual_lines[len(SAMPLES) + 1:]), "real console completion boundary")
    console_samples = []
    for row, (label, text, _) in enumerate(SAMPLES):
        original = actual_lines[row] == label + " " + text
        projected = text if original else _native_console_text(text)
        cells = console["cells"][row]
        expected = _utf16_units(label + " ")
        for char in text:
            expected.extend([0xfffd, 0xfffd] if not original and ord(char) > 0xffff else _utf16_units(char))
        _require([codepoint for codepoint, _ in cells] == expected + [32] * (console["columns"] - len(expected)),
                 "real console UTF-16 cells differ from the native projection")
        prefix_units = len(label) + 1
        sample_cells = cells[prefix_units:len(expected)]
        console_samples.append({"label": label, "projection": "original" if original else "replacement-non-bmp",
                                "widths": [2 if attributes & 0x100 else 0 if attributes & 0x200 else 1
                                           for _, attributes in sample_cells]})
    _require(type(data) is dict and data.get("schema") == "termforge-candidate-unicode-v1" and
             data.get("columns") == console["columns"] and data.get("rows") == console["rows"] and
             type(data.get("dpi")) is int and 48 <= data["dpi"] <= 768 and
             type(data.get("cell_width")) is int and data["cell_width"] > 0 and
             type(data.get("cell_height")) is int and data["cell_height"] > 0 and
             data.get("frame_matches_full") is True and data.get("presented_done") is None and
             type(data.get("lines")) is list and len(data["lines"]) == data["rows"],
             "candidate window identity, geometry or frame")
    lines = data["lines"]
    _require(all(type(line) is dict and line.get("row") == row and type(line.get("text")) is str
                 for row, line in enumerate(lines)), "candidate visible rows")
    for line in lines:
        _cell_observations(line.get("cells"), data["columns"], data["dpi"], data["cell_width"])
    accepted = []
    for label, text, width in SAMPLES:
        expected_line = label + " " + text
        matches = [line for line in lines if line["text"] == expected_line]
        _require(len(matches) == 1, "candidate literal is missing, changed or duplicated: " + label)
        column = len(label) + 1
        cells = sorted((cell for cell in matches[0]["cells"] if cell["column"] >= column and cell["width"]),
                       key=lambda cell: cell["column"])
        _require(cells and cells[0]["column"] == column and
                 "".join(cell["text"] for cell in cells) == text,
                 "candidate scalar preservation differs: " + label)
        for previous, following in zip(cells, cells[1:]):
            _require(following["column"] == previous["column"] + previous["width"],
                     "candidate scalar cells are not contiguous: " + label)
        cell = cells[0]
        if width is not None:
            _require(len(cells) == 1 and cell["text"] == text and cell["width"] == width,
                     "candidate established cell policy differs: " + label)
        if label == "TF_U_DECOMPOSE":
            _require(cell["marks"] == 1, "combining mark is not attached to its base")
        if width == 2:
            _require(any(next_cell["column"] == column + 1 and next_cell["width"] == 0
                         for next_cell in matches[0]["cells"]), "wide scalar lacks its trailing cell: " + label)
        accepted.append({"label": label, "text": text, "cells": [
            {"column": value["column"], "width": value["width"], "text": value["text"],
             "glyphs": value["glyphs"], "faces": value["faces"]} for value in cells]})
    return {"samples": accepted, "console_samples": console_samples,
            "source_sha256": hashlib.sha256(source).hexdigest(),
            "transport_sha256": hashlib.sha256(transport).hexdigest(),
            "transport_bytes": len(transport), "scoped_validation_passed": True,
            "scope": "scalar-preservation-and-glyph-only",
            "complex_grapheme_rendering_verified": False,
            "whole_cluster_width_policy_verified": False,
            "complete_unicode_ime_verified": False, "presented_done": None}


def _bounded(path, limit):
    _require(path.is_file() and not path.is_symlink(), "missing or linked Unicode evidence")
    with path.open("rb") as stream:
        value = stream.read(limit + 1)
    _require(len(value) <= limit, "oversized Unicode evidence")
    return value


def validate_unicode_output(directory):
    directory = Path(directory)
    data = json.loads(_bounded(directory / "candidate-unicode.json", 512000).decode("utf-8"))
    console = validate_console_window(directory, expected_role="unicode", expected_generation=1,
                                     prefix="candidate-unicode")
    source = _bounded(directory / "candidate-unicode.source.bin", 4096)
    transport = _bounded(directory / "candidate-unicode.transport.bin", 8192)
    return validate_candidate_unicode(data, console, source, transport)
