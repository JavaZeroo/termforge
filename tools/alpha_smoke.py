"""Validate an extracted Alpha bundle on owned, non-input Windows desktops.

The no-argument product uses the exact bundle. Existing scripted scenarios run
on a separate byte-identical copy with explicitly named test-only helpers.
Raw local diagnostics remain under _private; validation.json contains safe
facts and hashes, with no process IDs or absolute paths.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

RUNTIME_FILES = (
    "termforge_shell.exe", "shell_clipboard_reader.exe",
    "assets/fonts/RobotoMono-Regular.ttf", "assets/fonts/OFL.txt",
    "assets/fonts/README.md", "LICENSE",
)
SCENARIOS = ("normal", "resize", "close", "ui-main")
TEST_HELPERS = ("shell_test_launcher.exe", "shell_size_client.exe")


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def file_hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def identities(directory: Path) -> dict:
    result = {}
    for path in sorted(directory.rglob("*")):
        if path.is_symlink():
            raise ValueError("bundle must not contain symbolic links")
        if path.is_file():
            result[path.relative_to(directory).as_posix()] = {
                "bytes": path.stat().st_size, "sha256": file_hash(path),
            }
    return result


def disjoint(first: Path, second: Path) -> bool:
    return not first.is_relative_to(second) and not second.is_relative_to(first)


def normal_entry(bundle: Path, test_bin: Path, out: Path) -> dict:
    from experiments.conpty.run import OwnedJob
    from experiments.shell.run import OwnedTestDesktop

    out.mkdir()
    state = out / "state"
    state.mkdir()
    private = {"status": "failed"}
    process = job = desktop = None
    assigned = False
    try:
        job = OwnedJob()
        desktop = OwnedTestDesktop()
        environment = os.environ.copy()
        environment["LOCALAPPDATA"] = str(state.resolve())
        process = subprocess.Popen(
            [str(test_bin / "alpha_normal_smoke.exe"), desktop.full_name,
             str(bundle / "termforge_shell.exe"), str(state.resolve())],
            cwd=out, env=environment, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            creationflags=subprocess.CREATE_NO_WINDOW,
        )
        job.assign(process.pid)
        assigned = True
        try:
            stdout, stderr = process.communicate(b"GO\n", timeout=55)
            private["timed_out"] = False
        except subprocess.TimeoutExpired:
            private["timed_out"] = True
            job.terminate()
            stdout, stderr = process.communicate(timeout=10)
        private.update(stdout=stdout.decode("utf-8", "replace"),
                       stderr=stderr.decode("utf-8", "replace"), exit_code=process.returncode)
        if private["timed_out"] or process.returncode:
            raise ValueError("normal product entry or cleanup failed")
        if stderr:
            raise ValueError("normal product probe wrote unexpected stderr")
        row = json.loads(stdout)
        expected_true = (
            "no_arguments", "private_state", "workspace_visible", "terminal_visible",
            "real_system_cmd", "command_verified", "resize_verified",
        )
        if (row.get("schema") != "termforge-alpha-normal-smoke-v1" or
                any(row.get(key) is not True for key in expected_true) or
                row.get("gui_exit_code") != 0 or
                type(row.get("active_at_ready")) is not int or row["active_at_ready"] < 2 or
                row.get("presented_done", 1) is not None):
            raise ValueError("normal entry evidence does not meet the contract")
        for field in ("workspace_client_before", "terminal_client_before", "workspace_client_after"):
            value = row.get(field)
            if (not isinstance(value, list) or len(value) != 2 or
                    any(type(item) is not int or item <= 0 for item in value)):
                raise ValueError("normal entry rectangle missing")
        if any(a == b for a, b in zip(row["workspace_client_before"], row["workspace_client_after"])):
            raise ValueError("normal entry resize did not change both dimensions")
        inner = row.get("inner_job_after", {})
        if (type(inner.get("active")) is not int or inner["active"] != 0 or
                type(inner.get("total")) is not int or inner["total"] < 2 or
                type(inner.get("terminated")) is not int or inner["terminated"] != 0):
            raise ValueError("normal entry inner Job did not fully terminate")
        if (out / "alpha-command.txt").read_bytes() != b"TF_ALPHA_PACKAGE_NORMAL\r\n":
            raise ValueError("normal entry command bytes differ from the independent literal")
        private["status"] = "passed"
        return row
    except BaseException as error:
        private["error"] = str(error)
        raise
    finally:
        cleanup = []
        if process is not None and process.poll() is None:
            try:
                if assigned:
                    job.terminate()
                else:
                    process.kill()
                stdout, stderr = process.communicate(timeout=10)
                private.setdefault("stdout", stdout.decode("utf-8", "replace"))
                private.setdefault("stderr", stderr.decode("utf-8", "replace"))
                private.setdefault("exit_code", process.returncode)
            except (OSError, subprocess.TimeoutExpired) as error:
                cleanup.append(str(error))
        if job is not None:
            try:
                if assigned:
                    private["outer_job_after"] = job.drain()
                    if private["outer_job_after"]["active"]:
                        cleanup.append("outer Job remains active")
            except OSError as error:
                cleanup.append(str(error))
            finally:
                job.close()
        if desktop is not None:
            try:
                private["desktop_gone"] = desktop.close()
                if not private["desktop_gone"]:
                    cleanup.append("owned desktop remains")
            except OSError as error:
                cleanup.append(str(error))
        if cleanup:
            private["cleanup_errors"] = cleanup
            private["status"] = "failed"
        write_json(out / "diagnostic.private.json", private)
        if cleanup and "error" not in private:
            raise RuntimeError("normal entry cleanup failed")


def run(bundle: Path, test_bin: Path, out: Path) -> dict:
    if os.name != "nt":
        raise ValueError("Alpha package smoke requires Windows")
    bundle, test_bin, out = bundle.resolve(), test_bin.resolve(), out.resolve()
    if not all(disjoint(a, b) for a, b in ((bundle, out), (test_bin, out), (bundle, test_bin))):
        raise ValueError("bundle, test-bin and output must be separate directory trees")
    out.mkdir(parents=True, exist_ok=False)
    private = out / "_private"
    private.mkdir()
    report = {
        "schema": "termforge-alpha-package-smoke-v1", "status": "failed",
        "scope": "Windows owned non-input desktops; exact no-argument bundle entry and copied scripted harness",
        "system_clipboard": False, "effective_ime": False,
        "physical_keyboard_latency": None, "presented_done": None,
    }
    phase = "preflight"
    before = None
    try:
        if not bundle.is_dir() or not test_bin.is_dir():
            raise ValueError("bundle and test-bin must exist")
        before = identities(bundle)
        for name in RUNTIME_FILES:
            if name not in before:
                raise ValueError("bundle runtime or license file missing: " + name)
        for name in (*TEST_HELPERS, "alpha_normal_smoke.exe"):
            if name in before:
                raise ValueError("test-only helper is present in the release bundle")
            if not (test_bin / name).is_file():
                raise ValueError("test helper missing: " + name)
        report["bundle_files"] = before
        report["test_helpers"] = {
            name: {"bytes": (test_bin / name).stat().st_size, "sha256": file_hash(test_bin / name)}
            for name in (*TEST_HELPERS, "alpha_normal_smoke.exe")
        }
        report["smoke_sources"] = {
            name: file_hash(ROOT / "tools" / name)
            for name in ("alpha_normal_smoke.cpp", "alpha_smoke.py")
        }
        phase = "normal-entry"
        row = normal_entry(bundle, test_bin, private / "normal-entry")
        normal_private = json.loads((private / "normal-entry/diagnostic.private.json").read_text(encoding="utf-8"))
        report["normal_entry"] = {
            **row, "outer_job_after": normal_private["outer_job_after"],
            "desktop_gone": normal_private["desktop_gone"],
        }
        phase = "harness-copy"
        harness = private / "harness"
        shutil.copytree(bundle, harness)
        if identities(harness) != before:
            raise ValueError("scripted harness product files differ from the extracted bundle")
        for name in TEST_HELPERS:
            shutil.copyfile(test_bin / name, harness / name)
        from experiments.shell.run import execute
        report["scripted_scenarios"] = []
        for mode in SCENARIOS:
            phase = "scripted-" + mode
            sample = execute(harness / "termforge_shell.exe", mode, private / mode)
            if (sample.get("status") != "passed" or sample.get("exit_code") != 0 or
                    sample.get("timed_out") is not False or sample.get("desktop_gone") is not True or
                    sample.get("job_after_cleanup", {}).get("active") != 0):
                raise ValueError("scripted scenario cleanup or execution failed")
            # Publish only fixed safe facts, not raw screen contents, paths or IDs.
            result = sample["result"]
            safe_fields = (
                "valid", "mode", "cleanup_complete", "command_verified", "unicode_verified",
                "backspace_verified", "arrows_verified", "ctrl_c_verified", "resize_verified",
                "rows", "columns", "active_children_after", "sessions_closed", "main_sessions_verified",
                "focus_verified", "geometry_verified", "ui_font_family", "presented_done",
            )
            report["scripted_scenarios"].append({
                "mode": mode, "status": "passed", "exit_code": sample["exit_code"],
                "desktop_gone": sample["desktop_gone"], "outer_job_after": sample["job_after_cleanup"],
                "frame_sha256": sample["frame_sha256"],
                "result": {key: result[key] for key in safe_fields if key in result},
            })
        phase = "bundle-integrity"
        harness_after = identities(harness)
        expected_harness = dict(before)
        expected_harness.update({name: report["test_helpers"][name] for name in TEST_HELPERS})
        if harness_after != expected_harness:
            raise ValueError("harness product bytes changed or an unexpected file appeared")
        if identities(bundle) != before:
            raise ValueError("original extracted bundle changed during smoke")
        report["bundle_unchanged"] = True
        report["harness_product_bytes_unchanged"] = True
        report["status"] = "passed"
        return report
    except BaseException as error:
        report["failure_phase"] = phase
        report["error_type"] = type(error).__name__
        write_json(private / "failure.private.json", {"phase": phase, "error": str(error)})
        raise
    finally:
        if before is not None:
            try:
                report["bundle_unchanged"] = identities(bundle) == before
            except OSError:
                report["bundle_unchanged"] = None
        write_json(out / "validation.json", report)


def main() -> int:
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--bundle", type=Path, required=True)
    cli.add_argument("--test-bin", type=Path, required=True)
    cli.add_argument("--out", type=Path, required=True)
    args = cli.parse_args()
    try:
        result = run(args.bundle, args.test_bin, args.out)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print("Alpha package smoke failed: " + type(error).__name__, file=sys.stderr)
        return 1
    print(json.dumps({"status": result["status"], "normal_entry": True,
                      "scripted_scenarios": len(result["scripted_scenarios"]),
                      "bundle_unchanged": result["bundle_unchanged"]}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
