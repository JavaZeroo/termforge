"""Literal native outcomes for independent source ConPTY control workloads."""
from __future__ import annotations


def expected_source(case, columns, rows):
    expected_native(case, columns, rows)  # Fixed workload/grid validation.
    if case == "styled-cursor":
        return (b"\x1b[0m\x1b[2J\x1b[HTOP_FIXED\r\nREGION_A\r\nREGION_B\r\nBOTTOM_FIXED\r\n"
                b"\x1b[2;3r\x1b[31;44m\x1b[3;6H"
                b"\x1b[?1049h\x1b[0m\x1b[2J\x1b[HALT_OUTSIDE\r\n\x1b[4;1HALT_A\r\nALT_B"
                b"\x1b[4;5r\x1b[32;45m\x1b[5;1H\nALT_C\nALT_D\x1b[5;8H"
                b"\x1b[?1049lM\x1b[3;1H\nREGION_C\nREGION_D\x1b[6;1HSGR_DONE")
    parts = [b"\x1b[2J\x1b[HTOP_FIXED\r\nREGION_A\r\nREGION_B\r\nBOTTOM_FIXED\r\n\x1b[2;3r"]
    if case != "main-only":
        if case == "repeat-alt":
            parts.append(b"\x1b[?1049hALT_OUTSIDE\r\n\x1b[4;1HALT_A\r\nALT_B\x1b[4;5r\x1b[5;1H\nALT_C\nALT_D")
        parts.append(b"\x1b[?1049h\x1b[HALT_TOP\x1b[" + str(rows).encode("ascii") + b";1HALT_LAST\r\nALT_DONE")
        parts.append(b"\x1b[?1049l\x1b[?1049l" if case == "repeat-alt" else b"\x1b[?1049l")
    parts.append(b"\x1b[3;1H\nREGION_C\nREGION_D\x1b[6;1HCTRL_DONE\r\n")
    return b"".join(parts)


def expected_native(case, columns, rows):
    if (columns, rows) not in ((80, 24), (12, 7)) or case not in ("main-only", "default-alt", "repeat-alt", "styled-cursor"):
        raise ValueError("fixed native control and grid required")

    def state(lines, cursor):
        return {"lines": lines + [""] * (rows - len(lines)), "cursor": cursor}

    if case == "styled-cursor":
        before = state(["TOP_FIXED", "REGION_A", "REGION_B", "BOTTOM_FIXED"], [2, 5])
        alternate = state(["ALT_OUTSIDE", "", "", "ALT_C", "ALT_D"], [4, 7])
        alternate["attribute_spans"] = [(3, 0, columns, 0x52), (4, 0, columns, 0x52)]
        after_write = state(["TOP_FIXED", "REGION_A", "REGIOM_B", "BOTTOM_FIXED"], [2, 6])
        after_write["attribute_spans"] = [(2, 5, 6, 0x14)]
        final = state(["TOP_FIXED", "REGION_C", "REGION_D", "BOTTOM_FIXED", "", "SGR_DONE"], [5, 8])
        final["attribute_spans"] = [(1, 0, columns, 0x14), (2, 0, columns, 0x14), (5, 0, 8, 0x14)]
        return {"before": before, "alt": alternate, "restored": before, "after-write": after_write, "final": final}

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
    # The initial default is observed, while explicit SGR colors below use
    # independent Win32 attribute values, including erased trailing cells.
    attribute = stages["before"]["cells"][0][0][1]
    results = {}
    for name, target in expected.items():
        actual = stages[name]
        wanted = [[(ord(character), attribute) for character in line.ljust(columns)]
                  for line in target["lines"]]
        for row, begin, end, explicit in target.get("attribute_spans", ()):
            for column in range(begin, end):
                wanted[row][column] = (wanted[row][column][0], explicit)
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


def evaluate_terminal(record, case, columns, rows):
    target = expected_native(case, columns, rows)["final"]
    screen = record["screen"]
    cells = screen["cells"]
    lines = ["".join(cell["text"] for cell in line if cell.get("width", 1)).rstrip() for line in cells]
    alternate = bool(record["mode_bits"] & (1 << 12))
    fields = {"text": lines == target["lines"], "main_screen": not alternate}
    if case == "styled-cursor":
        # The terminal trace uses ANSI palette indices; CHAR_INFO uses Win32
        # foreground/background bits. Keep their literal oracles independent.
        default = {"fg": -1, "bg": -1, "bold": False}
        explicit = {"fg": 1, "bg": 4, "bold": False}
        # Match the trace's existing empty-cell encoding, while still requiring
        # explicit foreground and background on the colored scroll blanks.
        wanted = [[{"text": character if column < len(line) else "",
                    "width": 1 if column < len(line) else 0, "style": default.copy()}
                   for column, character in enumerate(line.ljust(columns))] for line in target["lines"]]
        for row, begin, end in ((1, 0, columns), (2, 0, columns), (5, 0, 8)):
            for column in range(begin, end):
                cell = wanted[row][column]
                cell.update(text=cell["text"] or " ", width=1, style=explicit.copy())
        fields.update(cells_and_styles=cells == wanted,
                      cursor=[screen["row"], screen["column"]] == [5, 8],
                      current_style=screen["current_style"] == explicit)
    return {"fields": fields, "passed": all(fields.values()), "lines": lines, "alternate": alternate}


def matches_unpatched_prediction(stages, case, columns, rows):
    expected = expected_native(case, columns, rows)
    if case == "styled-cursor":
        expected["final"] = {
            "lines": ["TOP_FIXED", "REGION_A", "REGIOM_B", "REGION_CIXED", "REGION_D", "SGR_DONE"] + [""] * (rows - 6),
            "cursor": [5, 8],
            "attribute_spans": [(2, 5, 6, 0x14), (3, 0, 8, 0x14), (4, 0, 8, 0x14), (5, 0, 8, 0x14)],
        }
    elif case != "main-only":
        # With the main region still active, LF at the physical bottom clamps
        # instead of scrolling. ALT_DONE overwrites ALT_LAST; ALT_TOP remains.
        expected["alt2" if case == "repeat-alt" else "alt"] = {
            "lines": ["ALT_TOP"] + [""] * (rows - 2) + ["ALT_DONE"], "cursor": [rows - 1, 8]}
    if case == "repeat-alt":
        expected["final"] = {"lines": ["TOP_FIXED", "REGION_A", "REGION_B", "REGION_CIXED", "REGION_D", "CTRL_DONE"] + [""] * (rows - 6),
                             "cursor": [6, 0]}
    return _evaluate(stages, expected, columns, rows)["passed"]
