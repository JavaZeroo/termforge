"""Bounded synthetic native resize comparison for the explicit reviewed backend."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import subprocess

from experiments.conpty.margin_run import PACKAGE, backend_path
from experiments.conpty.run import OwnedJob


# Literal native observations, independent of candidate reflow. Each row is
# padded to the acknowledged native width before exact comparison.
CASES = (
    ((8, 3), (8, 6), "H0\r\nH1\r\nTOP\r\nMID\r\nBOT", ("TOP", "MID", "BOT", "", "", ""), (2, 3)),
    ((8, 3), (16, 3), "H0\r\nH1\r\n123456789\r\nP>", ("123456789", "P>", ""), (1, 2)),
    ((8, 3), (16, 3), "H0\r\nH1\r\nH2\r\n123456789", ("H2", "123456789", ""), (1, 9)),
    ((8, 3), (5, 3), "H0\r\n123456789012345678901234567890", ("90123", "45678", "90"), (2, 2)),
    ((8, 4), (10, 4), "H0\r\nH1\r\nABCDEFGHIJKLMNO\r\nBOTTOM\x1b[3;2H",
     ("H1", "ABCDEFGHIJ", "KLMNO", "BOTTOM"), (1, 9)),
    ((8, 3), (16, 3), "H0\r\n123456789012345678901234567890", ("9012345678901234", "567890", ""), (1, 6)),
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def capture(exe, backend, out, index):
    # The native parent creates this fresh case directory only after GO.
    job = OwnedJob()
    process = None
    assigned = False
    stdout = stderr = b""
    report = dict(case=index, exit_code=None, timed_out=False, forced_outer_cleanup=False,
                  job_after=None, cleanup_errors=[])
    try:
        process = subprocess.Popen([str(exe), "--parent", str(out), str(index), str(backend)],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        job.assign(process.pid)
        assigned = True
        try:
            stdout, stderr = process.communicate(b"GO\n", timeout=20)
        except subprocess.TimeoutExpired:
            report["timed_out"] = True
    finally:
        try:
            if process is not None and process.poll() is None:
                report["forced_outer_cleanup"] = True
                try:
                    if assigned:
                        job.terminate()
                    else:
                        process.kill()  # This root is still gated; no child exists.
                    stdout, stderr = process.communicate(timeout=5)
                except Exception:
                    report["cleanup_errors"].append("process_cleanup")
            if assigned:
                try:
                    report["job_after"] = job.drain(timeout=5)
                    if report["job_after"]["active"]:
                        report["forced_outer_cleanup"] = True
                        job.terminate()
                except Exception:
                    report["cleanup_errors"].append("job_cleanup")
        finally:
            job.close()  # Kill-on-close remains effective when a cleanup operation fails.
        report["exit_code"] = process.poll() if process is not None else None
        out.mkdir(exist_ok=True)
        (out / "parent.stdout.bin").write_bytes(stdout)
        (out / "parent.stderr.bin").write_bytes(stderr)
        write_json(out / "capture.json", report)
    if (report["exit_code"] != 0 or report["timed_out"] or report["forced_outer_cleanup"] or
            report["cleanup_errors"] or report["job_after"] != {"active": 0, "total": 4, "terminated": 0}):
        raise ValueError("native_capture_failed")
    native = json.loads(stdout)
    if (native["case"] != index or native["child_exit"] != 37 or native["active_children"] != 0 or
            native["output_eof"] is not True or native["creation_flags"] != 24):
        raise ValueError("native_lifecycle_failed")
    report["native"] = native
    return report


def inspect(out, replay, index):
    initial, final, source, expected_rows, cursor = CASES[index]
    if (out / "source.bin").read_bytes() != ("\x1b[2J\x1b[H" + source).encode("ascii"):
        raise ValueError("source_changed")
    for stage, dimensions in (("before", initial), ("after", final)):
        native = json.loads((out / f"native-{stage}.json").read_text())
        columns, rows = dimensions
        values = native["utf16_rows"]
        if (native["columns"] != columns or native["rows"] != rows or len(values) != rows or
                any(len(row) != columns or any(type(c) is not int or not 0 <= c < 128 for c in row)
                    for row in values)):
            raise ValueError("native_snapshot_shape")
    expected = [row.ljust(final[0]) for row in expected_rows]
    if ["".join(map(chr, row)) for row in native["utf16_rows"]] != expected or native["cursor"] != list(cursor):
        raise ValueError("native_observation_changed")
    results = {}
    for policy, options in (("reflow_history", []), ("native_viewport", ["--native-viewport"])):
        run = subprocess.run([str(replay), str(out / "transport.bin"), str(out / "plan.txt"), *options],
                             capture_output=True, timeout=5)
        (out / f"{policy}.states.jsonl").write_bytes(run.stdout)
        (out / f"{policy}.stderr.bin").write_bytes(run.stderr)
        if run.returncode != 0:
            raise ValueError("candidate_replay_failed")
        state = json.loads(run.stdout.splitlines()[-1])["state"]
        results[policy] = dict(native_rows_equal=state["lines"] == expected,
                               native_cursor_equal=[state["row"], state["column"]] == list(cursor))
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--replay", type=Path, required=True)
    parser.add_argument("--backend", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    exe, replay = args.exe.resolve(strict=True), args.replay.resolve(strict=True)
    backend = Path(backend_path(str(args.backend / "conpty.dll"))).parent
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    report = dict(schema=1, started_utc=datetime.now(timezone.utc).isoformat(),
                  windows=platform.version(), backend=PACKAGE, creation_flags=24,
                  probe_sha256=sha(exe), replay_sha256=sha(replay),
                  sources_sha256={name: sha(Path(__file__).with_name(name)) for name in
                                  ("resize_native_run.py", "resize_native_probe.cpp", "session.hpp", "backend.hpp")},
                  cpr_replies_sent=False, diagnostic_only=True, presented_done=None, cases=[])
    success = True
    for index in range(len(CASES)):
        case = out / f"case-{index}"
        row = dict(case=index, passed=False)
        try:
            row.update(capture(exe, backend, case, index))
            row["replay"] = inspect(case, replay, index)
            row["passed"] = all(row["replay"]["native_viewport"].values())
        except Exception:
            # The owned case retains raw diagnostics; the summary contains no
            # exception messages or absolute command paths for publication.
            row["failure"] = "capture_or_comparison_failed"
        row["files_sha256"] = {p.name: sha(p) for p in case.iterdir() if p.is_file()} if case.exists() else {}
        report["cases"].append(row)
        write_json(out / "validation.json", report)
        print(json.dumps({"case": index, "passed": row["passed"], "replay": row.get("replay")}), flush=True)
        if not row["passed"]:
            success = False
            break
    return 0 if success else 1


if __name__ == "__main__":
    raise SystemExit(main())
