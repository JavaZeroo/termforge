"""Real cmd.exe integration on an owned non-input desktop; no user interactions."""
import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from experiments.build_paths import binary
from experiments.conpty.run import OwnedJob


def validate(row, mode):
    if mode == "ui-dpi":
        if (row.get("valid") is not True or row.get("mode") != mode or
                row.get("dpi_and_zoom_verified") is not True or
                type(row.get("sessions_closed")) is not int or row["sessions_closed"] != 3 or
                type(row.get("active_children_after")) is not int or row["active_children_after"] != 0 or
                row.get("presented_done", 1) is not None):
            raise ValueError("workspace DPI, zoom or cleanup gate failed")
        return
    if mode == "ui-main":
        if (row.get("valid") is not True or row.get("mode") != mode or
                any(row.get(field) is not True for field in
                    ("main_sessions_verified", "focus_verified", "geometry_verified")) or
                type(row.get("sessions_closed")) is not int or row["sessions_closed"] != 5 or
                row.get("active_children_after") != 0 or row.get("presented_done", 1) is not None or
                row.get("ui_font_family") != "Roboto Mono"):
            raise ValueError("main workspace session, focus, font or cleanup gate failed")
        return
    if mode in ("ui-edit", "ui-restore"):
        if (row.get("valid") is not True or row.get("mode") != mode or
                row.get("interactions_verified") is not True or row.get("sessions_closed") != 1 or
                row.get("active_children_after") != 0 or row.get("presented_done", 1) is not None):
            raise ValueError("workspace edit/restore gate failed")
        return
    if mode == "ui":
        if (row.get("valid") is not True or row.get("isolation_verified") is not True or
                row.get("resize_verified") is not True or row.get("sessions_closed") != 3 or
                row.get("active_children_after") != 0 or row.get("presented_done", 1) is not None):
            raise ValueError("workspace isolation, resize or cleanup gate failed")
        return
    for field in ("valid", "cleanup_complete", "command_verified", "unicode_verified", "backspace_verified", "arrows_verified"):
        if row.get(field) is not True:
            raise ValueError(f"failed shell gate: {field}")
    if row["active_children_after"] != 0 or row["input_rejected"] != 0:
        raise ValueError("leftover child or rejected input")
    if not 0 < row["output_queue_high_water"] <= 256 * 1024 or not 0 < row["input_queue_high_water"] <= 16 * 1024:
        raise ValueError("queue budget violated")
    if row["frames"] < 2 or row["consumed_bytes"] <= 65536 or row["presented_done"] is not None:
        raise ValueError("missing output/frame or invented endpoint")
    if not 0 < row["max_consumed_per_tick"] <= 65536:
        raise ValueError("UI byte budget violated")
    if mode in ("resize", "resize-close"):
        if row.get("resize_verified") != (3 if mode == "resize" else 2) or (row.get("columns"), row.get("rows")) != (72, 18):
            raise ValueError("resize dimensions were not independently verified")
        if len(row["screen"]["lines"]) != 18:
            raise ValueError("resized screen height mismatch")
    if mode in ("normal", "resize"):
        if row["exit_code"] != 37 or not row["ctrl_c_verified"]:
            raise ValueError("shell interruption or normal exit not verified")
        if not 0 <= row["scripted_input_to_state_ms"] <= row["scripted_input_to_submit_ms"]:
            raise ValueError("incorrect state/submission timing")
    elif row["scripted_input_to_state_ms"] is not None or row["scripted_input_to_submit_ms"] is not None:
        raise ValueError("unobserved input endpoints must be null")


def validate_main_geometry(data):
    """Independent 96-unit reference, measured before this implementation.

    Compare both workspace layout and actual HWND placement. This does not
    establish inner text-grid, pixel, physical-DPI or performance equivalence.
    """
    phases = ("empty-1360", "session-1360", "session-2048")
    if (not isinstance(data, dict) or data.get("ui_font_family") != "Roboto Mono" or
            not isinstance(data.get("snapshots"), list) or
            [row.get("phase") for row in data["snapshots"] if isinstance(row, dict)] != list(phases) or
            len(data["snapshots"]) != 3):
        raise ValueError("missing main workspace geometry phases or font identity")
    expected = (
        ([1360, 860], [227, 33, 906, 19], [227, 52, 906, 36], None),
        ([1360, 860], [227, 33, 906, 26], [227, 59, 906, 34], [227, 93, 906, 453]),
        ([2048, 1120], [342, 33, 1364, 26], [342, 59, 1364, 34], [342, 93, 1364, 626]),
    )
    for row, (window, group, toolbar, terminal) in zip(data["snapshots"], expected):
        fields = {"window": window, "main_group": group, "main_child": terminal, "main_toolbar": toolbar}
        if terminal is not None:
            fields.update(main_terminal=terminal)
        for field, wanted in fields.items():
            value = row.get(field)
            if value != wanted or (wanted is not None and
                                   (not isinstance(value, list) or any(type(n) is not int for n in value))):
                raise ValueError(f"main geometry mismatch: {row['phase']} {field}")


