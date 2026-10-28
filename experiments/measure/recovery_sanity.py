"""One fixed correctness-only batch for the two recovery replay executables.

This is not a performance runner: there are no repetitions, summaries or speed
thresholds. Raw streams precede validation and output directories are never reused.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from experiments.measure.recovery import decode_response, require, validate_identity, validate_sample

FLOOR = .1
PROCESS_TIMEOUT = 90
CHUNKS = {"project": (1, 4096, 65536), "interactive": (1, 16, 22000)}
INVALID = (("calibrate", ".1"), ("parse", "4096", "0", ".1"),
           ("state", "4096", "0", ".1"), ("project", "0", "0", ".1"),
           ("project", "4096", "2", ".1"), ("project", "4096", "0", "nan"))
SOURCES = ("CMakeLists.txt", "experiments/core/terminal.hpp", "experiments/core/screen.hpp",
           "experiments/core/unicode_width.hpp", "experiments/measure/measure.cpp",
           "experiments/measure/recovery.py", "experiments/measure/recovery_sanity.py")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_sanity(wrapped, noalloc, out):
    binaries = ((True, wrapped.resolve(strict=True)), (False, noalloc.resolve(strict=True)))
    require(binaries[0][1] != binaries[1][1], "Distinct build paths are required")
    binary_hashes = {str(path): digest(path) for _, path in binaries}
    source_hashes = {name: digest(ROOT / name) for name in SOURCES}
    out.mkdir(parents=True, exist_ok=False)
    manifest = {"protocol": "termforge-recovery-core-sanity-v1", "status": "running",
                "maximum_attempts": 1, "planned_core_processes": 38, "attempted_core_processes": 0,
                "fixed_floor_seconds": FLOOR, "per_process_timeout_seconds": PROCESS_TIMEOUT,
                "whole_batch_bound_seconds": None, "performance_gate": "not_evaluated",
                "source_sha256": source_hashes, "binary_sha256": binary_hashes, "records": []}

    def save():
        (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")

    def unchanged():
        require(all(digest(path) == binary_hashes[str(path)] for _, path in binaries) and
                all(digest(ROOT / name) == value for name, value in source_hashes.items()),
                "Frozen build or source changed")

    def invoke(path, arguments, wrapper, expected=None):
        unchanged()
        index = len(manifest["records"])
        row = {"index": index, "wrapper": wrapper, "arguments": list(arguments),
               "exit_code": None, "timed_out": False, "status": "failed"}
        manifest["attempted_core_processes"] += 1
        save()
        try:
            result = subprocess.run([str(path), *arguments], stdin=subprocess.DEVNULL,
                                    capture_output=True, timeout=PROCESS_TIMEOUT)
            stdout, stderr = result.stdout, result.stderr
            row["exit_code"] = result.returncode
        except subprocess.TimeoutExpired as error:
            stdout, stderr = error.stdout or b"", error.stderr or b""
            row["timed_out"] = True
        except OSError:
            stdout = stderr = b""
            row["launch_failed"] = True
        for name, data in (("stdout", stdout), ("stderr", stderr)):
            target = out / f"{index:02d}.{name}.bin"
            target.write_bytes(data)
            row[name] = {"file": target.name, "bytes": len(data), "sha256": digest(target)}
        manifest["records"].append(row)
        save()
        unchanged()
        require(not row["timed_out"], "Core sanity timed out; no retry")
        if expected == "rejected":
            require(row["exit_code"] == 2 and not stdout and bool(stderr), "Invalid CLI was not rejected")
        else:
            require(row["exit_code"] == 0 and not stderr, "Core sanity failed")
            value = decode_response(stdout)
            if expected is None:
                validate_identity(value, wrapper)
            else:
                validate_sample(value, wrapper, *expected, FLOOR)
        row["status"] = "passed"
        save()

    save()
    try:
        for wrapper, path in binaries:
            invoke(path, ("--identity",), wrapper)
        for wrapper, path in binaries:
            for route, chunks in CHUNKS.items():
                for chunk in chunks:
                    for observe in (0, 1):
                        invoke(path, (route, str(chunk), str(observe), str(FLOOR)), wrapper,
                               (route, chunk, observe))
            for arguments in INVALID:
                invoke(path, arguments, wrapper, "rejected")
        unchanged()
        require(len(manifest["records"]) == 38, "Fixed sanity process count differs")
        manifest["status"] = "passed"
    except BaseException:
        manifest["status"] = "failed"
        raise
    finally:
        save()
    return manifest


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--wrapped", type=Path, required=True)
    cli.add_argument("--noalloc", type=Path, required=True)
    cli.add_argument("--out", type=Path, required=True)
    args = cli.parse_args()
    result = run_sanity(args.wrapped, args.noalloc, args.out)
    print(json.dumps({"status": result["status"], "core_processes": len(result["records"]),
                      "performance_gate": result["performance_gate"]}))


if __name__ == "__main__":
    main()
