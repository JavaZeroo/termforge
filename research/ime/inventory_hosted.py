"""Read-only hosted IME inventory, with durable stages and owned-process deadlines."""
from __future__ import annotations

import argparse
import ctypes
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
from experiments.conpty.run import OwnedJob

OPERATIONS = ("os", "powershell", "dism", "ime-binary", "tip", "capability")


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def require_hosted(environment):
    expected = {"GITHUB_ACTIONS": "true", "RUNNER_ENVIRONMENT": "github-hosted",
                "RUNNER_OS": "Windows", "GITHUB_REPOSITORY": "JavaZeroo/termforge"}
    if (any(environment.get(key) != value for key, value in expected.items())
            or environment.get("ImageOS") not in ("win22", "win25", "win25-vs2026")
            or not re.fullmatch(r"\d{8}\.\d+\.\d+", environment.get("ImageVersion", ""))):
        raise ValueError("inventory requires the designated disposable GitHub-hosted Windows VM")


def claim_directory(path):
    # mkdir is the ownership operation, not an existence-check followed by mkdir.
    path.mkdir(parents=True, exist_ok=False)


def write_json(path, record):
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(record, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def error_identity(error):
    # Error messages can contain host paths. Publish only type and numeric code.
    return {"kind": type(error).__name__, "code": getattr(error, "winerror", None)}


def summarize_dism(path):
    """Whitelist a bounded log tail; no raw line, filename, URL or registry data."""
    if not path.is_file():
        return {"present": False}
    with path.open("rb") as stream:
        prefix = stream.read(2)
        size = stream.seek(0, 2)
        stream.seek(max(0, size - 65536))
        tail = stream.read(65536)
    encoding = "utf-16-le" if prefix == b"\xff\xfe" else "utf-8"
    text = tail.decode(encoding, errors="ignore")
    codes = sorted(set(code.upper().replace("0X", "0x") for code in re.findall(
        r"\b(?:HRESULT|hr)\s*[:=]\s*(0x[0-9a-f]{8})\b", text, flags=re.IGNORECASE)))
    return {"present": True, "bytes": size, "examined_bytes": len(tail),
            "truncated": size > len(tail), "hresults": codes[:16],
            "hresult_list_truncated": len(codes) > 16,
            "error_mentions": len(re.findall(r"\berror\b", text, re.IGNORECASE)),
            "warning_mentions": len(re.findall(r"\bwarning\b", text, re.IGNORECASE))}


def run_operation(operation, out, command, timeout):
    directory = out / operation
    claim_directory(directory)
    started = time.monotonic()
    record = {"schema": "termforge-ime-inventory-phase-v1", "phase": operation,
              "status": "running", "started_utc": utc_now(), "ended_utc": None,
              "elapsed_seconds": None, "timeout_seconds": timeout, "returncode": None,
              "error": None, "data": None, "pid": None, "process_reaped": False,
              "job_before_cleanup": None, "job_after": None, "lifecycle": [],
              "cleanup_errors": [], "evidence_errors": [], "dism_log_summary": None}

    def checkpoint(stage):
        record["lifecycle"].append({"phase": stage, "utc": utc_now(),
                                    "elapsed_seconds": time.monotonic() - started})
        write_json(directory / "phase.json", record)
        print(json.dumps({"operation": operation, "phase": stage}), flush=True)

    job = process = None
    stdout = stderr = b""
    checkpoint("starting-process")
    try:
        job = OwnedJob()
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, creationflags=subprocess.CREATE_NO_WINDOW)
        record["pid"] = process.pid
        checkpoint("process-started-awaiting-gate")
        job.assign(process.pid)
        checkpoint("job-assigned")
        # The fixed worker performs no environment operations until it reads GO.
        # This gates script work, not PowerShell host initialization before assign.
        checkpoint("releasing-query-gate")
        try:
            stdout, stderr = process.communicate(b"GO\n", timeout=timeout)
            record["returncode"] = process.returncode
        except subprocess.TimeoutExpired:
            record["status"] = "timed-out"
            record["error"] = {"kind": "deadline-exceeded", "code": None}
            checkpoint("deadline-exceeded")
        record["job_before_cleanup"] = job.accounting()
    except Exception as error:
        record["status"] = "failed"
        record["error"] = error_identity(error)
    finally:
        # Never terminate servicing services or unrelated PIDs. Job containment
        # covers only this script host and descendants created after assignment.
        try:
            if job:
                job.terminate()
        except Exception as error:
            record["cleanup_errors"].append(error_identity(error))
        try:
            if process:
                if process.poll() is None:
                    process.kill()  # also handles assignment failure before GO
                stdout, stderr = process.communicate(timeout=5)
                record["returncode"] = process.returncode
                record["process_reaped"] = process.poll() is not None
        except Exception as error:
            record["cleanup_errors"].append(error_identity(error))
        try:
            if job:
                record["job_after"] = job.drain(timeout=5)
                if record["job_after"]["active"]:
                    record["cleanup_errors"].append({"kind": "owned-job-not-empty", "code": None})
        except Exception as error:
            record["cleanup_errors"].append(error_identity(error))
        finally:
            if job:
                job.close()

    if record["cleanup_errors"]:
        record["status"] = "cleanup-failed"
    elif record["status"] == "running":
        try:
            if len(stdout) > 32768:
                raise ValueError("worker report too large")
            worker = json.loads(stdout.decode("utf-8-sig"))
            if (not isinstance(worker, dict) or worker.get("schema") != "termforge-ime-inventory-worker-v1"
                    or worker.get("operation") != operation
                    or worker.get("status") not in ("complete", "failed")
                    or not isinstance(worker.get("data"), dict)):
                raise ValueError("invalid worker report")
            record["data"] = worker["data"]
            record["status"] = "complete" if process.returncode == 0 and worker["status"] == "complete" else "failed"
            if record["status"] == "failed":
                record["error"] = {"kind": "worker-failed", "code": process.returncode}
        except (UnicodeError, ValueError, TypeError):
            record["status"] = "failed"
            record["error"] = {"kind": "invalid-worker-report", "code": None}
    record["ended_utc"] = utc_now()
    record["elapsed_seconds"] = time.monotonic() - started
    # Local private streams are deliberately excluded from artifact upload.
    for filename, contents in (("stdout.private", stdout), ("stderr.private", stderr)):
        try:
            (directory / filename).write_bytes(contents)
        except OSError as error:
            record["evidence_errors"].append({"artifact": filename, **error_identity(error)})
    try:
        record["dism_log_summary"] = summarize_dism(directory / "query.private.log")
    except OSError as error:
        record["evidence_errors"].append({"artifact": "dism-summary", **error_identity(error)})
    if record["evidence_errors"]:
        record["status"] = "evidence-failed"
        record["error"] = record["error"] or {"kind": "evidence-finalization-failed", "code": None}
    try:
        checkpoint("finished")
    except OSError as error:
        # The operation file can be unwritable while the parent summary remains
        # writable. Return its failed record so the parent can still preserve it.
        record["status"] = "evidence-failed"
        record["error"] = record["error"] or {"kind": "evidence-finalization-failed", "code": None}
        record["evidence_errors"].append({"artifact": "phase.json", **error_identity(error)})
    return record


