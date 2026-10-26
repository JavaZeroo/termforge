"""ConPTY margin information-loss diagnostic; never a full R2 acceptance gate."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess

from experiments.build_paths import binary
from experiments.candidates.alacritty.run import run_trace
from experiments.conpty.run import OwnedJob
from experiments.shell.workflow_checks import validate_console_window

SOURCE = (b"\x1b[2J\x1b[HTOP_FIXED\r\nREGION_A\r\nREGION_B\r\nBOTTOM_FIXED\r\n\x1b[2;3r"
          b"\x1b[?1049hALT_OUTSIDE\r\n\x1b[4;1HALT_A\r\nALT_B\x1b[4;5r\x1b[5;1H\nALT_C\nALT_D"
          b"\x1b[?1049l\x1b[3;1H\nREGION_C\nREGION_D\x1b[6;1HTF_MARGIN_DONE\r\n")
SOURCE_SHA256 = "01878a5d54c343007309abb89faeb90c89262a8b55e420c5f267342ed9b27e59"
PACKAGE = {
    "name": "Microsoft.Windows.Console.ConPTY", "version": "1.25.260930003",
    "url": "https://github.com/microsoft/terminal/releases/download/v1.25.2733.0/"
           "Microsoft.Windows.Console.ConPTY.1.25.260930003.nupkg",
    "bytes": 1750533,
    "sha256": "6d6f8b008c655d814c498d8205d4012dcd4d0b6dfc1096461d0a185d1e3caf03",
    "files": {
        "conpty.dll": "feeef341d891643c62d30b6b07800bc70f0bc148f44cb8c3bee84aa557ae805a",
        "OpenConsole.exe": "3d66b23d0a71bb8eed2b77edc8b9df9bf54ce6c8fb74c863a30e760997f80586",
    },
}


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def backend_path(value):
    if value == "system":
        return value
    path = Path(value).resolve(strict=True)
    if path.name != "conpty.dll":
        raise ValueError("expected conpty.dll")
    # Only the reviewed, matching x64 DLL/EXE pair is accepted. The tool does
    # not download/install a backend or silently fall back to the system host.
    for name, expected in PACKAGE["files"].items():
        if sha256(path.parent / name) != expected:
            raise ValueError("fixed backend identity mismatch")
    return str(path)


def validate_lifecycle(native, side_by_side=False):
    expected = {
        "waited": True, "exit_code": 37, "read_error": 109,
        "capture_overflow": False, "forced_reader_cancel": False,
        "forced_process_cancel": False, "cleanup_complete": True,
        "active_children_after": 0, "side_by_side": side_by_side, "creation_flags": 0,
    }
    if (type(native) is not dict or native.keys() != expected.keys() or
            any(type(native[key]) is not type(value) or native[key] != value
                for key, value in expected.items())):
        raise ValueError("native lifecycle incomplete")


def margin_result(lines, columns, rows):
    # This fixture's CR/LF framing and exact ASCII result are independent of
    # the host's native CHAR_INFO. Width 12 wraps the fixed completion marker.
    expected = ["TOP_FIXED", "REGION_C", "REGION_D", "BOTTOM_FIXED", ""]
    marker = "TF_MARGIN_DONE"
    expected += [marker[at:at + columns] for at in range(0, len(marker), columns)]
    expected += [""] * (rows - len(expected))
    return lines == expected


def inspect_case(out, trace, mode, columns, rows):
    source = (out / "source.bin").read_bytes()
    transport = (out / "transport.bin").read_bytes()
    if source != SOURCE or hashlib.sha256(source).hexdigest() != SOURCE_SHA256:
        raise ValueError("original source changed")
    if not 0 < len(transport) <= 8192:
        raise ValueError("transport bounds")
    stages = {stage: validate_console_window(out, expected_role=stage,
                                           expected_generation=1, prefix=stage)
              for stage in ("before", "alt", "restored", "final")}
    report, records = run_trace(trace, transport, rows, columns, 100,
                                [len(transport)], out / "replay")
    if not report["passed"]:
        raise ValueError("candidate replay failed")
    final = records[-1]["screen"]
    lines = ["".join(cell["text"] for cell in row if cell.get("width", 1)).rstrip()
             for row in final["cells"]]
    return {
        "mode": mode, "columns": columns, "rows": rows,
        "source_bytes": len(source), "source_sha256": SOURCE_SHA256,
        "transport_bytes": len(transport), "transport_sha256": hashlib.sha256(transport).hexdigest(),
        "original_decstbm_forwarded": all(vt in transport for vt in (b"\x1b[2;3r", b"\x1b[4;5r")),
        "source_equal_transport": source == transport,
        "native_lines": {stage: data["lines"] for stage, data in stages.items()},
        "native_cursor": {stage: [data["cursor_row"], data["cursor_column"]]
                          for stage, data in stages.items()},
        "native_restore_equal_before": stages["restored"]["cells"] == stages["before"]["cells"],
        "native_main_margins_restored": margin_result(stages["final"]["lines"], columns, rows),
        "terminal_lines": lines,
        "terminal_main_margins_restored": margin_result(lines, columns, rows),
        "terminal_alternate": bool(records[-1]["mode_bits"] & (1 << 12)),
        "replay_passed": report["passed"], "replay_core_sha256": sha256(trace),
        "product_backend_changed": False, "full_r2_contract_verified": False,
        "presented_done": None,
    }


def execute(executable, backend, trace, out, mode, columns, rows, *, gate=b"GO\n", timeout=15):
    if mode not in ("vt-on", "vt-off") or (columns, rows) not in ((80,24), (12,7)):
        raise ValueError("fixed workload required")
    out.mkdir(parents=True, exist_ok=False)
    job, process = OwnedJob(), None
    result = {"status": "failed", "outer_job_after": None, "forced_outer_cleanup": False}
    try:
        process = subprocess.Popen([str(executable), backend, mode, str(out), str(columns), str(rows)],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        job.assign(process.pid)
        stdout, stderr = process.communicate(gate, timeout=timeout)
        (out / "host.stdout.bin").write_bytes(stdout)
        (out / "host.stderr.bin").write_bytes(stderr)
        if process.returncode != 0:
            raise ValueError("native probe failed")
        native = json.loads(stdout)
        validate_lifecycle(native, side_by_side=backend != "system")
        result.update(inspect_case(out, trace, mode, columns, rows), native=native, status="completed")
    finally:
        if process is not None and process.poll() is None:
            result["forced_outer_cleanup"] = True
            job.terminate()
            stdout, stderr = process.communicate(timeout=5)
            (out / "host.stdout.bin").write_bytes(stdout)
            (out / "host.stderr.bin").write_bytes(stderr)
        try:
            result["outer_job_after"] = job.drain()
            if result["outer_job_after"]["active"]:
                result["forced_outer_cleanup"] = True
                job.terminate()
                result["outer_job_after"] = job.drain()
        finally:
            job.close()
            (out / "diagnostic.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    if result["forced_outer_cleanup"] or result["outer_job_after"]["active"]:
        raise ValueError("outer cleanup incomplete")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=None)
    parser.add_argument("--backend", default="system")
    parser.add_argument("--candidate-trace", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--require-margins", action="store_true")
    args = parser.parse_args()
    executable = (args.binary or binary("conpty_margin_probe")).resolve(strict=True)
    backend = backend_path(args.backend)
    trace = args.candidate_trace.resolve(strict=True)
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    samples = [execute(executable, backend, trace, out / "_private" / f"{columns}x{rows}-{mode}",
                       mode, columns, rows)
               for columns, rows in ((80,24), (12,7)) for mode in ("vt-on", "vt-off")]
    valid = all(sample["terminal_main_margins_restored"] for sample in samples if sample["mode"] == "vt-on")
    summary = {
        "schema": "termforge-conpty-margin-backend-v2", "generated_utc": datetime.now(timezone.utc).isoformat(),
        "backend": "system" if backend == "system" else PACKAGE,
        "probe_sha256": sha256(executable), "candidate_trace_sha256": sha256(trace),
        "terminal_main_margins_restored_in_both_sizes": valid, "full_r2_contract_verified": False,
        "samples": samples,
    }
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in summary.items() if key != "samples"}))
    return int(args.require_margins and not valid)


if __name__ == "__main__":
    raise SystemExit(main())
