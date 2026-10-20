"""Validate visual diagnostic records without accepting them as performance gates."""
import argparse
import json
from pathlib import Path
import re
import statistics


def require(condition, message):
    if not condition:
        raise ValueError(message)


def timestamp(value):
    require(type(value) is int and value >= 0, "invalid timestamp")
    return value


def validate(directory):
    directory = Path(directory)
    report = json.loads((directory / "summary.json").read_text(encoding="utf-8"))
    require(report["kind"] == "owned_window_visual_diagnostic", "wrong report kind")
    require(report["status"] in ("complete_diagnostic", "aborted"), "unknown completion status")
    for digest in report["source_sha256"].values():
        require(bool(re.fullmatch("[0-9a-f]{64}", digest)), "invalid source hash")
    frames = [json.loads(line) for line in (directory / "frames.jsonl").read_text(encoding="utf-8").splitlines()]
    previous = -1
    first_matches = {}
    for frame in frames:
        before, after = timestamp(frame["capture_before_ns"]), timestamp(frame["capture_after_ns"])
        require(previous <= before <= after, "nonmonotonic capture interval")
        previous = after
        require(type(frame["matched"]) is bool, "match must be boolean")
        if frame["matched"]:
            first_matches.setdefault(frame["event_id"], frame)
    for sample in report["samples"]:
        if sample.get("status") != "observed":
            require(report["status"] == "aborted", "incomplete sample in complete report")
            continue
        injection = sample["injection"]
        submit = timestamp(injection["submit_ns"])
        require(timestamp(injection["return_ns"]) >= submit, "injection time reversed")
        require(injection["accepted_events"] == 2, "partial injection counted as observed")
        match = first_matches[sample["event_id"]]
        require(sample["first_match"]["capture_after_ns"] == match["capture_after_ns"],
                "reported match differs from trace")
        latency = sample["capture_observed_latency_ns"]
        require(latency == match["capture_after_ns"] - submit and latency >= 0, "latency differs from events")
        lower, upper = sample["observation_bracket_ns"]
        require(0 <= lower <= upper == latency, "invalid observation bracket")
        fixture = sample["fixture"]
        require(timestamp(fixture["input_received_ns"]) <= timestamp(fixture["paint_submitted_ns"]),
                "fixture times reversed")
    if "clock_alignment" in report:
        clock = report["clock_alignment"]
        for row in clock["exchanges"]:
            require(timestamp(row["before_ns"]) <= timestamp(row["after_ns"]), "clock RTT reversed")
            timestamp(row["remote_ns"])
        best = min(clock["exchanges"], key=lambda row: row["after_ns"] - row["before_ns"])
        require(clock["lower_ns"] == best["remote_ns"] - best["after_ns"], "clock lower bound mismatch")
        require(clock["upper_ns"] == best["remote_ns"] - best["before_ns"], "clock upper bound mismatch")
    for delay in report["configured_delays_ms"]:
        values = [s["capture_observed_latency_ns"] for s in report["samples"]
                  if s.get("status") == "observed" and s["delay_ms"] == delay]
        summary = report["summary"][str(delay)]
        require(summary["observed"] == len(values), "count mismatch")
        require(summary["median_ns"] == (statistics.median(values) if values else None), "median mismatch")
        if len(values) < 200:
            require(summary["p95_ns"] is None, "P95 below sample threshold")
        if len(values) < 1000:
            require(summary["p99_ns"] is None, "P99 below sample threshold")
        if report["status"] == "complete_diagnostic":
            require(len(values) == report["events_per_delay"], "missing events")
    return {"record_consistency": "passed", "run_status": report["status"],
            "samples": len(report["samples"]), "performance_gate": "not_evaluated"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory")
    print(json.dumps(validate(parser.parse_args().directory)))
