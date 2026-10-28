"""Windows-only, owned diagnostic of the existing synthetic Node contract."""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from experiments.conpty.run import OwnedJob

CASES = ("normal", "hang-vm-helpers", "throw-before-grab")
EXECUTION_SECONDS = 10
CLEANUP_SECONDS = 5
PREFIX = b"TF_NODE_CONTRACT_V1:"
FIXTURE_CASES = ("normal", "grab", "toImage", "empty", "dpr", "pixel", "destroy")
STAGES = ("node-first-checkpoint", "go-received", "fixture-before-load", "before-file-read",
          "file-read", "vm-helpers-start", "vm-helpers-ready", "pure-checks-done") + tuple(
    stage for case in FIXTURE_CASES for stage in ("case-start:" + case, "case-end:" + case)
) + ("final",)
SOURCE_FILES = ("research/ui/node_contract_deadline.py", "research/ui/node_contract_bootstrap.js",
                "research/ui/test_raster_native_contract.js", "research/ui/raster_style_reference.js",
                "experiments/conpty/run.py")
NORMAL_STDOUT = "module paths, QRgb, ROI/ink gates,32B sret and normal-return destructor order passed\n"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_identity():
    return {name: sha(ROOT / name) for name in SOURCE_FILES}


class CheckpointError(ValueError):
    """Keep verified prefix rows when the next row is truncated or invalid."""
    def __init__(self, message, observations):
        super().__init__(message)
        self.observations = observations


def trace_result(observations):
    return {"observations": observations,
            "last_observed_stage": observations[-1]["stage"] if observations else None,
            "complete": len(observations) == len(STAGES)}


def read_checkpoints(stderr, root_pid):
    observations = []
    previous_clock = -1
    for line in stderr.splitlines():
        if not line.startswith(PREFIX):
            continue
        try:
            row = json.loads(line[len(PREFIX):].decode("utf-8"))
            index = len(observations)
            require(type(row) is dict and index < len(STAGES), "Unexpected checkpoint count")
            fields = {"sequence", "stage", "pid", "monotonic_ns"}
            if index == 0:
                fields |= {"node_version", "arch", "platform"}
                require(type(row.get("node_version")) is str and
                        re.fullmatch(r"v[0-9][A-Za-z0-9.+_-]{0,47}", row["node_version"]), "Invalid Node version")
                require(row.get("arch") in ("x64", "arm64", "ia32") and row.get("platform") == "win32",
                        "Unexpected Node runtime")
            require(set(row) == fields, "Checkpoint fields differ")
            require(type(row["sequence"]) is int and row["sequence"] == index and
                    row["stage"] == STAGES[index], "Checkpoint order differs")
            require(type(row["pid"]) is int and row["pid"] == root_pid, "Checkpoint PID differs")
            clock = row["monotonic_ns"]
            require(type(clock) is str and clock.isascii() and clock.isdecimal() and len(clock) <= 30,
                    "Invalid checkpoint clock")
            require(int(clock) >= previous_clock, "Checkpoint clock moved backwards")
            previous_clock = int(clock)
        except (ValueError, KeyError, UnicodeError, TypeError) as error:
            raise CheckpointError(str(error), observations) from error
        observations.append(row)
    return trace_result(observations)


