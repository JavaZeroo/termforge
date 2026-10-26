"""Capture real shell/pager/editor output and compare both cores in recovery mode."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import re

from cases import Case
from compare_candidate import run_case
from experiments.build_paths import binary as native_binary
from run import ROOT, sha, write_json
from experiments.candidates.alacritty.run import VERSION as CANDIDATE_VERSION, vendor_sources
from experiments.conpty.run import OwnedJob


class FileCase(Case):
    @property
    def data(self):
        return Path(self.text).read_bytes()


def vim_executable():
    return shutil.which("vim")


def capture_binary():
    value = os.environ.get("TERMFORGE_REAL_APP_CAPTURE")
    return Path(value).resolve(strict=True) if value else native_binary("real_app_capture")


def run_capture(binary, vim, output):
    if output.exists():
        raise ValueError("capture output directory already exists")
    job = process = None
    assigned = False
    stdout = stderr = b""
    supervision = dict(schema="termforge-real-app-capture-supervision-v1", timed_out=False,
                       returncode=None, job_after=None, cleanup_errors=[])
    try:
        job = OwnedJob()
        process = subprocess.Popen([str(binary), str(output), str(vim)], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        job.assign(process.pid)
        assigned = True
        try:
            stdout, stderr = process.communicate(b"GO\n", timeout=60)
        except subprocess.TimeoutExpired:
            supervision["timed_out"] = True
    finally:
        try:
            if process is not None and process.poll() is None:
                try:
                    if assigned:
                        job.terminate()
                    else:
                        process.kill()  # no GO was sent; no PTY clients exist
                except OSError:
                    supervision["cleanup_errors"].append("terminate_failed")
                try:
                    stdout, stderr = process.communicate(timeout=5)
                except (OSError, subprocess.TimeoutExpired):
                    supervision["cleanup_errors"].append("reap_failed")
            if job is not None and assigned:
                try:
                    supervision["job_after"] = job.drain()
                    if supervision["job_after"]["active"]:
                        job.terminate()
                        supervision["job_after"] = job.drain()
                        if supervision["job_after"]["active"]:
                            supervision["cleanup_errors"].append("job_not_empty")
                except OSError:
                    supervision["job_after"] = None
                    supervision["cleanup_errors"].append("accounting_failed")
        finally:
            if job is not None:
                try:
                    job.close()  # final kill-on-close fallback if termination failed
                except OSError:
                    supervision["cleanup_errors"].append("job_close_failed")
        if process is not None:
            try:
                if process.poll() is None:
                    stdout, stderr = process.communicate(timeout=5)
                supervision["returncode"] = process.poll()
            except (OSError, subprocess.TimeoutExpired):
                supervision["cleanup_errors"].append("reap_after_close_failed")
        output.mkdir(parents=True, exist_ok=True)
        (output / "capture.stdout.txt").write_bytes(stdout)
        (output / "capture.stderr.txt").write_bytes(stderr)
        write_json(output / "supervision.json", supervision)
    if supervision["timed_out"] or supervision["cleanup_errors"] or supervision["job_after"] is None \
            or supervision["job_after"]["active"] or supervision["returncode"] != 0:
        raise RuntimeError("real application capture failed")
    manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("shell_exit") != 0 or manifest.get("pager_exit") != 0 or manifest.get("editor_exit") != 0 \
            or manifest.get("editor_file_verified") is not True:
        raise ValueError("real application lifecycle or editor result was not verified")
    validate_pager_capture(output)
    return manifest


def validate_pager_capture(output):
    raw = (output / "pager.bin").read_bytes()
    lifecycle = json.loads((output / "pager-lifecycle.json").read_text(encoding="utf-8"))
    prompt = re.search(rb"-- More \([0-9]{1,3}%\) -- ", raw)
    if (not prompt or lifecycle.get("schema") != "termforge-more-capture-v1" or lifecycle.get("valid") is not True or
            lifecycle.get("prompt_end_offset") != prompt.end() or lifecycle.get("q_bytes") != 1 or
            lifecycle.get("output_bytes") != len(raw) or lifecycle.get("exit_code") != 0 or
            lifecycle.get("output_eof") is not True or lifecycle.get("read_error") != 109 or
            lifecycle.get("cleanup_complete") is not True or lifecycle.get("active_children_after") != 0 or
            lifecycle.get("forced_reader_cancel") is not False or lifecycle.get("forced_process_cancel") is not False):
        raise ValueError("pager prompt, exit or actual output EOF was not verified")


def run_suite(core_binary, candidate_binary, capture, vim, output):
    output.mkdir(parents=True, exist_ok=False)
    capture_output = output / "capture"
    manifest = run_capture(capture, vim, capture_output)
    records = []
    for name in ("shell", "pager", "editor"):
        report, core, candidate = run_case(core_binary, candidate_binary,
                                          FileCase(name, str(capture_output / f"{name}.bin")), "whole",
                                          output / name, ("--recovery",))
        report["candidate_final_rows"] = ["".join(cell["text"] for cell in row).rstrip()
                                          for row in candidate[-1]["screen"]["cells"]]
        write_json(output / name / "report.json", report)
        records.append(report)
    passed = all(record["status"] in ("match", "reference_difference") for record in records)
    sources = ["experiments/conpty/real_app_capture.cpp", "experiments/conpty/session.hpp",
               "experiments/core/screen.hpp", "experiments/core/terminal.hpp", "experiments/core/trace.cpp",
               "experiments/differential/real_app_matrix.py",
               "experiments/differential/compare_candidate.py", "experiments/differential/decisions.md",
               "experiments/candidates/alacritty/run.py"] + vendor_sources()
    version = subprocess.run([str(vim), "--version"], capture_output=True, timeout=10, check=True)
    vim_version = version.stdout.decode("utf-8", "replace").splitlines()[0]
    summary = dict(schema=1, platform=platform.platform(), candidate="alacritty_terminal", version=CANDIDATE_VERSION,
                   applications={"shell": "Windows cmd.exe", "pager": "Windows more.com", "editor": vim_version},
                   capture_sha256=sha(capture), core_trace_sha256=sha(core_binary),
                   candidate_trace_sha256=sha(candidate_binary),
                   source_sha256={path: sha(ROOT / path) for path in sources}, capture_manifest=manifest,
                   runs=len(records),
                   counts={status: sum(record["status"] == status for record in records)
                           for status in sorted({record["status"] for record in records})},
                   passed=passed, execution_passed=passed, real_application_covered=True,
                   semantic_acceptance=False, performance_measured=False, presented_done=None,
                   records=records)
    write_json(output / "summary.json", summary)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--capture", type=Path)
    parser.add_argument("--vim", type=Path)
    parser.add_argument("--out", type=Path, required=True, help="new output directory; never overwritten")
    args = parser.parse_args()
    vim = args.vim.resolve(strict=True) if args.vim else vim_executable()
    if not vim:
        raise FileNotFoundError("vim is required for the real application matrix")
    result = run_suite(args.trace.resolve(strict=True), args.candidate.resolve(strict=True),
                       capture_binary() if args.capture is None else args.capture.resolve(strict=True),
                       vim, args.out.resolve())
    print(json.dumps({key: result[key] for key in ("execution_passed", "runs", "counts",
                                                   "real_application_covered", "semantic_acceptance")}, indent=2))
    return 0 if result["execution_passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
