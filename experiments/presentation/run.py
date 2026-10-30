"""Owned HWND + private desktop rendering checks; never capture the user's desktop."""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import random
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from experiments.build_paths import binary


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate(row, backend, dpi):
    """Independently reject contradictory/missing telemetry, even with valid=true."""
    truth = ("valid", "window_verified", "snapshot_isolation_verified", "dirty_equals_full",
             "unchanged_row_preserved", "cursor_verified", "background_color_verified", "resize_verified",
             "foreground_color_verified", "combining_mark_changes_pixels", "missing_glyph_control_detected",
             "owned_window_destroyed", "private_desktop_closed")
    if any(row.get(key) is not True for key in truth):
        raise ValueError("Window correctness gate failed")
    if (row.get("backend") != backend or row.get("logical_dpi") != dpi or
            row.get("desktop") != "private-non-input" or row.get("snapshot_revision") != 2 or
            row.get("dirty_rows") != [1, 2] or row.get("window_dpi", 0) <= 0 or
            row.get("wm_paint_count", 0) < 27 or row.get("wm_printclient_count", 0) < 3):
        raise ValueError("Unexpected window/snapshot identity")
    if ("presented_done" not in row or row["presented_done"] is not None or
            row.get("capture_layer") != "WM_PRINTCLIENT from WM_PAINT retained DIB"):
        raise ValueError("Incorrect presentation/capture endpoint")
    # The fixture now carries one reverse and one underlined cell, so the ink and
    # glyph counts moved from 19 to 25 together with presentation.cpp.
    if (row.get("dwrite_glyph_count") != 25 or row.get("dwrite_missing_glyphs") != 0 or
            row.get("dwrite_overflowing_cells") != 0 or row.get("text_cells_with_ink") != 25 or not row.get("dwrite_resolved_faces")):
        raise ValueError("DirectWrite glyph/fit gate failed")
    audit = row.get("dwrite_text_audit", [])
    if [item.get("text") for item in audit] != ["e\u0301", "中", "文"]:
        raise ValueError("Missing text-specific font observations")
    for item in audit:
        if item.get("glyphs", 0) < 1 or item.get("missing") != 0 or not item.get("faces"):
            raise ValueError("Text glyph gate failed")
    for key, count in (("draw_samples_ms", 24), ("frame_submit_samples_ms", 24), ("timer_pair_samples_ms", 128)):
        samples = row.get(key, [])
        if len(samples) != count or any(not isinstance(x, (float, int)) or not math.isfinite(x) or x < 0 for x in samples):
            raise ValueError("Invalid timing samples")


def desktop_is_gone(pid):
    """Open only the probe's unique desktop name, never enumerate other desktops."""
    api = ctypes.WinDLL("user32", use_last_error=True)
    api.OpenDesktopW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    api.OpenDesktopW.restype = wintypes.HANDLE
    api.CloseDesktop.argtypes = [wintypes.HANDLE]
    handle = api.OpenDesktopW(f"TermForgeProbe-{pid}", 0, False, 0x0001)  # DESKTOP_READOBJECTS
    if handle:
        api.CloseDesktop(handle)
        return False
    error = ctypes.get_last_error()
    if error not in (2, 3):
        raise OSError(error, "Could not verify owned desktop cleanup")
    return True


