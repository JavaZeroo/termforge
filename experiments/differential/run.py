"""Unattended semantic differential traces, not a performance comparison."""
from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import platform
import random
import subprocess
import sys
import unicodedata

# pyte delegates scalar width to wcwidth; freeze the table selection before import.
os.environ["UNICODE_VERSION"] = "15.0.0"
import pyte
import wcwidth as width_module
from wcwidth import wcwidth

from cases import Case, cases

ROOT = Path(__file__).resolve().parents[2]
VERSIONS = {"pyte": "0.8.2", "wcwidth": "0.2.13"}
COLORS = {"default": -1, **{c: i for i, c in enumerate(
    ("black", "red", "green", "brown", "blue", "magenta", "cyan", "white",
     "brightblack", "brightred", "brightgreen", "brightbrown", "brightblue",
     "brightmagenta", "brightcyan", "brightwhite"))}}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def state_sha(state):
    return hashlib.sha256(json.dumps(state, sort_keys=True, ensure_ascii=False,
                                     separators=(",", ":")).encode("utf-8")).hexdigest()


def difference_signature(case, difference, core, reference):
    return dict(input_sha256=hashlib.sha256(case.data).hexdigest(),
                rows=case.rows, columns=case.columns, history=case.history,
                first_difference={key: value for key, value in difference.items() if key != "checkpoint"},
                final_core_sha256=state_sha(core[-1]["screen"]),
                final_reference_sha256=state_sha(reference[-1]["screen"]))


def check_dependencies():
    actual = {name: importlib.metadata.version(name) for name in VERSIONS}
    if actual != VERSIONS:
        raise RuntimeError(f"reference versions differ: {actual}; install requirements.txt --require-hashes")
    return actual


def trace_binary(value=None):
    value = value or os.environ.get("TERMFORGE_TRACE")
    if value:
        return Path(value).resolve(strict=True)
    name = "terminal_trace.exe" if os.name == "nt" else "terminal_trace"
    dirs = [Path(os.environ["TERMFORGE_BIN_DIR"])] if "TERMFORGE_BIN_DIR" in os.environ else []
    dirs += [ROOT / "build/bin/Release", ROOT / "build/bin"]
    for directory in dirs:
        if (directory / name).is_file():
            return (directory / name).resolve()
    raise FileNotFoundError("build terminal_trace and set --trace or TERMFORGE_TRACE")


def chunks(length, plan):
    if plan == "byte":
        return [1] * length
    if plan == "whole":
        return [length] if length else []
    if not plan.startswith("seed:"):
        raise ValueError("plan must be byte, whole or seed:<integer>")
    rng = random.Random(int(plan.split(":", 1)[1]))
    sizes = []
    while length:
        size = min(length, rng.randint(1, 17))
        sizes.append(size)
        length -= size
    return sizes


def style(char):
    return dict(fg=COLORS[char.fg], bg=COLORS[char.bg], bold=char.bold)


def reference_state(screen):
    def rows(lines):
        result = []
        for line in lines:
            row = []
            for x in range(screen.columns):
                char = line[x]
                row.append(dict(text=unicodedata.normalize("NFC", char.data),
                                width=wcwidth(char.data[0], unicode_version="15.0.0") if char.data else 0,
                                style=style(char)))
            result.append(row)
        return result
    return dict(row=screen.cursor.y, column=min(screen.cursor.x, screen.columns - 1),
                wrap_pending=screen.cursor.x == screen.columns,
                cells=rows([screen.buffer[y] for y in range(screen.lines)]),
                history=rows(screen.history.top), current_style=style(screen.cursor.attrs))


def reference_trace(case, sizes):
    screen = pyte.HistoryScreen(case.columns, case.rows, history=case.history)
    stream = pyte.ByteStream(screen)
    offset = 0
    result = [dict(offset=0, screen=reference_state(screen))]
    for size in sizes:
        stream.feed(case.data[offset:offset + size])
        offset += size
        result.append(dict(offset=offset, screen=reference_state(screen)))
    return result


def canonicalize_core(records):
    # NFC only reconciles equivalent text storage; no width/cursor/attribute differences are hidden.
    for record in records:
        for group in ("cells", "history"):
            for row in record["screen"][group]:
                for cell in row:
                    cell["text"] = unicodedata.normalize("NFC", cell["text"])
    return records


def first_difference(expected, actual, path=""):
    if type(expected) is not type(actual):
        return dict(path=path, expected=expected, actual=actual)
    if isinstance(expected, dict):
        if expected.keys() != actual.keys():
            return dict(path=path + ".keys", expected=sorted(expected), actual=sorted(actual))
        for key in expected:
            difference = first_difference(expected[key], actual[key], f"{path}.{key}")
            if difference:
                return difference
    elif isinstance(expected, list):
        if len(expected) != len(actual):
            return dict(path=path + ".length", expected=len(expected), actual=len(actual))
        for index, (left, right) in enumerate(zip(expected, actual)):
            difference = first_difference(left, right, f"{path}[{index}]")
            if difference:
                return difference
    elif expected != actual:
        return dict(path=path, expected=expected, actual=actual)
    return None


def compare_traces(expected, actual):
    for index, (left, right) in enumerate(zip(expected, actual)):
        difference = first_difference(left, right)
        if difference:
            return dict(checkpoint=index, offset=left["offset"], **difference)
    if len(expected) != len(actual):
        index = min(len(expected), len(actual))
        return dict(checkpoint=index, offset=expected[index]["offset"] if index < len(expected) else None,
                    path="trace.length", expected=len(expected), actual=len(actual))
    return None


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def write_jsonl(path, records):
    path.write_text("".join(json.dumps(record, ensure_ascii=False, separators=(",", ":")) + "\n"
                            for record in records), encoding="utf-8")


