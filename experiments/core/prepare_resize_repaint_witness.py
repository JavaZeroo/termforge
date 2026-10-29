"""Freeze #156's synthetic input and local plan; never build or run the witness."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

BASE = "58fee2805cde34c67683a79473ca56f9df3cbf69"
COMPARISON = "6a10e5322a1bddeb038670e50f32ff6deac145cf"
ARCHIVE_SHA = "ea38ca1099f6f52420d77bee16796d04e986eab02c8912a692e013bf4d12282a"
TRANSPORT_SHA = "c1d7cb18d5da399a838c997e0abe9375fd4db037e9f8f609ac02e1a58878cb84"
MEMBER = "test-artifacts/text-actions/run-rhfh7zlm/system-0/actions-geometry-bottom.transport.bin"
ROOT = Path(__file__).resolve().parents[2]
ORIGINAL_EIGHT = (
    "experiments/core/terminal.hpp", "experiments/core/screen.hpp",
    "experiments/core/text_document.hpp", "experiments/core/text_selection.hpp",
    "experiments/shell/shell.cpp", "experiments/shell/resize.hpp",
    "experiments/shell/workspace_actions_scenario.hpp", "experiments/shell/text_client.cpp",
)
SOURCES = ORIGINAL_EIGHT + (
    "experiments/core/text_projection.hpp", "experiments/core/unicode_width.hpp",
    "experiments/shell/text_interactions.hpp", "CMakeLists.txt",
    "experiments/core/resize_repaint_witness.cpp",
    "experiments/core/prepare_resize_repaint_witness.py",
    "docs/research/resize-repaint-witness-plan.md",
)
CASES = [
    {"case": "A", "fourth_prefix_bytes": 231, "cut_global_offset": 861, "predicted_bad_state": False},
    {"case": "B", "fourth_prefix_bytes": 204, "cut_global_offset": 834, "predicted_bad_state": True},
    {"case": "C", "fourth_prefix_bytes": 225, "cut_global_offset": 855, "predicted_bad_state": False},
    {"case": "D", "fourth_prefix_bytes": 219, "cut_global_offset": 849, "predicted_bad_state": True},
]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_identity(path: Path) -> dict:
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(65536), b""):
            digest.update(chunk)
            size += len(chunk)
    return {"bytes": size, "sha256": digest.hexdigest()}


def git(*arguments: str) -> str:
    # Read-only Git queries are the only child commands of preparation.
    result = subprocess.run(["git", *arguments], cwd=ROOT, check=True, capture_output=True)
    return result.stdout.decode("utf-8").strip()


def fixture_rows(first: int, count: int) -> bytes:
    # Original text_client.cpp fixture, not captured terminal or image content.
    return "".join(f"TF_FIND_{row:03d} needle 中文 e\u0301\r\n" for row in range(first, first + count)).encode("utf-8")


def redraw(expanded: bool) -> bytes:
    data = b"\x1b[?25l\x1b[H"
    for row in range(75, 80):
        data += fixture_rows(row, 1)[:-2] + b"\x1b[K\r\n"
    data += b"TF_FIND_READY 080\x1b[K\r\n"
    data += b"\r\n".join([b"\x1b[K"] * (4 if expanded else 1))
    if expanded:
        data += b"\x1b[7;1H"
    return data + b"\x1b[?25h"


def prepare(archive: Path, output: Path) -> dict:
    require(not git("status", "--porcelain", "--untracked-files=all"), "commit preparation sources before freezing")
    source_commit = git("rev-parse", "HEAD")
    require(git("merge-base", BASE, source_commit) == BASE, "preparation is not based on the failed source")
    require(output.is_relative_to(ROOT / "local"), "private plan and generated input must stay in this worktree's ignored local directory")
    require(not output.exists(), "output must be new; existing plans and attempts are immutable")
    normal, expanded = redraw(False), redraw(True)
    require(len(normal) == 210 and len(expanded) == 231, "constructed block length differs")
    transport = normal * 3 + expanded + normal
    require(len(transport) == 1071 and sha(transport) == TRANSPORT_SHA, "constructed transport identity differs")
    selected = fixture_rows(0, 80) + b"TF_FIND_READY 080\r\n"
    require(len(selected) == 2499, "original fixture literal size differs")
    before = file_identity(archive)
    require(before["sha256"] == ARCHIVE_SHA, "failure archive identity differs")
    with zipfile.ZipFile(archive) as bundle:
        entry = bundle.getinfo(MEMBER)
        require(entry.file_size == 1071, "failure transport length differs")
        observed = bundle.read(entry)
    require(observed == transport, "constructed bytes differ from the archived transport")
    require(file_identity(archive) == before, "failure archive changed during preparation")
    sources = []
    for name in SOURCES:
        path = ROOT / name
        sources.append({"path": name, "git_blob": git("rev-parse", f"{source_commit}:{name}"), **file_identity(path)})
    comparisons = []
    for name in ORIGINAL_EIGHT:
        failed = git("rev-parse", f"{BASE}:{name}")
        current = git("rev-parse", f"{COMPARISON}:{name}")
        require(failed == current, "original eight files differ at the explicit comparison source")
        require(failed == git("rev-parse", f"{source_commit}:{name}"), "preparation changed original code")
        comparisons.append({"path": name, "git_blob": failed})
    plan = {
        "schema": "termforge-resize-repaint-plan-v1", "issue": 156,
        "protocol": "termforge-resize-repaint-witness-v1", "execution_authorized": False,
        "source_commit": source_commit, "failed_source_commit": BASE,
        "comparison_commit": COMPARISON, "original_eight_equal": comparisons, "source_files": sources,
        "archive": {"path": str(archive), **before, "transport_member": MEMBER, "unchanged_after_read": True},
        "input": {"file": "transport.bin", "bytes": len(transport), "sha256": sha(transport),
                  "origin": "constructed from original TermForge synthetic markers and fixed VT; archive byte equality checked",
                  "block_lengths": [210, 210, 210, 231, 210],
                  "block_sha256": [sha(part) for part in [normal, normal, normal, expanded, normal]],
                  "selected_literal_bytes": len(selected), "selected_literal_sha256": sha(selected),
                  "seed_feed_bytes": len(b"\x1b[2J\x1b[H" + selected),
                  "seed_feed_sha256": sha(b"\x1b[2J\x1b[H" + selected)},
        "cases": CASES, "case_attempts_each": 1, "maximum_batches": 1,
        "initial": {"columns": 111, "rows": 7, "history_capacity": 100, "document_rows_required": 82,
                    "history_rows_required": 75, "cursor_row_required": 6, "cursor_column_required": 0,
                    "selection_bytes_required": 2499, "identity_scope": "fresh parser relative IDs; not CI absolute IDs"},
        "leading_grids_columns_rows": [[112, 7], [112, 7], [111, 7]],
        "expanded_grid_columns_rows": [149, 10], "restored_grid_columns_rows": [111, 7],
        "budgets": {"future_process_timeout_seconds": 10, "stdout_bytes_max": 2097152,
                    "stderr_bytes_max": 65536, "checkpoints_required": 51, "checkpoint_bytes_max": 40000,
                    "document_rows_max": 110, "copy_bytes_max": 8192},
        "build_intent": {"generator": "Visual Studio 17 2022", "architecture": "x64", "configuration": "Release",
                         "target": "resize_repaint_witness", "parallel": 1,
                         "definitions": {"BUILD_TESTING": "OFF", "TERMFORGE_BUILD_QT_SHELL": "OFF",
                                         "TERMFORGE_BUILD_CANDIDATE_ADAPTER": "OFF"},
                         "language": "C++17", "target_options": ["/W4", "/utf-8"],
                         "actual_compiler": None, "actual_flags": None, "actual_executable": None,
                         "actual_compile_log": None, "not_built_or_run": True},
        "later_execution_contract": {"absolute_executable_and_input": True, "recheck_all_frozen_inputs_before_launch": True,
                                     "retain_stdout_stderr_exit_even_on_failure": True, "postcheck_all_sources_and_archive": True,
                                     "no_cut_scan_retry_or_selection": True, "no_ctest_registration": True},
        "diagnostic_complete": None, "semantic_acceptance": False, "ci_cause_established": False,
        "limitations": ["preparation only; no compiler, core or witness process executed",
                        "missing CI parser offsets/cursor prevent attribution to its actual timing",
                        "synthetic seed recreates required shape, not every pre-stage CI input or ID gap",
                        "native/API completion is not parser or child acknowledgement",
                        "FNV summaries are comparison aids, SHA-256 binds input/source/raw identity"],
    }
    output.mkdir(parents=True, exist_ok=False)
    (output / "transport.bin").write_bytes(transport)
    encoded = (json.dumps(plan, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    (output / "plan.json").write_bytes(encoded)
    return {"source_commit": source_commit, "plan_sha256": sha(encoded), "transport_sha256": sha(transport),
            "source_files": len(sources), "cases": len(CASES), "execution_authorized": False}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    arguments = parser.parse_args()
    print(json.dumps(prepare(arguments.archive.resolve(strict=True), arguments.out.resolve()), sort_keys=True))