def execute(exe, backend, dpi, out, timeout=30, expect_timeout=False):
    if os.name != "nt":
        raise RuntimeError("This probe requires Windows")
    out.mkdir(parents=True, exist_ok=False)
    bmp = out / "frame.bmp"
    command = [str(exe.resolve()), backend, str(dpi), str(bmp.resolve())]
    if expect_timeout:
        command.append("--wait-after-paint")
    row = dict(backend=backend, logical_dpi=dpi, status="failed", expected_timeout=expect_timeout)
    process = None
    try:
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            stdout, stderr = process.communicate(timeout=timeout)
            row["timed_out"] = False
        except subprocess.TimeoutExpired:
            row["timed_out"] = True
            process.kill()  # The native probe never creates child processes.
            stdout, stderr = process.communicate(timeout=10)
        row.update(exit_code=process.returncode, stdout=stdout.decode("utf-8", "replace"), stderr=stderr.decode("utf-8", "replace"))
        row["owned_process_exited"] = process.poll() is not None
        deadline = time.monotonic() + 2
        while not desktop_is_gone(process.pid):
            if time.monotonic() > deadline:
                raise RuntimeError("Owned desktop remained after probe process exit")
            time.sleep(0.01)
        row["owned_desktop_gone"] = True
        if not expect_timeout and (row["timed_out"] or row["exit_code"]):
            raise RuntimeError("Window probe failed or timed out: " + row["stderr"].strip())
        result = json.loads(row["stdout"])
        if expect_timeout:
            if not row["timed_out"] or result != {"timeout_probe_ready": True, "window_painted": True}:
                raise ValueError("Timeout did not happen after a real window paint")
        else:
            if row["timed_out"] or row["exit_code"]:
                raise RuntimeError("Window probe failed or timed out")
            validate(result, backend, dpi)
            row.update(result=result, images={path.name: sha(path) for path in sorted(out.glob("*.bmp"))})
            if not bmp.is_file() or not Path(str(bmp) + ".initial.bmp").is_file():
                raise RuntimeError("Missing captured frames")
        row["status"] = "passed"
        return row
    except BaseException as error:
        row["error"] = str(error)
        raise
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            process.communicate(timeout=10)
        (out / "sample.json").write_text(json.dumps(row, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--bin-dir", type=Path)
    cli.add_argument("--out", required=True, type=Path)
    cli.add_argument("--repeats", type=int, default=2)
    args = cli.parse_args()
    if not 1 <= args.repeats <= 10:
        cli.error("--repeats must be 1..10")
    exe = binary("presentation_probe", args.bin_dir)
    args.out.mkdir(parents=True, exist_ok=False)
    metadata = dict(schema="termforge-window-probe-v1", status="running", started_utc=datetime.now(timezone.utc).isoformat(),
                    platform=platform.platform(), python=platform.python_version(), exe_sha256=sha(exe),
                    source_sha256={p.name: sha(p) for suffix in ("*.cpp", "*.hpp") for p in Path(__file__).parent.glob(suffix)},
                    runner_sha256=sha(Path(__file__)),
                    core_sources={name: sha(ROOT / "experiments/core" / name) for name in ("terminal.hpp", "screen.hpp", "unicode_width.hpp")},
                    font_family_requested="Consolas", seed=20261023, repeats=args.repeats,
                    capture_layer="WM_PRINTCLIENT from WM_PAINT retained DIB", presented_done=None,
                    physical_dpi_transition_verified=False, performance_ranking=False,
                    limitations=["private non-input desktop", "programmatic UpdateWindow paint trigger",
                                 "GDI font fallback unobserved; its DirectWrite audit is a separate reference",
                                 "HWND resize only, no terminal resize/reflow", "bounded fixed-scene layout cache",
                                 "instrumented draw/BitBlt durations, no observer-off calibration"])
    rows = []
    rng = random.Random(metadata["seed"])
    try:
        for repeat in range(args.repeats):
            configurations = [(backend, dpi) for backend in ("gdi", "dwrite") for dpi in (96, 144)]
            rng.shuffle(configurations)
            for backend, dpi in configurations:
                result = execute(exe, backend, dpi, args.out / f"{repeat}-{backend}-{dpi}")
                rows.append(dict(repeat=repeat, **result))
                print(f"{repeat} {backend} {dpi}: passed", flush=True)
        timeout = execute(exe, "gdi", 96, args.out / "timeout", timeout=3, expect_timeout=True)
        metadata["timeout_cleanup_passed"] = timeout["status"] == "passed"
        summary = []
        for backend in ("gdi", "dwrite"):
            for dpi in (96, 144):
                values = [row["result"] for row in rows if row["backend"] == backend and row["logical_dpi"] == dpi]
                item = dict(backend=backend, logical_dpi=dpi)
                for metric in ("draw_samples_ms", "frame_submit_samples_ms", "timer_pair_samples_ms"):
                    data = sorted(x for value in values for x in value[metric])
                    item[metric] = dict(count=len(data), min=min(data), median=statistics.median(data),
                                        p95=data[math.ceil(len(data)*.95)-1], max=max(data))
                summary.append(item)
        (args.out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
        metadata["status"] = "complete"
    except Exception as error:
        metadata.update(status="failed", error=str(error))
        raise
    finally:
        metadata["finished_utc"] = datetime.now(timezone.utc).isoformat()
        (args.out / "samples.jsonl").write_text("".join(json.dumps(row, ensure_ascii=False) + "\n" for row in rows), encoding="utf-8")
        (args.out / "manifest.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
