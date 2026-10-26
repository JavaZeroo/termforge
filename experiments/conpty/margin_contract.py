"""Offline, independent native contract for the unchanged 175-byte margin probe.

This reads captured evidence; it never launches or changes a console. A passed
fixture is narrower than the full R2 margin, style, history and resize contract.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from experiments.conpty.margin_run import SOURCE, SOURCE_SHA256
from experiments.shell.workflow_checks import validate_console_window


STAGES = ("before", "alt", "restored", "final")


def expected_native(columns, rows):
    """Literal outcomes of the four fixed stages, independent of any parser."""
    if (columns, rows) not in ((80, 24), (12, 7)):
        raise ValueError("fixed native grid required")
    main = ["TOP_FIXED", "REGION_A", "REGION_B", "BOTTOM_FIXED"]
    alternate = ["ALT_OUTSIDE", "", "", "ALT_C", "ALT_D"]
    final = ["TOP_FIXED", "REGION_C", "REGION_D", "BOTTOM_FIXED", ""]
    final += (["TF_MARGIN_DONE"] if columns == 80 else ["TF_MARGIN_DO", "NE"])
    # DECSTBM homes the cursor. The final CR/LF outside the region clamps to
    # the physical bottom at height 7; it does not scroll the margin or screen.
    return {
        stage: {"lines": lines + [""] * (rows - len(lines)), "cursor": cursor}
        for stage, lines, cursor in (
            ("before", main, [0, 0]),
            ("alt", alternate, [4, 5]),
            ("restored", main, [0, 0]),
            ("final", final, [6, 0]),
        )
    }


def evaluate_native(stages, columns, rows):
    expected = expected_native(columns, rows)
    if set(stages) != set(STAGES):
        raise ValueError("four native stages required")
    for capture in stages.values():
        if (capture["columns"], capture["rows"]) != (columns, rows):
            raise ValueError("native grid changed")
    # The original source does not select SGR or a palette. Preserve the
    # initially observed attribute, including blank cells, rather than claim
    # that an unchosen color is a portable expected default.
    attribute = stages["before"]["cells"][0][0][1]
    results = {}
    for stage, capture in stages.items():
        target = expected[stage]
        wanted_cells = [[(ord(character), attribute)
                         for character in line.ljust(columns)]
                        for line in target["lines"]]
        fields = {
            "text": capture["lines"] == target["lines"],
            "cell_text": capture["cell_lines"] == target["lines"],
            "cells_and_attributes": capture["cells"] == wanted_cells,
            "cursor": [capture["cursor_row"] - capture["top"],
                       capture["cursor_column"]] == target["cursor"],
        }
        results[stage] = dict(fields, passed=all(fields.values()))
    return {
        "schema": "termforge-native-margin-contract-v1",
        "columns": columns, "rows": rows,
        "source_sha256": SOURCE_SHA256,
        "initial_native_attribute": attribute,
        "stages": results,
        "all_native_stages_passed": all(value["passed"] for value in results.values()),
        "unmeasured": ["styled_source", "history", "resize", "terminal_stage_cursors"],
        "full_r2_contract_verified": False,
    }


def inspect_native(directory, columns, rows):
    directory = Path(directory)
    source = (directory / "source.bin").read_bytes()
    if source != SOURCE or hashlib.sha256(source).hexdigest() != SOURCE_SHA256:
        raise ValueError("original source changed")
    stages = {stage: validate_console_window(directory, expected_role=stage,
                                             expected_generation=1, prefix=stage)
              for stage in STAGES}
    return evaluate_native(stages, columns, rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--columns", type=int, required=True)
    parser.add_argument("--rows", type=int, required=True)
    args = parser.parse_args()
    result = inspect_native(args.directory, args.columns, args.rows)
    print(json.dumps(result, indent=2))
    return int(not result["all_native_stages_passed"])


if __name__ == "__main__":
    raise SystemExit(main())