def collect_inventory(out, powershell, query_timeout, image):
    report = {"schema": "termforge-hosted-ime-inventory-v1", "status": "running",
              "started_utc": utc_now(), "ended_utc": None, "read_only": True,
              "image_os": image["ImageOS"], "image_version": image["ImageVersion"],
              "real_ime_workflow_verified": False, "operations": [], "error": None}
    write_json(out / "inventory.json", report)
    worker = Path(__file__).with_name("inventory_worker.ps1")
    try:
        for operation in OPERATIONS:
            command = [str(powershell), "-NoLogo", "-NoProfile", "-NonInteractive",
                       "-ExecutionPolicy", "Bypass", "-File", str(worker),
                       "-Operation", operation, "-Out", str(out / operation)]
            result = run_operation(operation, out, command, query_timeout if operation == "capability" else 20)
            report["operations"].append(result)
            write_json(out / "inventory.json", report)
            if result["status"] != "complete" and (
                    result.get("process_reaped") is not True
                    or not isinstance(result.get("job_after"), dict)
                    or result["job_after"].get("active") != 0
                    or result.get("cleanup_errors")):
                # Other observations remain useful after a failed query, but
                # only once containment has positively proved our host is gone.
                break
    except Exception as error:
        report["error"] = error_identity(error)
    report["ended_utc"] = utc_now()
    report["status"] = ("inventory-complete-ime-unverified"
                        if report["error"] is None and len(report["operations"]) == len(OPERATIONS)
                        and all(result["status"] == "complete" for result in report["operations"])
                        else "inventory-failed")
    write_json(out / "inventory.json", report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--query-timeout", type=int, default=60, choices=range(5, 91))
    args = parser.parse_args()
    try:
        require_hosted(os.environ)
        if os.name != "nt" or not ctypes.windll.shell32.IsUserAnAdmin():
            raise ValueError("inventory requires an administrator token on Windows")
        workspace, out = Path(os.environ["GITHUB_WORKSPACE"]).resolve(), args.out.resolve()
        if out == workspace or not out.is_relative_to(workspace):
            raise ValueError("inventory evidence must remain below the checked-out workspace")
        powershell = Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
        if not powershell.is_file():
            raise ValueError("the fixed Windows PowerShell host is unavailable")
        claim_directory(out)
    except (ValueError, OSError, KeyError) as error:
        parser.error(str(error))
    report = collect_inventory(out, powershell, args.query_timeout, os.environ)
    return 0 if report["status"] == "inventory-complete-ime-unverified" else 1


if __name__ == "__main__":
    raise SystemExit(main())
