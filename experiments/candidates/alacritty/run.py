"""Build-independent headless checks for the fixed Alacritty candidate."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
VERSION = "0.26.1-dev+termforge.4"
UPSTREAM_COMMIT = "542147067dd016fc5f49120ef0ba605a98a4e3a7"
BASE_CRATE_SHA256 = "bda177466b9524d59f1b12f0dd30b68696788e9992a7e959021c4a0ed96fcf59"


def vendor_sources():
    return sorted(path.relative_to(ROOT).as_posix() for path in (HERE / "vendor").rglob("*") if path.is_file())


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def trace_binary(value=None):
    value = value or os.environ.get("TERMFORGE_ALACRITTY_TRACE")
    if value:
        return Path(value).resolve(strict=True)
    name = "termforge-alacritty-trace" + (".exe" if os.name == "nt" else "")
    return (HERE / "target" / "release" / name).resolve(strict=True)


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def run_trace(binary, data, rows, columns, history, sizes, output):
    output.mkdir(parents=True, exist_ok=False)
    (output / "input.bin").write_bytes(data)
    (output / "chunks.txt").write_text("".join(f"{size}\n" for size in sizes), encoding="ascii")
    command = [str(binary), str(output / "input.bin"), str(rows), str(columns), str(history),
               str(output / "chunks.txt")]
    try:
        process = subprocess.run(command, capture_output=True, timeout=20)
        stdout, stderr, returncode = process.stdout, process.stderr, process.returncode
    except subprocess.TimeoutExpired as error:
        stdout, stderr, returncode = error.stdout or b"", error.stderr or b"", None
    (output / "candidate.jsonl").write_bytes(stdout)
    (output / "candidate.stderr.txt").write_bytes(stderr)
    records, error = [], None
    if returncode == 0:
        try:
            records = [json.loads(line) for line in stdout.decode("utf-8").splitlines()]
            if not records or records[-1]["offset"] != len(data):
                raise ValueError("missing final checkpoint")
        except (ValueError, KeyError, TypeError) as exc:
            error = str(exc)
    report = dict(bytes=len(data), rows=rows, columns=columns, history=history, chunk_sizes=sizes,
                  input_sha256=hashlib.sha256(data).hexdigest(), returncode=returncode, error=error,
                  checkpoints=len(records), passed=returncode == 0 and error is None)
    write_json(output / "report.json", report)
    return report, records


def row_text(row):
    return "".join(cell["text"] for cell in row)


def run_suite(binary, output):
    output.mkdir(parents=True, exist_ok=False)
    cases = []

    data = "A中e\u0301😀\r\nB\r\nC\r\nD".encode("utf-8")
    whole_report, whole = run_trace(binary, data, 3, 8, 2, [len(data)], output / "fragmentation-whole")
    byte_report, byte = run_trace(binary, data, 3, 8, 2, [1] * len(data), output / "fragmentation-byte")
    fragmentation_passed = (whole_report["passed"] and byte_report["passed"] and
                            whole[-1]["screen"] == byte[-1]["screen"])
    cases.append(dict(case="ascii_unicode_fragmentation", passed=fragmentation_passed,
                      whole=whole_report, byte=byte_report))

    data = b"A\r\nB\r\nC\r\nD\x1b[?1049hALT\x1b[6n\x1b[?1049lZ"
    report, records = run_trace(binary, data, 3, 8, 2, [1] * len(data), output / "alternate-query")
    replies = [reply for record in records for reply in record["replies"]]
    final = records[-1]
    passed = (report["passed"] and replies == ["\x1b[3;5R"] and
              [row_text(row) for row in final["screen"]["cells"]] == ["B", "C", "DZ"] and
              [row_text(row) for row in final["screen"]["history"]] == ["A"] and
              not final["mode_bits"] & (1 << 12))
    cases.append(dict(case="alternate_query_history", passed=passed, replies=replies, report=report))

    data = b"A\r\nB\r\nC\r\nD\r\nE"
    report, records = run_trace(binary, data, 3, 8, 2, [len(data)], output / "bounded-history")
    final = records[-1]
    passed = (report["passed"] and
              [row_text(row) for row in final["screen"]["cells"]] == ["C", "D", "E"] and
              [row_text(row) for row in final["screen"]["history"]] == ["A", "B"])
    cases.append(dict(case="bounded_history", passed=passed, report=report))

    sources = ["Cargo.toml", "Cargo.lock", "src/main.rs", "src/ffi.rs",
               "include/termforge_candidate.h", "run.py"]
    source_sha256 = {path: sha(HERE / path) for path in sources}
    adapter_sources = ["experiments/core/candidate_terminal.cpp", "experiments/core/candidate_terminal.hpp",
                       "experiments/core/candidate_terminal_test.cpp"]
    source_sha256.update({path: sha(ROOT / path) for path in vendor_sources() + adapter_sources})
    summary = dict(schema=1, candidate="alacritty_terminal", version=VERSION,
                   license="Apache-2.0", upstream_commit=UPSTREAM_COMMIT,
                   base_crate_sha256=BASE_CRATE_SHA256, platform=platform.platform(),
                   trace_sha256=sha(binary), source_sha256=source_sha256,
                   cases=cases, passed=all(case["passed"] for case in cases),
                   performance_measured=False,
                   limitations=["Only default/ANSI-16 colors map exactly; other colors use -2",
                                "Space and empty cells share the upstream is_empty projection",
                                "Headless adapter evidence is not product TUI or platform acceptance"])
    write_json(output / "summary.json", summary)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", type=Path)
    parser.add_argument("--out", type=Path, required=True, help="new output directory; never overwritten")
    args = parser.parse_args()
    summary = run_suite(trace_binary(args.trace), args.out.resolve())
    print(json.dumps({"passed": summary["passed"], "cases": len(summary["cases"])}, indent=2))
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