def validate_dpi_geometry(data):
    """Check physical HWND and cell measurements against the reference layout.

    OS DPI is recorded separately: these controlled messages do not move the
    window between monitors or establish physical display presentation.
    """
    phases = ("96-baseline", "144-renaming", "192-drag-cancelled", "96-restored")
    if (not isinstance(data, dict) or data.get("schema") != "termforge-ui-dpi-v1" or
            data.get("controlled_messages") is not True or data.get("real_monitor_transition") is not False or
            not isinstance(data.get("snapshots"), list) or len(data["snapshots"]) != 4 or
            any(not isinstance(row, dict) for row in data["snapshots"]) or
            [row.get("phase") for row in data["snapshots"]] != list(phases)):
        raise ValueError("missing controlled DPI evidence or phases")

    def vector(value, expected):
        return isinstance(value, list) and all(type(n) is int for n in value) and value == expected

    zooms = ([100, 100, 100], [100, 100, 125], [125, 100, 125], [100, 100, 100])
    for phase, (row, dpi, zoom) in enumerate(zip(data["snapshots"], (96, 144, 192, 96), zooms)):
        scale = lambda n: (n * dpi + 48) // 96
        if (type(row.get("controlled_dpi")) is not int or row["controlled_dpi"] != dpi or
                type(row.get("os_window_dpi")) is not int or not 48 <= row["os_window_dpi"] <= 768 or
                not vector(row.get("logical_client"), [1360, 860]) or
                not vector(row.get("physical_client"), [scale(1360), scale(860)]) or
                row.get("rename_active") is not (phase == 1) or
                not isinstance(row.get("sessions"), list) or len(row["sessions"]) != 3):
            raise ValueError("DPI client, editor or session evidence mismatch")
        reference = ([0, 597, 1360, 206], [0, 597, 1360, 206],
                     [301, 93, 832, 453] if phase == 3 else [227, 93, 906, 453])
        visible = [False, True, True] if phase < 2 else [True, False, True]
        for index, (session, rect, percent) in enumerate(zip(row["sessions"], reference, zoom)):
            if not isinstance(session, dict):
                raise ValueError("invalid DPI session measurement")
            x, y, width, height = rect
            physical = [scale(x), scale(y), scale(x + width) - scale(x), scale(y + height) - scale(y)]
            cells = [(base * dpi * percent + 4800) // 9600 for base in (12, 26)]
            grid = [physical[2] // cells[0], physical[3] // cells[1]]
            if (session.get("group") != ("main" if index == 2 else "bottom") or
                    type(session.get("index")) is not int or session["index"] != (0 if index == 2 else index) or
                    session.get("visible") is not visible[index] or
                    type(session.get("zoom_percent")) is not int or session["zoom_percent"] != percent or
                    type(session.get("display_dpi")) is not int or session["display_dpi"] != dpi or
                    not vector(session.get("logical_rect"), rect) or
                    not vector(session.get("physical_rect"), physical) or
                    not vector(session.get("cell_pixels"), cells) or not vector(session.get("grid"), grid)):
                raise ValueError(f"DPI session geometry mismatch: {row['phase']} session {index}")


def desktop_gone(pid):
    api = ctypes.WinDLL("user32", use_last_error=True)
    api.OpenDesktopW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    api.OpenDesktopW.restype = wintypes.HANDLE
    api.CloseDesktop.argtypes = [wintypes.HANDLE]
    handle = api.OpenDesktopW(f"TermForgeShell-{pid}", 0, False, 1)
    if handle:
        api.CloseDesktop(handle)
        return False
    if ctypes.get_last_error() not in (2, 3):
        raise ctypes.WinError(ctypes.get_last_error())
    return True


def execute(exe, mode, out, timeout=40, settings_source=None):
    out.mkdir(parents=True, exist_ok=False)
    if settings_source is not None:
        shutil.copyfile(settings_source, out / "workspace.layout")
    job = OwnedJob()
    process = None
    record = dict(mode=mode, status="failed")
    try:
        process = subprocess.Popen([str(exe.resolve()), "--test", mode, str((out / "frame.bmp").resolve())],
                                   cwd=out, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        job.assign(process.pid)
        try:
            stdout, stderr = process.communicate(b"GO\n", timeout=timeout)
            record["timed_out"] = False
        except subprocess.TimeoutExpired:
            record["timed_out"] = True
            record["job_at_timeout"] = job.accounting()
            job.terminate()
            stdout, stderr = process.communicate(timeout=10)
        record.update(exit_code=process.returncode, stdout=stdout.decode("utf-8", "replace"),
                      stderr=stderr.decode("utf-8", "replace"), job_after=job.drain())
        if record["job_after"]["active"]:
            raise ValueError("owned processes remained")
        record["desktop_gone"] = desktop_gone(process.pid)
        if not record["desktop_gone"]:
            raise ValueError("owned desktop remained")
        if mode != "watchdog" and (record["timed_out"] or record["exit_code"]):
            raise ValueError("shell probe failed: " + record["stderr"].strip())
        result = json.loads(record["stdout"])
        if mode == "watchdog":
            if not record["timed_out"] or result != {"watchdog_ready": True} or record["job_at_timeout"]["active"] < 2:
                raise ValueError("watchdog did not contain a live shell")
        else:
            if record["timed_out"] or record["exit_code"]:
                raise ValueError("shell probe execution failed")
            validate(result, mode)
            if mode == "ui-main":
                validate_main_geometry(json.loads((out / "geometry.json").read_text(encoding="utf-8")))
            if mode == "ui-dpi":
                validate_dpi_geometry(json.loads((out / "dpi.json").read_text(encoding="utf-8")))
            record["result"] = result
            record["frame_sha256"] = hashlib.sha256((out / "frame.bmp").read_bytes()).hexdigest()
        record["status"] = "passed"
        return record
    except BaseException as error:
        record["error"] = str(error)
        raise
    finally:
        if process is not None and process.poll() is None:
            job.terminate()
            process.communicate(timeout=10)
        job.close()
        (out / "sample.json").write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--bin-dir", type=Path)
    cli.add_argument("--out", type=Path, required=True)
    cli.add_argument("--repeats", type=int, default=3)
    args = cli.parse_args()
    if not 1 <= args.repeats <= 10:
        cli.error("repeats must be 1..10")
    exe = binary("termforge_shell", args.bin_dir)
    args.out.mkdir(parents=True, exist_ok=False)
    files = ["experiments/shell/shell.cpp", "experiments/shell/workspace.hpp", "experiments/shell/queues.hpp", "experiments/shell/run.py",
             "experiments/shell/workspace_interactions.hpp", "experiments/shell/workspace_scenarios.hpp",
             "experiments/shell/workspace_main_scenario.hpp", "assets/fonts/RobotoMono-Regular.ttf",
             "experiments/shell/workspace_dpi_scenario.hpp",
             "experiments/shell/layout_state.hpp", "experiments/shell/ui_font.hpp",
             "experiments/shell/resize.hpp", "experiments/shell/size_client.cpp",
             "experiments/conpty/session.hpp", "experiments/presentation/render.hpp", "experiments/presentation/desktop.hpp",
             "experiments/core/terminal.hpp", "experiments/core/screen.hpp", "experiments/core/unicode_width.hpp"]
    meta = dict(status="running", platform=platform.platform(), started_utc=datetime.now(timezone.utc).isoformat(),
                exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                size_client_sha256=hashlib.sha256((exe.parent / "shell_size_client.exe").read_bytes()).hexdigest(),
                sources={f: hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files},
                input_endpoint="scripted Ctrl-C WM_CHAR acceptance to TF_AFTER state and GDI submission; not physical keyboard", presented_done=None)
    try:
        for repeat in range(args.repeats):
            for mode in ("normal", "close", "resize", "resize-close", "ui", "ui-edit", "ui-main", "ui-dpi"):
                execute(exe, mode, args.out / f"{repeat}-{mode}")
                print(f"{repeat} {mode}: passed", flush=True)
            execute(exe, "ui-restore", args.out / f"{repeat}-ui-restore",
                    settings_source=args.out / f"{repeat}-ui-edit" / "workspace.layout")
            print(f"{repeat} ui-restore: passed", flush=True)
        execute(exe, "watchdog", args.out / "watchdog", timeout=3)
        meta["status"] = "complete"
    except BaseException as error:
        meta.update(status="failed", error=str(error))
        raise
    finally:
        meta["finished_utc"] = datetime.now(timezone.utc).isoformat()
        (args.out / "manifest.json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