def diagnose_node(node, case):
    require(os.name == "nt", "This diagnostic requires Windows owned Jobs")
    require(case in CASES, "Unexpected diagnostic case")
    node = node.resolve(strict=True)
    bootstrap = ROOT / "research/ui/node_contract_bootstrap.js"
    record = {"schema": "termforge-node-contract-observation-v1", "case": case, "status": "failed",
              "execution_budget_seconds": EXECUTION_SECONDS, "cleanup_budget_seconds": CLEANUP_SECONDS,
              "source_files_sha256": source_identity(), "node_sha256": sha(node),
              "process_created": False, "root_reaped": False, "root_in_owned_job": False,
              "cleanup_verified": False, "forced_job_termination": False, "job_after": None,
              "primary_exception": None, "stdout": "", "stderr": ""}
    job, process, assigned = None, None, False
    stdout, stderr = b"", b""
    cleanup_errors = []
    cleanup_deadline = None

    def remaining_cleanup():
        nonlocal cleanup_deadline
        if cleanup_deadline is None:
            cleanup_deadline = time.monotonic() + CLEANUP_SECONDS
        return max(0, cleanup_deadline - time.monotonic())

    try:
        job = OwnedJob()
        create_begin = time.monotonic()
        process = subprocess.Popen([str(node), str(bootstrap), case], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        record.update(process_created=True, root_pid=process.pid,
                      process_creation_elapsed_seconds=time.monotonic() - create_begin)
        # The original test's ten-second wait is not extended by Job/GO setup.
        execution_deadline = time.monotonic() + EXECUTION_SECONDS
        job.assign(process.pid)
        assigned = True
        api = job.api
        api.IsProcessInJob.argtypes = [wintypes.HANDLE, wintypes.HANDLE, ctypes.POINTER(wintypes.BOOL)]
        api.IsProcessInJob.restype = wintypes.BOOL
        in_job = wintypes.BOOL()
        job.check(api.IsProcessInJob(wintypes.HANDLE(int(process._handle)), job.handle, ctypes.byref(in_job)))
        require(bool(in_job.value), "Retained root handle is not in the owned Job")
        record["root_in_owned_job"] = True
        record["job_before_go"] = job.accounting()
        communicate_begin = time.monotonic()
        try:
            stdout, stderr = process.communicate(b"GO\n", timeout=max(0, execution_deadline - time.monotonic()))
        except subprocess.TimeoutExpired as error:
            stdout, stderr = error.output or b"", error.stderr or b""
            record["status"] = "timed_out"
            record["primary_exception"] = {"type": "TimeoutExpired", "timeout_seconds": EXECUTION_SECONDS,
                                           "message": str(error)}
            try:
                record["job_at_timeout"] = job.accounting()
            except Exception as query_error:
                cleanup_errors.append("query Job at timeout: " + str(query_error))
        finally:
            record["communicate_elapsed_seconds"] = time.monotonic() - communicate_begin
    except Exception as error:
        if record["primary_exception"] is None:
            record["primary_exception"] = {"type": type(error).__name__, "message": str(error)}
        else:
            cleanup_errors.append("secondary observation: " + str(error))
    finally:
        if process is not None:
            try:
                if process.poll() is None:
                    remaining_cleanup()
                    if assigned:
                        job.terminate()
                        record["forced_job_termination"] = True
                    else:
                        process.kill()
                    stdout, stderr = process.communicate(timeout=remaining_cleanup())
                record["root_reaped"] = process.poll() is not None
                record["exit_code"] = process.returncode
            except Exception as error:
                cleanup_errors.append("reap root: " + str(error))
        if job is not None:
            try:
                record["job_after"] = job.drain(timeout=remaining_cleanup())
                record["cleanup_verified"] = (record["root_reaped"] and record["root_in_owned_job"] and
                                               record["job_after"]["active"] == 0)
            except Exception as error:
                cleanup_errors.append("query/drain owned Job: " + str(error))
            finally:
                try:
                    job.close()
                except Exception as error:
                    cleanup_errors.append("close owned Job: " + str(error))
        if process is not None:
            for stream in (process.stdin, process.stdout, process.stderr):
                try:
                    if stream is not None:
                        stream.close()
                except Exception as error:
                    cleanup_errors.append("close root stream: " + str(error))
        record.update(stdout=stdout.decode("utf-8", "replace"), stderr=stderr.decode("utf-8", "replace"))
        try:
            record["trace"] = read_checkpoints(stderr, record.get("root_pid"))
        except CheckpointError as error:
            record["trace"] = trace_result(error.observations)
            record["trace"]["protocol_error"] = str(error)
        if cleanup_errors:
            record["cleanup_errors"] = cleanup_errors
            record["cleanup_verified"] = False
        if record["status"] != "timed_out" and record["primary_exception"] is None:
            if (record.get("exit_code") == 0 and record["cleanup_verified"] and record["trace"]["complete"] and
                    not record["forced_job_termination"] and record["stdout"] == NORMAL_STDOUT):
                record["status"] = "passed"
    return record


def expected_observation(row):
    """Recognize this fixed diagnostic plan without changing child verdicts."""
    if not row["cleanup_verified"] or row["trace"].get("protocol_error"):
        return False
    if row["case"] == "normal":
        return row["status"] == "passed"
    if row["case"] == "hang-vm-helpers":
        return (row["status"] == "timed_out" and row["trace"]["last_observed_stage"] == "vm-helpers-start" and
                row["primary_exception"] is not None and row["primary_exception"]["type"] == "TimeoutExpired" and
                row["forced_job_termination"])
    return (row["case"] == "throw-before-grab" and row["status"] == "failed" and row["exit_code"] != 0 and
            not row["forced_job_termination"] and row["trace"]["last_observed_stage"] == "case-start:grab" and
            "TF_NODE_CONTROLLED_EXCEPTION" in row["stderr"])


def prepare_plan(destination, output):
    destination, output = destination.resolve(), output.resolve()
    require(destination.is_relative_to(ROOT / "local") and output.is_relative_to(ROOT / "local"),
            "Plan and output must be owned local paths")
    require(not destination.exists() and not output.exists(), "Plan/output already exists")
    node = Path(shutil.which("node") or "").resolve(strict=True)
    require(node.is_file(), "Node executable is missing")
    data = {"schema": "termforge-node-contract-plan-v1", "issue": 144, "execution_authorized": False,
            "maximum_attempts": 1, "execution_budget_seconds": EXECUTION_SECONDS,
            "cleanup_budget_seconds": CLEANUP_SECONDS, "cases": list(CASES),
            "source_base_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
            "source_files_sha256": source_identity(), "node": str(node), "node_sha256": sha(node),
            "python": str(Path(sys.executable).resolve()), "python_sha256": sha(Path(sys.executable)),
            "python_version": sys.version, "windows_version": platform.version(), "out": str(output),
            "whole_lifecycle_bound_seconds": None,
            "scope": "One diagnostic batch: normal, controlled hang and controlled exception; no original-root-cause claim"}
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")
    return data


def run_plan(path):
    path = path.resolve(strict=True)
    plan = json.loads(path.read_text(encoding="utf-8"))
    require(plan.get("execution_authorized") is True, "This is an offline proposal; execution is not authorized")
    require(plan.get("schema") == "termforge-node-contract-plan-v1" and plan.get("issue") == 144 and
            plan.get("maximum_attempts") == 1 and plan.get("cases") == list(CASES) and
            plan.get("execution_budget_seconds") == EXECUTION_SECONDS and
            plan.get("cleanup_budget_seconds") == CLEANUP_SECONDS, "Plan contract differs")
    require(plan["source_base_commit"] == subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip() and
            plan["source_files_sha256"] == source_identity(), "Plan source identity changed")
    node = Path(plan["node"]).resolve(strict=True)
    require(sha(node) == plan["node_sha256"] and Path(sys.executable).resolve() == Path(plan["python"]) and
            sha(Path(sys.executable)) == plan["python_sha256"], "Runtime identity changed")
    output = Path(plan["out"]).resolve()
    require(path.is_relative_to(ROOT / "local") and output.is_relative_to(ROOT / "local") and not output.exists(),
            "Output must be a new owned local directory")
    with path.with_suffix(".attempted").open("x", encoding="utf-8") as marker:
        marker.write("One diagnostic attempt; retain after success or failure.\n")
    output.mkdir(parents=True)
    observations = []
    for index, case in enumerate(CASES):
        row = diagnose_node(node, case)
        (output / f"{index}-{case}.json").write_text(json.dumps(row, indent=2) + "\n", encoding="utf-8", newline="\n")
        observations.append(row)
        # Failed containment is terminal and never releases the next case.
        if not row["cleanup_verified"]:
            break
    complete = len(observations) == len(CASES) and all(expected_observation(row) for row in observations)
    return {"schema": "termforge-node-contract-batch-v1", "observations": observations,
            "status": "expected_diagnostic_observations" if complete else "failed",
            "original_timeout_root_cause": None, "whole_lifecycle_bound_seconds": None}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    prepare = commands.add_parser("prepare-plan")
    prepare.add_argument("--plan", type=Path, required=True)
    prepare.add_argument("--out", type=Path, required=True)
    run = commands.add_parser("run-plan")
    run.add_argument("--plan", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "prepare-plan":
        prepare_plan(args.plan, args.out)
        print(json.dumps({"prepared": True, "execution_authorized": False}))
        return 0
    result = run_plan(args.plan)
    print(json.dumps(result))
    return 0 if result["status"] == "expected_diagnostic_observations" else 1


if __name__ == "__main__":
    raise SystemExit(main())
