"""Offline whitelist export of the fixed #144 three-case diagnostic batch."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
EXECUTED_SOURCE = "02aac45ca6e8eaffcbca1d3ef1a80d243159ec46"
PLAN_SHA256 = "87a0518c23b65f050d3991557b6b58aca334c8d80dc939fab601d3fae01f0fc8"
# These are preserved proposals from the fixed batch, not mutable input labels.
FALSE_PLANS = (
    ("", "initial", "4e2a7280c1a64d6203f2b7c9f8c00bfbb4d1aa4f691f154e75bb249e074631d6",
     "5ddecd4e0bd978d38a38974bb4d27b193b605fa7"),
    ("-p2", "protocol-fix", "77429f072e16ebaf3d1b7c729aae65dd67f1ae36d682b1b3c2812eb22703ef5d",
     "0949ac6fc371e24ee20c25c36b6e90f372c99fe8"),
    ("-p3", "version-fix", "115c972ce327e9456edf9e96de4f885ec2fdeb16fd819dd34dc24bac871072b4",
     EXECUTED_SOURCE),
)
CASES = ("normal", "hang-vm-helpers", "throw-before-grab")
SOURCE_FILES = ("research/ui/node_contract_deadline.py", "research/ui/node_contract_bootstrap.js",
                "research/ui/test_raster_native_contract.js", "research/ui/raster_style_reference.js",
                "experiments/conpty/run.py")
STAGES = ["node-first-checkpoint", "go-received", "fixture-before-load", "before-file-read",
          "file-read", "vm-helpers-start", "vm-helpers-ready", "pure-checks-done"]
for _case in ("normal", "grab", "toImage", "empty", "dpr", "pixel", "destroy"):
    STAGES.extend(("case-start:" + _case, "case-end:" + _case))
STAGES.append("final")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def hex_identity(value, length):
    require(type(value) is str and re.fullmatch(r"[0-9a-f]{" + str(length) + "}", value) is not None,
            "Invalid hexadecimal identity")
    return value


def load(path):
    return json.loads(path.read_bytes().decode("utf-8-sig"))


def file_identity(label, path):
    data = path.read_bytes()
    return {"id": label, "bytes": len(data), "sha256": digest(data)}


def account(value):
    require(type(value) is dict and set(value) == {"active", "total", "terminated"}, "Job fields differ")
    require(all(type(number) is int and number >= 0 for number in value.values()), "Invalid Job count")
    return {name: value[name] for name in ("active", "total", "terminated")}


def export(plan_path, log_path, exit_path, case_directory):
    require(digest(plan_path.read_bytes()) == PLAN_SHA256, "Authorized plan identity differs")
    plan, batch, exit_record = load(plan_path), load(log_path), load(exit_path)
    require(plan["source_base_commit"] == EXECUTED_SOURCE and plan["execution_authorized"] is True and
            plan["maximum_attempts"] == 1 and plan["cases"] == list(CASES), "Plan contract differs")
    require(plan["execution_budget_seconds"] == 10 and plan["cleanup_budget_seconds"] == 5 and
            plan["whole_lifecycle_bound_seconds"] is None, "Plan budget differs")
    require(type(plan["source_files_sha256"]) is dict and set(plan["source_files_sha256"]) == set(SOURCE_FILES),
            "Source set differs")
    source_hashes = {name: hex_identity(plan["source_files_sha256"][name], 64) for name in SOURCE_FILES}
    python_sha, node_sha = (hex_identity(plan[name], 64) for name in ("python_sha256", "node_sha256"))
    require(all(digest((ROOT / name).read_bytes()) == plan["source_files_sha256"][name] for name in SOURCE_FILES),
            "Current source bytes differ from executed source")
    require(digest(Path(plan["node"]).read_bytes()) == plan["node_sha256"] and
            digest(Path(plan["python"]).read_bytes()) == plan["python_sha256"], "Runtime EXE identity differs")
    require(plan["out"] == str(case_directory.resolve()), "Case directory differs")
    require(batch["schema"] == "termforge-node-contract-batch-v1" and
            batch["status"] == "expected_diagnostic_observations" and exit_record["exit_code"] == 0,
            "CLI/batch verdict differs")
    require(batch["original_timeout_root_cause"] is None and batch["whole_lifecycle_bound_seconds"] is None,
            "Unknown endpoint was filled")
    require(type(plan["windows_version"]) is str and re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", plan["windows_version"]),
            "Windows version is not a safe scalar")
    require(type(plan["python_version"]) is str, "Python version is not a safe scalar")
    python_version = re.match(r"\A([0-9]+\.[0-9]+\.[0-9]+)", plan["python_version"])
    require(python_version is not None, "Python version is not a safe scalar")
    require(all(batch[name] == plan[name] for name in ("windows_version", "python_version")), "Batch host differs")
    require(len(batch["observations"]) == 3, "Observation count differs")
    marker = plan_path.with_suffix(".attempted")
    require(marker.read_bytes() == b"One diagnostic attempt; retain after success or failure.\r\n", "Attempt marker differs")
    raw_files = [file_identity("authorized-plan", plan_path), file_identity("cli-log", log_path),
                 file_identity("cli-exit", exit_path), file_identity("attempt-marker", marker)]
    public_cases = []
    for index, (case, row) in enumerate(zip(CASES, batch["observations"])):
        case_path = case_directory / f"{index}-{case}.json"
        case_bytes = case_path.read_bytes()
        require(case_bytes == (json.dumps(row, indent=2) + "\n").encode(), "Case bytes differ from CLI row")
        raw_files.append(file_identity("case-" + case, case_path))
        require(row["schema"] == "termforge-node-contract-observation-v1" and row["case"] == case,
                "Case identity differs")
        require(row["source_files_sha256"] == plan["source_files_sha256"] and row["node_sha256"] == plan["node_sha256"] and
                all(row[name] == plan[name] for name in ("windows_version", "python_version")), "Case input/host differs")
        require(row["execution_budget_seconds"] == 10 and row["cleanup_budget_seconds"] == 5, "Case budget differs")
        require(all(row[name] is True for name in ("process_created", "root_reaped", "root_in_owned_job", "cleanup_verified")),
                "Owned root/cleanup evidence differs")
        require("cleanup_errors" not in row and "protocol_error" not in row["trace"], "Case contains an observation error")
        observations = [json.loads(line.split(":", 1)[1]) for line in row["stderr"].splitlines()
                        if line.startswith("TF_NODE_CONTRACT_V1:")]
        require(observations == row["trace"]["observations"], "Captured checkpoints differ from decoded trace")
        count = (23, 6, 11)[index]
        require(len(observations) == count and [event["stage"] for event in observations] == STAGES[:count],
                "Checkpoint schedule differs")
        require([event["sequence"] for event in observations] == list(range(count)) and
                all(type(event["pid"]) is int and event["pid"] == row["root_pid"] for event in observations),
                "Checkpoint sequence/root identity differs")
        clocks = [int(event["monotonic_ns"]) for event in observations]
        require(clocks == sorted(clocks) and clocks[0] >= 0, "Checkpoint clocks differ")
        require(row["trace"]["last_observed_stage"] == STAGES[count - 1] and row["trace"]["complete"] is (index == 0),
                "Trace terminal state differs")
        first = observations[0]
        require(type(first["node_version"]) is str and re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+", first["node_version"]) and first["arch"] == "x64" and
                first["platform"] == "win32", "Node runtime differs")
        expected_status, expected_exit = (("passed", 0), ("timed_out", 99), ("failed", 1))[index]
        require(row["status"] == expected_status and row["exit_code"] == expected_exit and
                row["forced_job_termination"] is (index == 1), "Child verdict differs")
        require(account(row["job_before_go"]) == {"active": 1, "total": 1, "terminated": 0} and
                account(row["job_after"]) == {"active": 0, "total": 2, "terminated": 0}, "Job accounting differs")
        exception = None
        timeout_job = None
        if index == 1:
            timeout_job = account(row["job_at_timeout"])
            require(row["primary_exception"]["type"] == "TimeoutExpired" and row["primary_exception"]["timeout_seconds"] == 10 and
                    timeout_job == {"active": 2, "total": 2, "terminated": 0}, "Timeout evidence differs")
            exception = {"type": "TimeoutExpired", "timeout_seconds": 10}
        else:
            require(row["primary_exception"] is None, "Unexpected host exception")
            require(row.get("job_at_timeout") is None, "Unexpected timeout Job")
        expected_stdout = "module paths, QRgb, ROI/ink gates,32B sret and normal-return destructor order passed\n" if index == 0 else ""
        require(row["stdout"] == expected_stdout and
                (index != 2 or "TF_NODE_CONTROLLED_EXCEPTION" in row["stderr"]), "Captured output differs")
        times = {name: row[name] for name in ("process_creation_elapsed_seconds", "communicate_elapsed_seconds")}
        require(all(type(value) in (float, int) and math.isfinite(value) and value >= 0 for value in times.values()),
                "Invalid diagnostic timing")
        public_cases.append({"case": case, "status": expected_status, "exit_code": expected_exit,
                             "execution_budget_seconds": 10, "cleanup_budget_seconds": 5, **times,
                             "primary_host_exception": exception, "forced_job_termination": index == 1,
                             "root_reaped": True, "root_in_owned_job": True, "cleanup_verified": True,
                             "job_before_go": account(row["job_before_go"]), "job_at_timeout": timeout_job,
                             "job_after": account(row["job_after"]), "checkpoint_count": count,
                             "stages": list(STAGES[:count]), "complete": index == 0,
                             "node_version": first["node_version"], "node_arch": "x64", "node_platform": "win32"})
    prior = load(ROOT / "local/node-deadline-p3/prior-artifacts-before.json")
    require(len(prior) == 24, "Prior artifact set differs")
    preserved = []
    for index, (name, expected) in enumerate(sorted(prior.items())):
        path = ROOT / name
        require(digest(path.read_bytes()) == hex_identity(expected, 64), "Prior artifact changed")
        preserved.append(file_identity(f"prior-{index:02d}", path))
    false_plans = []
    for suffix, label, expected_sha, expected_source in FALSE_PLANS:
        path = ROOT / f"local/node-144-offline-proposal{suffix}.json"
        require(digest(path.read_bytes()) == expected_sha, "False proposal identity differs")
        value = load(path)
        require(hex_identity(value["source_base_commit"], 40) == expected_source, "False proposal identity differs")
        require(value["execution_authorized"] is False and not path.with_suffix(".attempted").exists() and
                not Path(value["out"]).exists(), "False proposal execution state differs")
        false_plans.append({**file_identity(label, path), "source_base_commit": expected_source,
                            "execution_authorized": False, "attempt_marker_present": False, "output_present": False})
    reference_commit = "43883022883d382e3dd57bd627e9c735bafe58ca"
    references = ("research/ui/raster_style_reference.js", "research/ui/test_raster_style_reference.py", "experiments/conpty/run.py")
    archive = load(ROOT / "local/node-deadline-preflight/old-source-identity.json")
    require(archive["original_head"] == reference_commit, "Reference source commit differs")
    reference_hashes = {}
    for name in references:
        entry = next(row for row in archive["git_blob_archive"] if row["path"] == name)
        require(digest((ROOT / name).read_bytes()) == entry["git_blob_sha256"] ==
                digest((ROOT / "local/node-deadline-preflight/old-source" / name).read_bytes()), "Reference source changed")
        reference_hashes[name] = hex_identity(entry["git_blob_sha256"], 64)
    result = {"schema": "termforge-node-contract-public-v1", "issue": 144, "execution_source_commit": EXECUTED_SOURCE,
              "authorized_plan_sha256": PLAN_SHA256, "maximum_attempts": 1, "actual_case_count": 3,
              "cli_exit_code": 0, "batch_status": "expected_diagnostic_observations", "original_timeout_root_cause": None,
              "whole_lifecycle_bound_seconds": None, "windows_version": plan["windows_version"],
              "python_version_number": python_version[1], "python_version_text_sha256": digest(plan["python_version"].encode()),
              "python_exe_sha256": python_sha, "node_exe_sha256": node_sha,
              "source_files_sha256": source_hashes, "observations": public_cases,
              "reference_sources_unchanged_from_commit": reference_commit, "reference_sources_sha256": reference_hashes,
              "raw_files": raw_files, "preserved_prior_artifacts": preserved, "unexecuted_false_plans": false_plans,
              "exporter_sha256": digest(Path(__file__).read_bytes())}
    text = json.dumps(result, indent=2) + "\n"
    require(not re.search(r"[A-Za-z]:[\\/]|root_pid|TF_NODE_CONTRACT_V1|TF_NODE_CONTROLLED_EXCEPTION", text),
            "Public projection contains a private identity or raw stream")
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("plan", "cli-log", "exit-record", "case-dir", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    require(not args.out.exists(), "Output must not already exist")
    text = export(args.plan, args.cli_log, args.exit_record, args.case_dir)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(text, encoding="utf-8", newline="\n")
    print(json.dumps({"exported": True, "cases": 3, "sha256": digest(text.encode())}))


if __name__ == "__main__":
    main()
