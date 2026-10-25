"""Capture real shell/pager/editor output and compare both cores in recovery mode."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess

from cases import Case
from compare_candidate import run_case
from experiments.build_paths import binary as native_binary
from run import ROOT, sha, write_json
from experiments.candidates.alacritty.run import VERSION as CANDIDATE_VERSION, vendor_sources


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
    try:
        process = subprocess.run([str(binary), str(output), str(vim)], capture_output=True, timeout=60)
    except subprocess.TimeoutExpired as error:
        raise RuntimeError("real application capture timed out") from error
    output.mkdir(parents=True, exist_ok=True)
    (output / "capture.stdout.txt").write_bytes(process.stdout)
    (output / "capture.stderr.txt").write_bytes(process.stderr)
    if process.returncode != 0:
        raise RuntimeError(f"real application capture failed: {process.returncode}")
    manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("shell_exit") != 0 or manifest.get("pager_exit") != 0 or manifest.get("editor_exit") != 0 \
            or manifest.get("editor_file_verified") is not True:
        raise ValueError("real application lifecycle or editor result was not verified")
    return manifest


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
               "experiments/core/screen.hpp", "experiments/core/trace.cpp",
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
