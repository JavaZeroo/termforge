"""Check independent expected screen/history through replay and real bounded pipe paths."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from experiments.build_paths import binary


def digest(data):
    return hashlib.sha256(data).hexdigest()


def state_digest(state):
    return digest(json.dumps(state, sort_keys=True, separators=(",", ":")).encode())


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--out", required=True, type=Path)
    cli.add_argument("--fixture", choices=("ascii", "unicode"), default="ascii")
    cli.add_argument("--bin-dir", type=Path, help="CMake runtime directory; overrides TERMFORGE_BIN_DIR")
    cli.add_argument("--route", choices=("auto", "replay", "all"), default="auto",
                     help="auto includes Windows pipes on Windows, replay elsewhere")
    args = cli.parse_args()
    use_pipe = args.route == "all" or (args.route == "auto" and os.name == "nt")
    if use_pipe and os.name != "nt":
        cli.error("the anonymous pipe experiment requires Windows; use --route replay")
    binaries = {name: binary(name, args.bin_dir) for name in (("replay", "receive") if use_pipe else ("replay",))}
    args.out.mkdir(parents=True, exist_ok=False)
    # Each record overwrites and erases throwaway text, then leaves a numbered line.
    # Expected state is derived from record numbers, not another escape interpreter.
    count = 20000
    payload = b"".join(f"junk{i:06d}\rX\x1b[2K\rline{i:06d}\r\n".encode() for i in range(count))
    if args.fixture == "unicode":
        payload = b"".join(f"junk{i:06d}\rX\x1b[2K\r\x1b[31m中\x1b[0me\u0301{i:06d}\r\n".encode() for i in range(count))
    path = args.out / "input.bin"
    path.write_bytes(payload)
    line = lambda i: f"line{i:06d}".ljust(80)
    if args.fixture == "unicode":
        line = lambda i: f"中e\u0301{i:06d}" + " " * 71
    expected = dict(row=23, column=0, wrap_pending=False,
                    lines=[line(i) for i in range(count - 23, count)] + [" " * 80],
                    history=[line(i) for i in range(count - 123, count - 23)],
                    discarded_history_lines=count - 123)
    if args.fixture == "unicode":
        def details(rows):
            result = []
            for row in range(rows):
                for column, text, width, fg in [(0, "中", 2, 1), (1, "", 0, 1), (2, "e\u0301", 1, -1)]:
                    result.append(dict(row=row, column=column, text=text, width=width,
                                       style=dict(fg=fg, bg=-1, bold=False)))
            return result
        expected.update(cell_details=details(23), history_cell_details=details(100))
    (args.out / "expected.json").write_text(json.dumps(expected, indent=2) + "\n")
    sources = ["experiments/core/terminal.hpp", "experiments/core/screen.hpp", "experiments/core/unicode_width.hpp", "experiments/core/replay.cpp",
               "experiments/core/run.py", "experiments/build_paths.py", "experiments/receive/receive.cpp"]
    meta = dict(schema_version=2, fixture=args.fixture, unicode_version="15.0.0", status="running", platform=platform.platform(),
                python=platform.python_version(), input_bytes=len(payload), input_sha256=digest(payload),
                expected_sha256=state_digest(expected),
                sources={p: digest((ROOT / p).read_bytes()) for p in sources},
                routes=["replay", "pipe"] if use_pipe else ["replay"],
                binaries={path.name: digest(path.read_bytes()) for path in binaries.values()})
    manifest = args.out / "manifest.json"
    manifest.write_text(json.dumps(meta, indent=2) + "\n")
    completed = 0
    try:
        with (args.out / "samples.jsonl").open("w") as output:
            configurations = [("replay", chunk, None, None) for chunk in (1, 7, 512, 4096, 65536)]
            if use_pipe:
                configurations += [("pipe", chunk, policy, mode) for chunk in (512, 4096, 65536)
                                   for policy in ("each", "edge") for mode in ("stream", "yield")]
            for route, chunk, policy, mode in configurations:
                if route == "replay":
                    command = [str(binaries["replay"]), str(path.resolve()), "24", "80", "100", str(chunk)]
                else:
                    command = [str(binaries["receive"]), str(chunk), policy, mode, str(len(payload)), str(chunk), str(path.resolve())]
                record = dict(route=route, chunk=chunk, policy=policy, mode=mode)
                try:
                    result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", timeout=30)
                    record.update(exit_code=result.returncode, stderr=result.stderr)
                    data = json.loads(result.stdout)
                    screen = data.pop("screen", None)
                    record.update(result=data, screen_sha256=state_digest(screen), screen_matches=screen == expected)
                    if result.returncode != 0 or screen != expected:
                        raise RuntimeError("execution or independently expected screen mismatch")
                    if route == "pipe" and (not data["valid"] or data["consumed"] != len(payload) or data["peak_queued_bytes"] > chunk):
                        raise RuntimeError("pipe integrity failure")
                    completed += 1
                except Exception as error:
                    record["error"] = str(error)
                    raise
                finally:
                    output.write(json.dumps(record) + "\n"); output.flush()
            meta.update(status="complete", checked_runs=completed)
    except Exception as error:
        meta.update(status="failed", checked_runs=completed, error=str(error))
        raise
    finally:
        manifest.write_text(json.dumps(meta, indent=2) + "\n")
    print(f"Verified {completed} runs against independent screen/history expectations")


if __name__ == "__main__":
    main()
