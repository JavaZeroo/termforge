"""Run one separately authorized fixed static-Search batch; never build it."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import subprocess
import sys
import time
from prepare_search_cost import (BASE, ROOT, STAGES, bounds, cases, generated_sources,
                                 git, identity, recipe, require, same_types, source_files)


def check_file(value):
    require(isinstance(value, dict) and set(value) == {"path", "bytes", "sha256"}, "Frozen file identity required")
    require(type(value["bytes"]) is int and value["bytes"] >= 0 and isinstance(value["sha256"], str), "Frozen file types differ")
    path = value["path"]
    require(isinstance(path, str) and Path(path).is_absolute(), "Actual absolute file path required")
    path = Path(path).resolve(strict=True)
    require(path.is_file() and identity(path) == {k: value[k] for k in ("bytes", "sha256")}, "Frozen file bytes differ")
    return path


def verify_inputs(plan, path, original):
    require(path.read_bytes() == original, "Plan bytes changed")
    require(git("rev-parse", "HEAD").decode().strip() == plan["source_commit"], "Source HEAD changed")
    require(set(plan["source_identity"]) == set(source_files()), "Source closure differs")
    for name, value in plan["source_identity"].items():
        require(same_types(value, identity(ROOT / name)), "Source identity/type differs: " + name)
    generated = generated_sources()
    require(isinstance(plan["generated_directory"], str) and Path(plan["generated_directory"]).is_absolute(), "Generated directory must be absolute")
    directory = Path(plan["generated_directory"]).resolve(strict=True)
    require(directory.is_relative_to(ROOT / "local") and directory.is_dir(), "Generated directory outside local")
    require(set(plan["generated_headers"]) == set(generated), "Both exact generated callers are required")
    for name, expected in plan["generated_headers"].items():
        require(check_file(expected) == directory / name, "Generated header absolute path differs")
        require(identity(directory / name) == {"bytes": len(generated[name].encode()),
                "sha256": hashlib.sha256(generated[name].encode()).hexdigest()}, "Published generated Search seam differs")
    require(set(plan["tools"]) == {"python", "cmake", "compiler"}, "Tool closure differs")
    for tool in plan["tools"].values():
        check_file(tool)
    require(Path(sys.executable).resolve() == Path(plan["tools"]["python"]["path"]).resolve(), "Python selection differs")
    binary = plan["actual_executable"]
    require(isinstance(binary, dict) and set(binary) == {"path", "bytes", "sha256", "observed"} and binary["observed"] is True,
            "Actual compiled executable is unobserved")
    executable = check_file({k: binary[k] for k in ("path", "bytes", "sha256")})
    check_file(plan["build_evidence"])
    return executable


def load_plan(path):
    original = path.read_bytes()
    plan = json.loads(original)
    require(plan.get("execution_authorized") is True, "False plan rejected before any attempt/output")
    require(plan.get("schema") == "termforge-r164-static-search-cost-plan-v1" and plan.get("base_commit") == BASE, "Wrong static Search plan")
    require(type(plan.get("actual_runs")) is int and plan["actual_runs"] == 0, "Plan already executed or invalid count")
    require(same_types(plan.get("recipe"), recipe()) and same_types(plan.get("cases"), cases()), "Recipe/types/case order differ")
    require(plan.get("authorized_cases") == [case["id"] for case in cases()], "Exactly the fixed four cases in order are required")
    for key, expected in (("maximum_attempts_per_case", 1), ("maximum_batch_seconds", 50), ("whole_batch_bound_seconds", 50)):
        require(type(plan.get(key)) is int and plan[key] == expected, "Fixed attempt/deadline contract differs")
    require(plan.get("whole_lifecycle_hard_bound_seconds") is None and plan.get("product_acceptance") is None and
            plan.get("search_cost_result") is None, "Preparation cannot contain acceptance/results or a whole-lifecycle claim")
    executable = verify_inputs(plan, path, original)
    locations = []
    for key in ("output_directory", "attempt_marker"):
        value = plan.get(key)
        require(isinstance(value, str) and Path(value).is_absolute(), "New absolute output/marker required")
        value = Path(value).resolve()
        require(value.is_relative_to(ROOT / "local") and not value.exists(), "Output/attempt must be new under local")
        require(not value.is_relative_to(Path(plan["generated_directory"]).resolve()) and not value.is_relative_to(executable.parent),
                "Output/attempt cannot overlap generated headers or the runtime")
        locations.append(value)
    output, marker = locations
    require(output != marker and not output.is_relative_to(marker) and not marker.is_relative_to(output), "Attempt must be separate from output")
    return plan, original, executable, output, marker


def validate_output(stdout, stderr, case):
    require(stderr == b"" and len(stdout) <= 1024*1024, "Unexpected stderr or oversized protocol")
    rows = [json.loads(line) for line in stdout.decode("ascii").splitlines()]
    require(len(rows) == 25 and all(isinstance(row, dict) for row in rows), "One fixture and 24 stage rows are required")
    fixture = rows[0]
    h, adapter = case["history"], case["adapter"]
    b = bounds(h); n = b["retained_rows"]
    require(set(fixture) == {"schema", "adapter", "history", "retained_rows", "epoch", "order_revision", "sorted_proven", "rows", "hit_ranks", "oracle_ranges", "rss_bytes"}, "Fixture fields differ")
    require(fixture["schema"] == "termforge-r164-static-search-fixture-v1" and fixture["adapter"] == adapter and
            type(fixture["history"]) is int and fixture["history"] == h and type(fixture["retained_rows"]) is int and
            fixture["retained_rows"] == n and type(fixture["epoch"]) is int and fixture["epoch"] > 0 and
            type(fixture["order_revision"]) is int and fixture["order_revision"] == 1 and fixture["sorted_proven"] is False and
            fixture["rss_bytes"] is None, "Fixture identity/frame contract differs")
    identities = fixture["rows"]
    require(isinstance(identities, list) and len(identities) == n and all(isinstance(row, list) and len(row) == 2 and
            all(type(value) is int and value > 0 for value in row) for row in identities), "Real row identity snapshot differs")
    ids = [row[0] for row in identities]
    require(len(set(ids)) == n and ids != sorted(ids), "Fixture identities are not unique/unordered")
    ordered = ids.copy(); ordered[h+2], ordered[h+3] = ordered[h+3], ordered[h+2]
    require(ordered == sorted(ordered), "Fixture contains an unplanned identity permutation")
    ranks = [j*(n-1)//15 for j in range(16)]
    require(same_types(fixture["hit_ranks"], ranks), "Fixed hit layout differs")
    expected = [[*identities[rank], 0, fixture["epoch"], *identities[rank], 6, fixture["epoch"]] for rank in ranks]
    require(same_types(fixture["oracle_ranges"], expected), "Independent physical-rank result oracle differs")
    numeric = {"history", "sample", "elapsed_ns", "step_calls", "work_used", "bytes_scanned", "rows_loaded", "lookups", "probes", "rebuilds", "table_payload_bytes", "search_status"}
    required = numeric | {"schema", "adapter", "stage", "max_step_ns", "result_ranges", "oracle_passed", "rss_bytes"}
    for sample_index, sample in enumerate((-1, 0, 1, 2)):
        sample_rows = rows[1+sample_index*6:1+(sample_index+1)*6]
        for stage_index, (row, stage) in enumerate(zip(sample_rows, STAGES)):
            require(set(row) == required and all(type(row[name]) is int for name in numeric), "Stage fields/numeric types differ")
            require(row["schema"] == "termforge-r164-static-search-stage-v1" and row["adapter"] == adapter and
                    row["history"] == h and row["sample"] == sample and row["stage"] == stage and
                    row["oracle_passed"] is True and row["rss_bytes"] is None, "Stage identity/order/oracle failed")
            require(all(row[name] >= 0 for name in numeric - {"sample"}), "Negative timing/counter")
            is_scan = stage.endswith("_step")
            require(row["search_status"] == (1 if stage.endswith("_start") else 2), "Search did not preserve running/complete status")
            require((type(row["max_step_ns"]) is int and 0 <= row["max_step_ns"] <= row["elapsed_ns"]) if is_scan else row["max_step_ns"] is None,
                    "Maximum-step endpoint differs")
            require([row[k] for k in ("step_calls", "work_used", "bytes_scanned", "rows_loaded")] ==
                    ([b["step_calls_per_scan"], b["work_per_scan"], b["bytes_per_scan"], n] if is_scan else [0, 0, 0, 0]), "Step/work/byte/row oracle differs")
            results = expected if stage == "warm_needle_step" else [expected[-1]] if stage == "last_match" else []
            require(same_types(row["result_ranges"], results), "Actual result ranges/navigation differ from literal oracle")
            require(row["rebuilds"] == (1 if adapter == "lazy_table" and stage_index == 1 else 0), "Cold/warm table state differs")
            require(row["table_payload_bytes"] == 0 if adapter == "scan" or stage_index == 0 else row["table_payload_bytes"] >= 16*n,
                    "Position-table payload does not match cold/warm state")
            if stage.endswith("_start"):
                require(row["lookups"] == 0 and row["probes"] == 0, "Start unexpectedly located rows")
            if is_scan:
                require(row["lookups"] == b["locate_calls_per_scan"], "Full-scan locate-call oracle differs")
                if adapter == "scan":
                    require(row["probes"] == b["scan_probes_per_scan"], "Full-scan Scan probe oracle differs")
            if stage in ("last_match", "navigate_no_wrap"):
                key = "last_match" if stage == "last_match" else "navigate"
                require(row["lookups"] == b[key+"_lookups"], "Validation/navigation locate-call oracle differs")
                if adapter == "scan":
                    require(row["probes"] == b["scan_"+key+"_probes"], "Quadratic Scan probe oracle differs")
        require(sample_rows[1]["lookups"] == sample_rows[3]["lookups"] and sample_rows[1]["probes"] == sample_rows[3]["probes"],
                "Cold miss and warm hit scanned different row-order work")
        require(len({row["table_payload_bytes"] for row in sample_rows[1:]}) == 1, "Immutable snapshot changed table payload")
        lookups = sum(row["lookups"] for row in sample_rows)
        probes = sum(row["probes"] for row in sample_rows)
        require(lookups <= b["lookups_per_sample_upper_bound"], "Static locate-call bound exceeded")
        per_lookup = n if adapter == "scan" else (n-1).bit_length()+1
        require(probes <= lookups*per_lookup, "Static lookup-probe bound exceeded")


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--plan", required=True, type=Path)
    args = cli.parse_args()
    path = args.plan.resolve(strict=True)
    plan, original, executable, output, marker = load_plan(path)
    marker.parent.mkdir(parents=True, exist_ok=True)
    with marker.open("x", encoding="ascii") as handle:
        handle.write(hashlib.sha256(original).hexdigest() + "\n")
    output.mkdir(parents=True)
    (output / "authorized-plan.json").write_bytes(original)
    began = time.monotonic()
    batch = {"schema": "termforge-r164-static-search-batch-v1", "source_commit": plan["source_commit"],
             "plan_sha256": hashlib.sha256(original).hexdigest(), "cases": [], "diagnostic_complete": False,
             "product_acceptance": None, "whole_lifecycle_hard_bound_seconds": None}
    permit = secrets.token_hex(16)
    environment = dict(os.environ, TERMFORGE_R164_STATIC_SEARCH_COST_AUTHORIZED=permit)
    try:
        for case in cases():
            verify_inputs(plan, path, original)
            remaining = 50 - (time.monotonic() - began)
            require(remaining > 0, "Fixed batch deadline elapsed")
            command = [str(executable), *[part.replace("{permit}", permit) for part in case["arguments"]]]
            stem = output / case["id"]
            stem.with_suffix(".command.json").write_text(json.dumps({"command": command, "cwd": str(ROOT)}) + "\n", encoding="utf-8")
            started = time.monotonic()
            try:
                result = subprocess.run(command, cwd=ROOT, env=environment, capture_output=True, timeout=min(10, remaining))
                stdout, stderr, code, timeout = result.stdout, result.stderr, result.returncode, False
            except subprocess.TimeoutExpired as error:
                stdout, stderr, code, timeout = error.stdout or b"", error.stderr or b"", None, True
            stem.with_suffix(".stdout.log").write_bytes(stdout)
            stem.with_suffix(".stderr.log").write_bytes(stderr)
            batch["cases"].append({"case": case["id"], "exit_code": code, "timed_out": timeout,
                "elapsed_seconds": time.monotonic()-started, "stdout": identity(stem.with_suffix(".stdout.log")),
                "stderr": identity(stem.with_suffix(".stderr.log"))})
            verify_inputs(plan, path, original)
            require(not timeout and code == 0, "Target timeout/failure; stop without retry")
            validate_output(stdout, stderr, case)
        verify_inputs(plan, path, original)
        require(time.monotonic()-began <= 50, "Fixed batch deadline exceeded")
        batch["diagnostic_complete"] = True
    except Exception as error:
        batch["error"] = str(error)
    finally:
        try:
            verify_inputs(plan, path, original)
            batch["post_inputs_unchanged"] = True
        except Exception as error:
            batch["post_inputs_unchanged"] = False
            batch["post_input_error"] = str(error)
            batch["diagnostic_complete"] = False
        batch["elapsed_seconds"] = time.monotonic()-began
        (output / "batch.json").write_text(json.dumps(batch, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(batch))
    return 0 if batch["diagnostic_complete"] else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(json.dumps({"status": "rejected", "error": str(error)}), file=sys.stderr)
        raise SystemExit(1)
