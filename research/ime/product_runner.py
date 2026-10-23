"""Collect real product IME evidence only on the designated disposable hosted VM."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
from experiments.conpty.run import OwnedJob
from research.ime.inventory_hosted import claim_directory, error_identity, require_hosted, write_json

MODES = ("ui-ime-basic", "ui-ime-geometry", "ui-ime-focus", "ui-ime-close", "ui-ime-reading")
ENVIRONMENT_KEYS = ("GITHUB_ACTIONS", "RUNNER_ENVIRONMENT", "RUNNER_OS", "GITHUB_REPOSITORY", "ImageOS", "ImageVersion")


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def file_identity(path):
    digest = hashlib.sha256()
    size = 0
    with Path(path).open("rb") as stream:
        for part in iter(lambda: stream.read(65536), b""):
            size += len(part)
            digest.update(part)
    return {"bytes": size, "sha256": digest.hexdigest()}


def _run_gated_command(mode, directory, command, timeout):
    """Contain a fixed GO worker; local tests use benign Python children only.

    This private transport does not drive input or alter desktops. The public
    product entry point performs its hosted guard before any directory claim.
    """
    directory = Path(directory)
    claim_directory(directory)
    started = time.monotonic()
    record = {"schema": "termforge-product-ime-case-evidence-v1", "mode": mode,
              "status": "running", "started_utc": utc_now(), "ended_utc": None,
              "timeout_seconds": timeout, "elapsed_seconds": None, "pid": None,
              "returncode": None, "timed_out": False, "assigned": False,
              "gate_released": False, "process_reaped": False,
              "forced_job_termination": False, "outer_job_before_cleanup": None,
              "outer_job_after": None, "error": None, "cleanup_errors": [],
              "evidence_errors": [], "lifecycle": [], "streams": {},
              "real_ime_product_verified": False}

    def checkpoint(stage):
        record["lifecycle"].append({"stage": stage, "utc": utc_now(),
                                    "elapsed_seconds": time.monotonic() - started})
        write_json(directory / "result.json", record)

    job = process = None
    checkpoint("starting")
    try:
        with (directory / "stdout.jsonl").open("wb") as stdout, (directory / "stderr.txt").open("wb") as stderr:
            try:
                job = OwnedJob()
                process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=stdout, stderr=stderr,
                                           creationflags=subprocess.CREATE_NO_WINDOW)
                record["pid"] = process.pid
                checkpoint("awaiting-go")
                job.assign(process.pid)
                record["assigned"] = True
                checkpoint("job-assigned")
                # The native launcher does not create its product process or
                # send input before this gate. No desktop argument is supplied.
                record["gate_released"] = True
                checkpoint("releasing-go")
                try:
                    process.communicate(b"GO\n", timeout=timeout)
                    record["returncode"] = process.returncode
                except subprocess.TimeoutExpired:
                    record["timed_out"] = True
                    record["status"] = "timed-out"
                    record["error"] = {"kind": "deadline-exceeded", "code": None}
                if not record["timed_out"]:
                    record["outer_job_before_cleanup"] = job.drain(timeout=5)
                    if record["outer_job_before_cleanup"]["active"]:
                        raise RuntimeError("owned product descendants remained after launcher exit")
                    if process.returncode != 0:
                        record["status"] = "failed"
                        record["error"] = {"kind": "native-failed", "code": process.returncode}
                    else:
                        record["status"] = "collected"
                else:
                    record["outer_job_before_cleanup"] = job.accounting()
            except Exception as error:
                record["status"] = "failed"
                record["error"] = record["error"] or error_identity(error)
            finally:
                # An assignment failure leaves only the gated launcher, which
                # has received no GO and cannot have created an input target.
                if job and record["assigned"]:
                    before = record["outer_job_before_cleanup"]
                    if process.poll() is None or before is None or before["active"]:
                        try:
                            job.terminate()
                            record["forced_job_termination"] = True
                        except Exception as error:
                            record["cleanup_errors"].append(error_identity(error))
                if process:
                    try:
                        if process.poll() is None:
                            process.kill()
                        process.communicate(timeout=5)
                        record["returncode"] = process.returncode
                        record["process_reaped"] = process.poll() is not None
                    except Exception as error:
                        record["cleanup_errors"].append(error_identity(error))
                if job:
                    try:
                        record["outer_job_after"] = job.drain(timeout=5)
                        if record["outer_job_after"]["active"]:
                            record["cleanup_errors"].append({"kind": "owned-job-not-empty", "code": None})
                    except Exception as error:
                        record["cleanup_errors"].append(error_identity(error))
                    finally:
                        try:
                            job.close()
                        except Exception as error:
                            record["cleanup_errors"].append(error_identity(error))
    except Exception as error:
        record["status"] = "failed"
        record["error"] = record["error"] or error_identity(error)

    for filename in ("stdout.jsonl", "stderr.txt"):
        try:
            record["streams"][filename] = file_identity(directory / filename)
        except OSError as error:
            record["evidence_errors"].append({"artifact": filename, **error_identity(error)})
    record["ended_utc"] = utc_now()
    record["elapsed_seconds"] = time.monotonic() - started
    if record["cleanup_errors"]:
        record["status"] = "cleanup-failed"
    elif record["evidence_errors"]:
        record["status"] = "evidence-failed"
    try:
        checkpoint("finished")
    except OSError as error:
        record["status"] = "evidence-failed"
        record["error"] = record["error"] or error_identity(error)
        record["evidence_errors"].append({"artifact": "result.json", **error_identity(error)})
    return record


def execute_hosted_ime(launcher, out, timeout=60):
    # Guard before even resolving an executable or creating an output parent.
    # Tests never forge these values to run the public input path locally.
    require_hosted(os.environ)
    if os.name != "nt":
        raise ValueError("product IME requires Windows")
    if type(timeout) not in (int, float) or not 45 <= timeout <= 60:
        raise ValueError("each product IME case timeout must be between 45 and 60 seconds")
    launcher = Path(launcher).resolve(strict=True)
    siblings = (launcher, launcher.with_name("termforge_shell.exe"), launcher.with_name("shell_ime_client.exe"))
    identities = {path.name: file_identity(path) for path in siblings}
    out = Path(out).resolve()
    workspace = Path(os.environ.get("GITHUB_WORKSPACE", ROOT)).resolve()
    if not out.is_relative_to(workspace) or out == workspace:
        raise ValueError("product IME evidence must be in a new directory within the workspace")
    claim_directory(out)
    report = {"schema": "termforge-product-ime-evidence-v1", "status": "running",
              "started_utc": utc_now(), "ended_utc": None,
              "environment": {key: os.environ[key] for key in ENVIRONMENT_KEYS},
              "binaries": identities, "desktop_policy": "existing-input-desktop-no-create-switch-or-delete",
              "timeout_seconds_per_case": timeout, "cases": [], "real_ime_product_verified": False}
    write_json(out / "manifest.json", report)
    from research.ime.product_checks import validate_product_case
    for mode in MODES:
        directory = out / mode
        row = _run_gated_command(mode, directory,
                                 [str(launcher), "--hosted-ime", mode, str(directory / "frame.bmp")], timeout)
        if row["status"] == "collected":
            try:
                row["verification"] = validate_product_case(directory, mode, row)
                row["status"] = "passed"
                row["real_ime_product_verified"] = True
            except (OSError, ValueError, TypeError, KeyError) as error:
                row["status"] = "failed"
                row["error"] = {"kind": "product-oracle-rejected", "code": None, "detail": str(error)}
            write_json(directory / "result.json", row)
        report["cases"].append(row)
        write_json(out / "manifest.json", report)
        # Only continue on a fresh case if the previous owned process tree is
        # actually empty. An unknown cleanup state cannot authorize more input.
        if not row["process_reaped"] or row["cleanup_errors"] or not row["outer_job_after"] or row["outer_job_after"]["active"]:
            break
    report["real_ime_product_verified"] = (len(report["cases"]) == len(MODES) and
                                           all(row["real_ime_product_verified"] for row in report["cases"]))
    report["status"] = "product-ime-passed" if report["real_ime_product_verified"] else "product-ime-failed"
    report["ended_utc"] = utc_now()
    write_json(out / "manifest.json", report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--launcher", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=60)
    args = parser.parse_args()
    try:
        result = execute_hosted_ime(args.launcher, args.out, args.timeout)
    except (OSError, ValueError) as error:
        parser.exit(2, str(error) + "\n")
    print(json.dumps({"status": result["status"], "real_ime_product_verified": result["real_ime_product_verified"]}))
    return 0 if result["real_ime_product_verified"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
