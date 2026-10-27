"""Literal native outcomes for independent source ConPTY control workloads."""
from __future__ import annotations


def expected_source(case, columns, rows):
    expected_native(case, columns, rows)  # Fixed workload/grid validation.
    parts = [b"\x1b[2J\x1b[HTOP_FIXED\r\nREGION_A\r\nREGION_B\r\nBOTTOM_FIXED\r\n\x1b[2;3r"]
    if case != "main-only":
        if case == "repeat-alt":
            parts.append(b"\x1b[?1049hALT_OUTSIDE\r\n\x1b[4;1HALT_A\r\nALT_B\x1b[4;5r\x1b[5;1H\nALT_C\nALT_D")
        parts.append(b"\x1b[?1049h\x1b[HALT_TOP\x1b[" + str(rows).encode("ascii") + b";1HALT_LAST\r\nALT_DONE")
        parts.append(b"\x1b[?1049l\x1b[?1049l" if case == "repeat-alt" else b"\x1b[?1049l")
    parts.append(b"\x1b[3;1H\nREGION_C\nREGION_D\x1b[6;1HCTRL_DONE\r\n")
    return b"".join(parts)


def expected_native(case, columns, rows):
    if (columns, rows) not in ((80, 24), (12, 7)) or case not in ("main-only", "default-alt", "repeat-alt"):
        raise ValueError("fixed native control and grid required")

    def state(lines, cursor):
        return {"lines": lines + [""] * (rows - len(lines)), "cursor": cursor}

    before = state(["TOP_FIXED", "REGION_A", "REGION_B", "BOTTOM_FIXED"], [0, 0])
    result = {"before": before}
    if case != "main-only":
        if case == "repeat-alt":
            result["alt1"] = state(["ALT_OUTSIDE", "", "", "ALT_C", "ALT_D"], [4, 5])
        result["alt2" if case == "repeat-alt" else "alt"] = state(
            [""] * (rows - 2) + ["ALT_LAST", "ALT_DONE"], [rows - 1, 8])
        result["restored"] = before
    result["final"] = state(["TOP_FIXED", "REGION_C", "REGION_D", "BOTTOM_FIXED", "", "CTRL_DONE"], [6, 0])
    return result


def _evaluate(stages, expected, columns, rows):
    if set(stages) != set(expected):
        raise ValueError("native control stages differ")
    for capture in stages.values():
        if (capture["columns"], capture["rows"]) != (columns, rows):
            raise ValueError("native control grid changed")
    # No control selects SGR. Compare all cells against the initial attribute;
    # this checks preservation, without choosing a portable default color.
    attribute = stages["before"]["cells"][0][0][1]
    results = {}
    for name, target in expected.items():
        actual = stages[name]
        wanted = [[(ord(character), attribute) for character in line.ljust(columns)]
                  for line in target["lines"]]
        fields = {"text": actual["lines"] == target["lines"],
                  "cell_text": actual["cell_lines"] == target["lines"],
                  "cells_and_attributes": actual["cells"] == wanted,
                  "cursor": [actual["cursor_row"], actual["cursor_column"]] == target["cursor"],
                  "viewport": actual["top"] == 0}
        results[name] = dict(fields, passed=all(fields.values()))
    return {"stages": results, "passed": all(row["passed"] for row in results.values()),
            "initial_native_attribute": attribute, "full_r2_contract_verified": False}


def evaluate_native(stages, case, columns, rows):
    return _evaluate(stages, expected_native(case, columns, rows), columns, rows)


def matches_unpatched_prediction(stages, case, columns, rows):
    expected = expected_native(case, columns, rows)
    if case != "main-only":
        # With the main region still active, LF at the physical bottom clamps
        # instead of scrolling. ALT_DONE overwrites ALT_LAST; ALT_TOP remains.
        expected["alt2" if case == "repeat-alt" else "alt"] = {
            "lines": ["ALT_TOP"] + [""] * (rows - 2) + ["ALT_DONE"], "cursor": [rows - 1, 8]}
    if case == "repeat-alt":
        expected["final"] = {"lines": ["TOP_FIXED", "REGION_A", "REGION_B", "REGION_CIXED", "REGION_D", "CTRL_DONE"] + [""] * (rows - 6),
                             "cursor": [6, 0]}
    return _evaluate(stages, expected, columns, rows)["passed"]
