"""Reject missing, skipped, loading-error or unexpected TAEF results."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re

SUMMARY = re.compile(
    r"Summary:\s*Total=(\d+),\s*Passed=(\d+),\s*Failed=(\d+),\s*"
    r"Blocked=(\d+),\s*Not Run=(\d+),\s*Skipped=(\d+)")
METHOD = "AlternateScreenMarginsTest"


def validate(text: str, exit_code: int, expected: str) -> dict:
    rows = SUMMARY.findall(text)
    if len(rows) != 1:
        raise ValueError("Expected exactly one completed TAEF summary")
    counts = dict(zip(("total", "passed", "failed", "blocked", "not_run", "skipped"),
                      map(int, rows[0])))
    if counts["total"] != sum(counts[key] for key in counts if key != "total"):
        raise ValueError("Inconsistent test counts")
    if any(counts[key] for key in ("blocked", "not_run", "skipped")):
        raise ValueError("Blocked, unexecuted or skipped tests are not acceptance")
    if METHOD not in text:
        raise ValueError("The margin regression was not observed")
    if expected == "red":
        # TAEF's high bits report harness/timeout faults; the low bits count failures.
        if exit_code != 1 or counts != dict(total=1, passed=0, failed=1, blocked=0, not_run=0, skipped=0):
            raise ValueError("Expected one failed regression")
        if not re.search(r"(?:Error|Failed)[^\n]*Verify[^\n]*_scrollMargins", text, re.IGNORECASE):
            raise ValueError("Failure did not identify the scrolling-margin assertion")
    elif expected in ("focused-green", "full-green"):
        if exit_code != 0 or counts["failed"] or counts["passed"] != counts["total"]:
            raise ValueError("Expected a successful TAEF execution")
        if expected == "focused-green" and counts["total"] != 1:
            raise ValueError("Expected exactly one focused regression")
        if expected == "full-green" and counts["total"] <= 1:
            raise ValueError("A focused test cannot stand in for the full adapter suite")
    else:
        raise ValueError("Unknown result contract")
    return {"expected": expected, "exit_code": exit_code, "counts": counts, "accepted": True}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=Path)
    parser.add_argument("--exit-code", type=int, required=True)
    parser.add_argument("--expected", choices=("red", "focused-green", "full-green"), required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    result = validate(args.log.read_text(encoding="utf-8-sig"), args.exit_code, args.expected)
    args.out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
