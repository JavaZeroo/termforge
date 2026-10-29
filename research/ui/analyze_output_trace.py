"""Correlate a scenario's output trace with its phase records.

Reads the opt-in artifacts written by the shell diagnostics:
  * output-trace.jsonl   (termforge-output-trace-v1 / termforge-input-trace-v1)
  * actions-progress.jsonl or *-progress.jsonl (phase records)
  * selection-discard.jsonl (optional)

Prints, for each selection discard, the surrounding phases and the repaint
batches consumed close to it, including the grid each batch was consumed at.
This is analysis only: it never runs the shell and changes no threshold.
"""
from __future__ import annotations

import json
from pathlib import Path

WINDOW_MS = 2000


def read_jsonl(path: Path):
    if not path.exists():
        return []
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.strip():
            continue
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError:
            # Two shells can append to the same trace; a torn line is skipped
            # rather than aborting the whole analysis.
            continue
    return rows


def load_stage(directory: Path):
    progress = None
    for name in ("actions-progress.jsonl",):
        candidate = directory / name
        if candidate.exists():
            progress = candidate
            break
    if progress is None:
        matches = sorted(directory.glob("*-progress.jsonl"))
        progress = matches[0] if matches else None
    phases = [(row.get("t_ms"), row.get("phase")) for row in read_jsonl(progress) if row.get("t_ms")] if progress else []
    trace = read_jsonl(directory / "output-trace.jsonl")
    discards = read_jsonl(directory / "selection-discard.jsonl")
    sample = None
    sample_path = directory / "sample.json"
    if sample_path.exists():
        try:
            sample = json.loads(sample_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            sample = None
    return phases, trace, discards, sample


def describe(directory: Path) -> list[str]:
    phases, trace, discards, sample = load_stage(directory)
    lines = [f"== {directory.parent.name}/{directory.name}"]
    if isinstance(sample, dict) and sample.get("desktop_close_settle"):
        lines.append("   desktop close settle: %s" % json.dumps(sample["desktop_close_settle"]))
    if not phases and not trace:
        lines.append("   no diagnostic artifacts")
        return lines
    for discard in discards:
        at = discard.get("t_ms")
        before = [p for p in phases if p[0] <= at]
        after = [p for p in phases if p[0] > at]
        lines.append(
            "   discard %s row=%s between %s and %s saved_bytes=%s current_bytes=%s saved_columns=%s current_columns=%s"
            % (discard.get("reason"), discard.get("row"),
               before[-1][1] if before else None, after[0][1] if after else None,
               discard.get("saved_bytes"), discard.get("current_bytes"),
               discard.get("saved_columns"), discard.get("current_columns")))
        for row in trace:
            if not row.get("repaint"):
                continue
            if abs(row.get("t_ms", 0) - (at or 0)) <= WINDOW_MS:
                lines.append("      repaint t=%s terminal=%s columns=%s rows=%s bytes=%s"
                             % (row.get("t_ms"), row.get("terminal"), row.get("columns"),
                                row.get("rows"), row.get("bytes")))
    return lines


def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="run-* directory holding the scenario subdirectories")
    args = parser.parse_args()
    stages = [p for p in args.directory.iterdir() if p.is_dir() and not p.name.startswith("run-")]
    if not stages:
        for run in sorted(p for p in args.directory.iterdir() if p.is_dir() and p.name.startswith("run-")):
            stages.extend(sorted(p for p in run.iterdir() if p.is_dir()))
    for stage in stages:
        for line in describe(stage):
            print(line)


if __name__ == "__main__":
    main()
