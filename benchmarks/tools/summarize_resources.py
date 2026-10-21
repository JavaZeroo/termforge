"""Validate sample.py v0.1 JSONL and summarize observed resources, streaming."""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path

GROUPS = ("root", "descendant", "sampler")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def number(row, key, integer=False, positive=False):
    value = row[key]
    require(type(value) in ((int,) if integer else (int, float)) and
            math.isfinite(value) and (value > 0 if positive else value >= 0),
            f"invalid {key}")
    return value


def summarize(path):
    digest = hashlib.sha256()
    metadata = end = previous = None
    counts = Counter()
    statuses = Counter()
    identities = {}
    groups = {g: {"observed_processes": 0, "observed_samples": 0,
                  "unavailable_rows": 0, "observed_cpu_seconds": None,
                  "sampled_peak_observed_private_bytes": None,
                  "sampled_peak_observed_working_set_bytes": None,
                  "observed_cpu_core_equivalents": None} for g in GROUPS}
    quality = {"collection_ns_max": None, "actual_interval_ns_max": None,
               "schedule_lateness_ns_max": None, "skipped_ticks": 0,
               "discovery_errors": 0, "samples_with_unavailable_processes": 0}
    first_elapsed = last_elapsed = None
    with Path(path).open("rb") as stream:
        for line_number, raw in enumerate(stream, 1):
            digest.update(raw)
            try:
                row = json.loads(raw)
                require(isinstance(row, dict), "record must be an object")
                kind = row["type"]
                require(end is None, "record after end")
                if metadata is None:
                    require(kind == "metadata", "first record must be metadata")
                    require(row["schema_version"] == "0.1", "unsupported schema")
                    require(row["clock"] == "perf_counter_ns", "unsupported clock")
                    number(row, "root_pid", integer=True, positive=True)
                    number(row, "root_create_time", positive=True)
                    number(row, "start_counter_ns", integer=True)
                    for key in ("requested_interval_ns", "requested_discovery_interval_ns"):
                        number(row, key, integer=True, positive=True)
                    metadata = row
                elif kind == "sample":
                    elapsed = number(row, "elapsed_ns", integer=True)
                    require(previous is None or elapsed > previous, "nonmonotonic sample")
                    expected_interval = None if previous is None else elapsed - previous
                    require(row["actual_interval_ns"] == expected_interval,
                            "actual interval disagrees with timestamps")
                    for key in ("collection_ns", "schedule_lateness_ns"):
                        value = number(row, key, integer=True)
                        quality[key + "_max"] = max(quality[key + "_max"] or 0, value)
                    if expected_interval is not None:
                        quality["actual_interval_ns_max"] = max(
                            quality["actual_interval_ns_max"] or 0, expected_interval)
                    require(isinstance(row["errors"], list), "errors must be a list")
                    quality["discovery_errors"] += len(row["errors"])
                    require(isinstance(row["processes"], list), "processes must be a list")
                    seen = set()
                    totals = {g: [] for g in GROUPS}
                    unavailable = False
                    for process in row["processes"]:
                        group = process["group"]
                        require(group in GROUPS, "unknown process group")
                        pid = number(process, "pid", integer=True, positive=True)
                        created = number(process, "create_time", positive=True)
                        key = (pid, created, group)
                        require(key not in seen, "duplicate process in sample")
                        seen.add(key)
                        is_root = (pid, created) == (metadata["root_pid"], metadata["root_create_time"])
                        require(group == "sampler" or is_root == (group == "root"),
                                "root identity/group mismatch")
                        status = process["status"]
                        require(status in ("observed", "NoSuchProcess", "AccessDenied"),
                                "unknown process status")
                        statuses[status] += 1
                        if status != "observed":
                            groups[group]["unavailable_rows"] += 1
                            unavailable = True
                            continue
                        cpu = number(process, "cpu_seconds_lifetime")
                        delta = number(process, "cpu_seconds_since_first_observation")
                        for metric in ("working_set_bytes", "private_bytes", "handles"):
                            number(process, metric, integer=True)
                        if key not in identities:
                            require(delta == 0, "first observation CPU delta must be zero")
                            identities[key] = [cpu, cpu, 0]
                            groups[group]["observed_processes"] += 1
                        base, last_cpu, _ = identities[key]
                        require(cpu >= last_cpu, "CPU counter decreased for process identity")
                        require(math.isclose(cpu - base, delta, rel_tol=1e-9, abs_tol=1e-9),
                                "CPU delta disagrees with lifetime counter")
                        identities[key] = [base, cpu, delta]
                        totals[group].append(process)
                    require(any(k[2] == "sampler" for k in seen), "missing sampler row")
                    if unavailable:
                        quality["samples_with_unavailable_processes"] += 1
                    for group, processes in totals.items():
                        if processes:
                            result = groups[group]
                            result["observed_samples"] += 1
                            for metric in ("private_bytes", "working_set_bytes"):
                                name = "sampled_peak_observed_" + metric
                                result[name] = max(result[name] or 0,
                                                   sum(p[metric] for p in processes))
                    first_elapsed = elapsed if first_elapsed is None else first_elapsed
                    last_elapsed = previous = elapsed
                elif kind == "schedule_gap":
                    require(previous is not None, "gap before first sample")
                    quality["skipped_ticks"] += number(row, "skipped_ticks", integer=True, positive=True)
                elif kind == "end":
                    require(row["status"] == "complete", "incomplete end status")
                    require(previous is not None, "no samples")
                    require(number(row, "elapsed_ns", integer=True) >= previous, "end before sample")
                    end = row
                else:
                    raise ValueError("unexpected record type")
                counts[kind] += 1
            except (ValueError, KeyError, TypeError, OverflowError) as exc:
                raise ValueError(f"line {line_number}: {exc}") from exc
    require(end is not None, "missing end record (truncated trace)")
    span = last_elapsed - first_elapsed
    for group, result in groups.items():
        deltas = [v[2] for k, v in identities.items() if k[2] == group]
        if deltas:
            result["observed_cpu_seconds"] = sum(deltas)
            if span > 0:
                result["observed_cpu_core_equivalents"] = sum(deltas) / (span / 1e9)
    return {"schema_version": "0.1", "kind": "resource_trace_summary",
            "validation_status": "valid", "performance_gate": "not_evaluated",
            "source_sha256": digest.hexdigest(), "sample_count": counts["sample"],
            "sampling_span_ns": span, "trace_elapsed_ns": end["elapsed_ns"],
            "requested_interval_ns": metadata["requested_interval_ns"],
            "requested_discovery_interval_ns": metadata["requested_discovery_interval_ns"],
            "groups": groups, "quality": quality, "process_status_counts": dict(statuses),
            "limitations": [
                "Observed CPU retains exited processes but misses CPU before first/after last observation.",
                "Core equivalents divide observed CPU by first-to-last sample span, not workload duration.",
                "Memory peaks sum only observed processes within each sample; unavailable processes are unknown.",
                "No observations produce null, not zero; working sets can double-count shared pages.",
                "Discovery can miss short-lived children; descendants include producers and helpers.",
                "Valid trace does not imply low overhead, complete coverage, or terminal correctness."
            ]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    report = summarize(args.trace)
    with Path(args.out).open("x", encoding="utf-8") as output:
        json.dump(report, output, indent=2, allow_nan=False)
        output.write("\n")