def run_case(binary, case, plan, output):
    output.mkdir(parents=True, exist_ok=False)
    sizes = chunks(len(case.data), plan)
    (output / "input.bin").write_bytes(case.data)
    (output / "chunks.txt").write_text("\n".join(map(str, sizes)) + "\n", encoding="ascii")
    command = [str(binary), str(output / "input.bin"), str(case.rows), str(case.columns),
               str(case.history), str(output / "chunks.txt")]
    try:
        process = subprocess.run(command, capture_output=True, timeout=20)
        stdout, stderr, returncode = process.stdout, process.stderr, process.returncode
    except subprocess.TimeoutExpired as error:
        stdout, stderr, returncode = error.stdout or b"", error.stderr or b"", None
    (output / "core.jsonl").write_bytes(stdout)
    (output / "core.stderr.txt").write_bytes(stderr)
    reference = reference_trace(case, sizes)
    write_jsonl(output / "reference.jsonl", reference)
    error = None
    try:
        actual = canonicalize_core([json.loads(line) for line in stdout.decode("utf-8").splitlines()])
        difference = compare_traces(reference, actual)
        if not actual:
            error = "core returned no state checkpoints"
    except (ValueError, KeyError, TypeError) as exc:
        actual, difference, error = [], None, str(exc)
    # Nonzero execution must never be reclassified as an accepted semantic difference.
    if returncode != 0 or error:
        status = "execution_failure"
    elif difference:
        known_path = Path(__file__).with_name("known-differences.json")
        known = json.loads(known_path.read_text(encoding="utf-8")) if known_path.exists() else {}
        signature = difference_signature(case, difference, actual, reference)
        status = ("expected_difference" if case.difference and known.get(case.name) == signature
                  else "unexpected_difference")
    else:
        status = "unexpected_match" if case.difference else "match"
    report = dict(case=case.name, plan=plan, input_sha256=sha(output / "input.bin"),
                  bytes=len(case.data), rows=case.rows, columns=case.columns, history=case.history,
                  chunk_sizes=sizes, checkpoints=len(reference), status=status, returncode=returncode,
                  known_difference=case.difference, first_difference=difference, error=error,
                  normalization="NFC text; pyte cursor.x==columns maps to wrap_pending + last column",
                  reproduce=["terminal_trace", "input.bin", str(case.rows), str(case.columns),
                             str(case.history), "chunks.txt"])
    write_json(output / "report.json", report)
    return report, actual, reference


def run_suite(binary, output, selected=None, plans=None):
    check_dependencies()
    output.mkdir(parents=True, exist_ok=False)
    records = []
    for case in selected or cases():
        byte_core, byte_reference = None, None
        for plan in plans or ["byte", "whole", "seed:42", "seed:20261022", "seed:8675309"]:
            report, core, reference = run_case(binary, case, plan, output / case.name / plan.replace(":", "-"))
            if plan == "byte":
                byte_core, byte_reference = core, reference
            elif byte_core is not None and len(byte_core) == len(case.data) + 1:
                offsets = [item["offset"] for item in reference]
                report["core_fragmentation_difference"] = compare_traces([byte_core[i] for i in offsets], core)
                report["reference_fragmentation_difference"] = compare_traces([byte_reference[i] for i in offsets], reference)
                if report["core_fragmentation_difference"] or report["reference_fragmentation_difference"]:
                    report["status"] = "fragmentation_failure"
                write_json(output / case.name / plan.replace(":", "-") / "report.json", report)
            records.append(report)
    sources = ["experiments/core/trace.cpp", "experiments/core/terminal.hpp", "experiments/core/screen.hpp",
               "experiments/core/unicode_width.hpp", "experiments/differential/run.py",
               "experiments/differential/cases.py", "experiments/differential/requirements.txt",
               "experiments/differential/known-differences.json"]
    summary = dict(schema=1, reference=VERSIONS, unicode_width="15.0.0", python=platform.python_version(),
                   platform=platform.platform(), trace_sha256=sha(binary),
                   reference_source_sha256={"pyte/screens.py": sha(Path(pyte.__file__).with_name("screens.py")),
                                            "pyte/streams.py": sha(Path(pyte.__file__).with_name("streams.py")),
                                            "wcwidth/wcwidth.py": sha(Path(width_module.__file__).with_name("wcwidth.py"))},
                   source_sha256={path: sha(ROOT / path) for path in sources},
                   runs=len(records), checkpoints=sum(record["checkpoints"] for record in records),
                   counts={status: sum(record["status"] == status for record in records)
                           for status in sorted({record["status"] for record in records})},
                   passed=all(record["status"] in ("match", "expected_difference") for record in records),
                   performance_measured=False, records=records)
    write_json(output / "summary.json", summary)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", type=Path)
    parser.add_argument("--out", type=Path, required=True, help="new output directory; never overwritten")
    parser.add_argument("--case", choices=[case.name for case in cases()])
    parser.add_argument("--plan", help="byte, whole, or seed:<integer>; default runs all five plans")
    args = parser.parse_args()
    selected = [case for case in cases() if args.case is None or case.name == args.case]
    result = run_suite(trace_binary(args.trace), args.out.resolve(), selected, [args.plan] if args.plan else None)
    print(json.dumps({key: result[key] for key in ("passed", "runs", "checkpoints", "counts")}, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
