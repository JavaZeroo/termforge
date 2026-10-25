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
import uuid

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from experiments.build_paths import binary
from experiments.conpty.run import OwnedJob
from experiments.shell.history_checks import validate_history, validate_native_scroll
from experiments.shell.paste_checks import validate_paste_workflow
from experiments.shell.clipboard_checks import require_hosted_clipboard, validate_clipboard_workflow
from experiments.shell.selection_checks import validate_selection_workflow
from experiments.shell.selection_bytes_checks import validate_selection_bytes_workflow
from experiments.shell.selection_context_checks import validate_selection_context_workflow
from experiments.shell.action_checks import validate_actions_workflow
from experiments.shell.find_checks import validate_find_workflow, validate_find_stream_workflow
from experiments.shell.workflow_sessions_checks import validate_workflow_sessions


WORKSPACE_DATA_MODES = {
    "ui-workspace-save", "ui-workspace-restore", "ui-workspace-v1", "ui-workspace-empty-create",
    "ui-workspace-empty", "ui-workspace-corrupt", "ui-workspace-future",
    "ui-workspace-write-failure", "ui-workspace-close-failure",
}


def validate(row, mode):
    if mode in WORKSPACE_DATA_MODES:
        closed = 3 if mode in ("ui-workspace-save", "ui-workspace-restore") else 0 if mode == "ui-workspace-empty" else 1
        expected_failure = mode in ("ui-workspace-future", "ui-workspace-close-failure")
        expected = {"valid": True, "mode": mode, "sessions_closed": closed, "active_children_after": 0,
                    "settings_save_failed": expected_failure, "presented_done": None}
        if expected_failure:
            expected["configuration_preserved"] = True
            expected["settings_discarded"] = True
        if mode == "ui-workspace-write-failure":
            expected["retry_saved"] = True
        if row != expected:
            raise ValueError("workspace persistence, fresh sessions or cleanup failed")
        return
    if mode == "candidate-input-escape":
        if (row.get("valid") is not True or row.get("exit_code") != 37 or
                row.get("active_children_after") != 0 or row.get("candidate_replies") is not True or
                row.get("command_verified") is not True or row.get("input_rejected") != 0 or
                row.get("presented_done") is not None):
            raise ValueError("candidate escape lifecycle failed")
        return
    if mode == "candidate-tui":
        if row.get("valid") is not True or row.get("exit_code") != 37 or row.get("active_children_after") != 0:
            raise ValueError("candidate shell lifecycle failed")
        if any(row.get(field) is not True for field in ("candidate_tui_start", "candidate_replies", "candidate_tui", "candidate_resize", "command_verified")):
            raise ValueError("candidate TUI start, reply, editor or resize verification failed")
        if row.get("input_rejected") != 0 or row.get("frames", 0) < 1 or row.get("consumed_bytes", 0) <= 0:
            raise ValueError("candidate input or presentation gate failed")
        if not 0 < row.get("max_consumed_per_tick", 0) <= 65536 or row.get("presented_done") is not None:
            raise ValueError("candidate UI budget or presentation endpoint failed")
        return
    if mode == "ui-workflow-sessions":
        if row != {"valid": True, "mode": mode, "sessions_closed": 18, "active_children_after": 0, "presented_done": None}:
            raise ValueError("eight-session workflow or cleanup failed")
        return
    if mode in ("ui-selection-context", "ui-copy-context-system"):
        if row != {"valid": True, "mode": mode, "sessions_closed": 3, "active_children_after": 0, "presented_done": None}:
            raise ValueError("selection context workflow or cleanup failed")
        return
    if mode in ("ui-paste", "ui-paste-system", "ui-clipboard-system", "ui-selection", "ui-copy-system", "ui-find", "ui-find-stream", "ui-selection-bytes", "ui-copy-bytes-system", "ui-actions", "ui-actions-system"):
        if row != {"valid": True, "mode": mode, "sessions_closed": 2, "active_children_after": 0, "presented_done": None}:
            raise ValueError("paste window workflow or cleanup failed")
        return
    if mode == "ui-keyboard":
        if row != {"valid": True, "mode": mode, "sessions_closed": 1, "active_children_after": 0, "presented_done": None}:
            raise ValueError("keyboard workflow or cleanup failed")
        return
    if mode == "ui-history":
        if (row.get("valid") is not True or row.get("mode") != mode or
                row.get("history_verified") is not True or row.get("sessions_closed") != 3 or
                row.get("active_children_after") != 0 or row.get("presented_done", 1) is not None):
            raise ValueError("workspace history or cleanup gate failed")
        return
    if mode == "ui-dpi":
        if (row.get("valid") is not True or row.get("mode") != mode or
                row.get("dpi_and_zoom_verified") is not True or
                type(row.get("sessions_closed")) is not int or row["sessions_closed"] != 3 or
                type(row.get("active_children_after")) is not int or row["active_children_after"] != 0 or
                row.get("presented_done", 1) is not None):
            raise ValueError("workspace DPI, zoom or cleanup gate failed")
        return
    if mode == "ui-monitors":
        if row != {"valid": True, "mode": mode, "real_monitor_transition": True,
                   "sessions_closed": 3, "active_children_after": 0, "presented_done": None}:
            raise ValueError("real monitor transition or cleanup gate failed")
        return
    if mode == "ui-shells":
        if row != {"valid": True, "mode": mode, "shells_verified": 4,
                   "sessions_closed": 4, "active_children_after": 0, "presented_done": None}:
            raise ValueError("local shell launch or cleanup gate failed")
        return
    if mode == "ui-launch-failure":
        if row != {"valid": True, "mode": mode, "launch_failure_visible": True, "recovered": True,
                   "sessions_closed": 3, "active_children_after": 0, "presented_done": None}:
            raise ValueError("launch failure presentation, recovery or cleanup gate failed")
        return
    if mode == "ui-split":
        if row != {"valid": True, "mode": mode, "split_verified": True,
                   "sessions_closed": 3, "active_children_after": 0, "presented_done": None}:
            raise ValueError("main split or cleanup gate failed")
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


def validate_local_shells(data):
    if (not isinstance(data, dict) or data.get("schema") != "termforge-local-shells-v1" or
            data.get("shells") != ["cmd", "powershell", "pwsh", "wsl-Ubuntu"] or
            data.get("main_sessions") != 2 or data.get("bottom_sessions") != 2 or
            not isinstance(data.get("working_directory"), str) or not data["working_directory"] or
            data.get("normal_exit") is not True or data.get("presented_done", 1) is not None):
        raise ValueError("local shell identity, directory or presentation evidence mismatch")


def validate_launch_failure(data):
    if (not isinstance(data, dict) or data.get("schema") != "termforge-launch-failure-v1" or
            not isinstance(data.get("error"), str) or "CreateProcess" not in data["error"] or
            data.get("recovered") is not True or data.get("sessions_closed") != 3 or
            data.get("presented_done", 1) is not None):
        raise ValueError("launch failure visibility, recovery or cleanup evidence mismatch")


def validate_main_split(data):
    if (not isinstance(data, dict) or data.get("schema") != "termforge-main-split-v1" or
            any(data.get(field) is not True for field in
                ("independent_output", "background_output", "restored", "right_closed", "left_alive")) or
            data.get("sessions_closed") != 3 or data.get("presented_done", 1) is not None or
            not isinstance(data.get("snapshots"), list) or len(data["snapshots"]) != 2 or
            [row.get("phase") for row in data["snapshots"]] != ["active", "restored"]):
        raise ValueError("main split behavior evidence mismatch")
    for snapshot in data["snapshots"]:
        if (snapshot.get("split_active") is not True or snapshot.get("right_focus") is not True or
                snapshot.get("left_visible") is not True or snapshot.get("right_visible") is not True):
            raise ValueError("main split visibility or focus mismatch")
        left, right = snapshot.get("left_rect"), snapshot.get("right_rect")
        if (not isinstance(left, list) or not isinstance(right, list) or len(left) != 4 or len(right) != 4 or
                any(type(value) is not int for value in left + right) or
                left[2] != right[0] or left[1] != right[1] or left[3] != right[3] or left[2] <= left[0]):
            raise ValueError("main split physical rectangles mismatch")


def validate_monitor_geometry(data):
    phases = ("origin", "target", "restored")
    if (not isinstance(data, dict) or data.get("schema") != "termforge-ui-monitors-v1" or
            data.get("real_monitor_transition") is not True or
            type(data.get("distinct_dpi")) is not bool or data.get("presented_done", 1) is not None or
            not isinstance(data.get("monitors"), list) or len(data["monitors"]) != 2 or
            not isinstance(data.get("snapshots"), list) or len(data["snapshots"]) != 3 or
            [row.get("phase") for row in data["snapshots"]] != list(phases)):
        raise ValueError("missing real monitor evidence or phases")
    roles = [row.get("role") for row in data["monitors"]]
    if roles != ["origin", "target"]:
        raise ValueError("incorrect monitor roles")
    for monitor in data["monitors"]:
        for field in ("rect", "work"):
            rect = monitor.get(field)
            if (not isinstance(rect, list) or len(rect) != 4 or
                    any(type(value) is not int for value in rect) or rect[2] <= rect[0] or rect[3] <= rect[1]):
                raise ValueError("invalid monitor rectangle")
        if type(monitor.get("dpi")) is not int or monitor["dpi"] <= 0:
            raise ValueError("invalid monitor DPI")
    if data["distinct_dpi"] != (data["monitors"][0]["dpi"] != data["monitors"][1]["dpi"]):
        raise ValueError("monitor DPI distinction was fabricated")
    for index, snapshot in enumerate(data["snapshots"]):
        monitor = data["monitors"][0 if index != 1 else 1]
        if snapshot.get("window_dpi") != monitor["dpi"] or snapshot.get("rename_focused") is not True:
            raise ValueError("monitor window DPI or rename focus mismatch")
        rect = snapshot.get("window_rect")
        if not isinstance(rect, list) or len(rect) != 4 or any(type(value) is not int for value in rect):
            raise ValueError("invalid monitor window rectangle")
        center_x, center_y = (rect[0] + rect[2]) // 2, (rect[1] + rect[3]) // 2
        bounds = monitor["rect"]
        if not (bounds[0] <= center_x < bounds[2] and bounds[1] <= center_y < bounds[3]):
            raise ValueError("window center is outside the claimed real monitor")
        sessions = snapshot.get("sessions")
        if not isinstance(sessions, list) or len(sessions) != 3:
            raise ValueError("monitor session count mismatch")
        for session in sessions:
            if (session.get("display_dpi") != monitor["dpi"] or type(session.get("visible")) is not bool or
                    not isinstance(session.get("grid"), list) or len(session["grid"]) != 2 or
                    not isinstance(session.get("cell_pixels"), list) or len(session["cell_pixels"]) != 2 or
                    any(type(value) is not int or value <= 0 for value in session["grid"] + session["cell_pixels"])):
                raise ValueError("monitor session projection mismatch")


def validate_main_geometry(data):
    """Derive the logical layout from measured client pixels, not nominal sizes."""
    phases = ("empty-1360", "session-1360", "session-2048")
    if (not isinstance(data, dict) or data.get("ui_font_family") != "Roboto Mono" or
            not isinstance(data.get("snapshots"), list) or
            [row.get("phase") for row in data["snapshots"] if isinstance(row, dict)] != list(phases) or
            len(data["snapshots"]) != 3):
        raise ValueError("missing main workspace geometry phases or font identity")
    for index, row in enumerate(data["snapshots"]):
        dpi = row.get("dpi")
        pixels = row.get("client_pixels")
        if (type(dpi) is not int or dpi <= 0 or not isinstance(pixels, list) or len(pixels) != 2 or
                any(type(value) is not int or value <= 0 for value in pixels)):
            raise ValueError("main geometry requires actual client pixels and DPI")
        width, height = [(value * 96 + dpi // 2) // dpi for value in pixels]
        side = int((width - 2) / 6 + 0.5)
        left, right = side + 1, width - side - 1
        upper = int((max(100, height - 90) - 1) * (2 / 3) + 0.5)
        toolbar_top = 52 if index == 0 else 59
        toolbar_height = 36 if index == 0 else 34
        terminal_top = toolbar_top + toolbar_height
        terminal = [left, terminal_top, right - left, upper + 33 - terminal_top]
        fields = {
            "window": [width, height],
            "main_group": [left, 33, right - left, toolbar_top - 33],
            "main_toolbar": [left, toolbar_top, right - left, toolbar_height],
            "main_address": [left + 103, toolbar_top + 4, right - left - 133, toolbar_height - 8],
            "main_terminal": terminal,
            "main_child": None if index == 0 else terminal,
            "shell_terminal": [0, upper + 84, width, max(upper + 110, height - 57) - upper - 84],
        }
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
    if (not isinstance(data, dict) or data.get("schema") != "termforge-ui-dpi-v2" or
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
            bar = session.get("native_scrollbar_width")
            if type(bar) is not int or not 1 <= bar <= 256:
                raise ValueError("missing native OS scrollbar measurement")
            client = [physical[2] - bar, physical[3]]
            grid = [client[0] // cells[0], client[1] // cells[1]]
            if (session.get("group") != ("main" if index == 2 else "bottom") or
                    type(session.get("index")) is not int or session["index"] != (0 if index == 2 else index) or
                    session.get("visible") is not visible[index] or
                    type(session.get("zoom_percent")) is not int or session["zoom_percent"] != percent or
                    type(session.get("display_dpi")) is not int or session["display_dpi"] != dpi or
                    not vector(session.get("logical_rect"), rect) or
                    not vector(session.get("physical_rect"), physical) or
                    not vector(session.get("client_pixels"), client) or
                    not vector(session.get("cell_pixels"), cells) or not vector(session.get("grid"), grid)):
                raise ValueError(f"DPI session geometry mismatch: {row['phase']} session {index}")


def desktop_gone(name):
    api = ctypes.WinDLL("user32", use_last_error=True)
    api.OpenDesktopW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    api.OpenDesktopW.restype = wintypes.HANDLE
    api.CloseDesktop.argtypes = [wintypes.HANDLE]
    handle = api.OpenDesktopW(name, 0, False, 1)
    if handle:
        api.CloseDesktop(handle)
        return False
    if ctypes.get_last_error() not in (2, 3):
        raise ctypes.WinError(ctypes.get_last_error())
    return True


class OwnedTestDesktop:
    """Parent owns the desktop until the complete child process has exited.

    Windows text services can retain process-scoped threads. Ending only the
    UI thread is insufficient once actual IME APIs are used.
    """
    def __init__(self):
        self.api = ctypes.WinDLL("user32", use_last_error=True)
        self.api.CreateDesktopW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR, ctypes.c_void_p,
                                            wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
        self.api.CreateDesktopW.restype = wintypes.HANDLE
        self.api.CloseDesktop.argtypes = [wintypes.HANDLE]
        self.api.CloseDesktop.restype = wintypes.BOOL
        self.api.GetProcessWindowStation.restype = wintypes.HANDLE
        self.api.GetUserObjectInformationW.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p,
                                                      wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
        self.name = f"TermForgeShell-{os.getpid()}-{uuid.uuid4().hex}"
        station = ctypes.create_unicode_buffer(256)
        needed = wintypes.DWORD()
        if not self.api.GetUserObjectInformationW(self.api.GetProcessWindowStation(), 2, station,
                                                  ctypes.sizeof(station), ctypes.byref(needed)):
            raise ctypes.WinError(ctypes.get_last_error())
        self.full_name = station.value + "\\" + self.name
        self.handle = self.api.CreateDesktopW(self.name, None, None, 0, 0x10000000, None)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())

    def close(self):
        if self.handle:
            if not self.api.CloseDesktop(self.handle):
                raise ctypes.WinError(ctypes.get_last_error())
            self.handle = None
        return desktop_gone(self.name)


def execute(exe, mode, out, timeout=40, settings_source=None, *, resources=False):
    if mode in ("ui-paste-system", "ui-clipboard-system", "ui-copy-system", "ui-copy-bytes-system", "ui-copy-context-system", "ui-actions-system"):
        require_hosted_clipboard(os.environ)
    if resources and mode != "ui-workflow-sessions":
        raise ValueError("resource observation is limited to the bounded sessions workflow")
    out.mkdir(parents=True, exist_ok=False)
    job = desktop = None
    assigned = False
    process = None
    resource_recorder = None
    resource_started = False
    record = dict(mode=mode, status="failed")
    try:
        if settings_source is not None:
            shutil.copyfile(settings_source, out / "workspace.layout")
        job = OwnedJob()
        desktop = OwnedTestDesktop()
        record["desktop_name"] = desktop.name
        process = subprocess.Popen([str(exe.with_name("shell_test_launcher.exe").resolve()), desktop.full_name,
                                    mode, str((out / "frame.bmp").resolve())],
                                   cwd=out, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        job.assign(process.pid)
        assigned = True
        if resources:
            from experiments.shell.workflow_resources import WorkflowResources
            resource_recorder = WorkflowResources(process, out / "_resources-private", seconds=timeout + 10)
            resource_recorder.start()
            resource_started = True
            resource_recorder.mark("launcher-go")
        try:
            stdout, stderr = process.communicate(b"GO\n", timeout=timeout)
            record["timed_out"] = False
        except subprocess.TimeoutExpired:
            record["timed_out"] = True
            record["job_at_timeout"] = job.accounting()
            job.terminate()
            stdout, stderr = process.communicate(timeout=10)
        if resource_started:
            resource_recorder.mark("launcher-exit")
        record.update(exit_code=process.returncode, stdout=stdout.decode("utf-8", "replace"),
                      stderr=stderr.decode("utf-8", "replace"), job_after=job.drain())
        if record["job_after"]["active"]:
            raise ValueError("owned processes remained")
        record["desktop_gone"] = desktop.close()
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
            if mode == "ui-monitors":
                validate_monitor_geometry(json.loads((out / "monitors.json").read_text(encoding="utf-8")))
            if mode == "ui-shells":
                validate_local_shells(json.loads((out / "shells.json").read_text(encoding="utf-8")))
            if mode == "ui-launch-failure":
                validate_launch_failure(json.loads((out / "launch-failure.json").read_text(encoding="utf-8")))
            if mode == "ui-split":
                validate_main_split(json.loads((out / "split.json").read_text(encoding="utf-8")))
            if mode == "ui-history":
                validate_history(json.loads((out / "history.json").read_text(encoding="utf-8")))
                validate_native_scroll([json.loads(line) for line in (out / "native-scroll.jsonl").read_text(encoding="utf-8").splitlines()])
            if mode == "ui-keyboard":
                expected = ("A汉e\u0301🙂👩\u200d💻1\ufe0f\u20e3".encode("utf-8") + b"\t\x1b[Z\x00\x1b[1;5D\x1b[3;4~\x1bx" +
                            "é".encode("utf-8") + b"\x00\x7f\x1b[H\x1b[F\x1b[2~\x1b[3~\x1b[5~\x1b[6~" +
                            "\ufffd🙂\ufffd".encode("utf-8"))
                if (out / "input.bin").read_bytes() != expected:
                    raise ValueError("real keyboard receiver bytes differ from independent expectation")
            if mode in ("ui-paste", "ui-paste-system"):
                expected = b"DEFAULT_ENTER_ACCEPT\r" + b"P\xe6\xb1\x89\r" * 5000
                if (out / "input.bin").read_bytes() != expected:
                    raise ValueError("paste confirmation/target receiver differs from independent expectation")
                record["system_clipboard"] = mode == "ui-paste-system"
                record["paste_workflow"] = validate_paste_workflow(out)
            if mode == "ui-clipboard-system":
                record["system_clipboard"] = True
                record["clipboard_workflow"] = validate_clipboard_workflow(out)
            if mode in ("ui-selection", "ui-copy-system"):
                record["selection_workflow"] = validate_selection_workflow(out, mode == "ui-copy-system", record)
            if mode == "ui-find":
                record["find_workflow"] = validate_find_workflow(out, record)
            if mode == "ui-find-stream":
                record["find_stream_workflow"] = validate_find_stream_workflow(out, record)
            if mode in ("ui-selection-bytes", "ui-copy-bytes-system"):
                record["selection_bytes_workflow"] = validate_selection_bytes_workflow(out, mode == "ui-copy-bytes-system", record)
            if mode in ("ui-selection-context", "ui-copy-context-system"):
                record["selection_context_workflow"] = validate_selection_context_workflow(out, mode == "ui-copy-context-system", record)
            if mode in ("ui-actions", "ui-actions-system"):
                record["actions_workflow"] = validate_actions_workflow(out, mode == "ui-actions-system", record)
            if mode == "ui-workflow-sessions":
                record["sessions_workflow"] = validate_workflow_sessions(out, record)
            record["result"] = result
            if mode in WORKSPACE_DATA_MODES:
                record["data_only"] = True
            else:
                record["frame_sha256"] = hashlib.sha256((out / "frame.bmp").read_bytes()).hexdigest()
        record["status"] = "passed"
        return record
    except BaseException as error:
        record["error"] = str(error)
        raise
    finally:
        cleanup_errors = []
        # Keep the original failure and evidence even if a later cleanup fails.
        # Before GO, an unassigned launcher cannot have created a shell tree.
        if process is not None and process.poll() is None:
            if assigned:
                try:
                    job.terminate()
                except OSError as error:
                    cleanup_errors.append(str(error))
            try:
                if process.poll() is None:
                    process.kill()
                stdout, stderr = process.communicate(timeout=10)
                record.setdefault("stdout", stdout.decode("utf-8", "replace"))
                record.setdefault("stderr", stderr.decode("utf-8", "replace"))
                record.setdefault("exit_code", process.returncode)
            except (OSError, subprocess.TimeoutExpired) as error:
                cleanup_errors.append(str(error))
        if job is not None:
            try:
                if assigned:
                    record["job_after_cleanup"] = job.drain()
                    if record["job_after_cleanup"]["active"]:
                        cleanup_errors.append("owned processes remained after cleanup")
            except OSError as error:
                cleanup_errors.append(str(error))
            finally:
                job.close()
        if desktop is not None and desktop.handle:
            try:
                record["desktop_gone"] = desktop.close()
                if not record["desktop_gone"]:
                    cleanup_errors.append("owned desktop remained after cleanup")
            except OSError as error:
                cleanup_errors.append(str(error))
        if resource_recorder is not None:
            if resource_started:
                try:
                    resource_recorder.mark("cleanup-finished")
                except BaseException as error:
                    cleanup_errors.append("resource cleanup marker failed: " + str(error))
            try:
                summary = resource_recorder.finish(native_phases=out / "resource-phases.jsonl",
                                                   observations_path=out / "resource-observations.jsonl")
                (out / "resource-summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
                record["resources"] = {"summary": "resource-summary.json", "scope": summary["scope"],
                                       "performance_gate": summary["performance_gate"]}
            except BaseException as error:
                cleanup_errors.append("resource recording failed: " + str(error))
        if cleanup_errors:
            record["cleanup_errors"] = cleanup_errors
            record["status"] = "failed"
        (out / "sample.json").write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        if cleanup_errors and "error" not in record:
            raise RuntimeError("; ".join(cleanup_errors))


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
             "experiments/shell/workspace_workflow_scenario.hpp", "experiments/shell/workflow_scenario_state.hpp",
             "experiments/shell/workflow_controller.hpp", "experiments/shell/workflow_fixture.hpp",
             "experiments/shell/runtime_observation.hpp", "experiments/shell/operation_observation.hpp", "experiments/shell/workflow_sessions_checks.py",
             "experiments/shell/workflow_resources.py", "experiments/shell/workflow_resource_phases.py", "benchmarks/tools/sample.py", "benchmarks/tools/summarize_resources.py",
             "experiments/shell/workspace_main_scenario.hpp", "assets/fonts/RobotoMono-Regular.ttf",
             "experiments/shell/workspace_dpi_scenario.hpp", "experiments/shell/workspace_monitor_scenario.hpp",
             "experiments/shell/shell_launch.hpp", "experiments/shell/shell_launch.cpp", "experiments/shell/workspace_shells_scenario.hpp",
             "experiments/shell/workspace_launch_failure_scenario.hpp", "experiments/shell/workspace_split_scenario.hpp",
             "experiments/shell/workspace_history_scenario.hpp", "experiments/shell/history_checks.py",
             "experiments/shell/native_scroll_gesture.hpp",
             "experiments/shell/workspace_keyboard_scenario.hpp", "experiments/shell/input_client.cpp",
             "experiments/shell/workspace_paste_scenario.hpp", "experiments/shell/paste_prompt.hpp",
             "experiments/shell/paste_scenario_state.hpp", "experiments/shell/paste_checks.py",
             "experiments/shell/ime.hpp", "experiments/shell/test_launcher.cpp",
             "experiments/shell/clipboard_worker.hpp", "experiments/shell/clipboard_worker.cpp",
             "experiments/shell/keyboard.hpp", "experiments/shell/paste.hpp",
             "experiments/shell/layout_state.hpp", "experiments/shell/workspace_state.hpp", "experiments/shell/workspace_persistence_scenario.hpp",
             "experiments/shell/ui_font.hpp",
             "experiments/shell/resize.hpp", "experiments/shell/size_client.cpp",
             "experiments/conpty/session.hpp", "experiments/presentation/render.hpp", "experiments/presentation/desktop.hpp",
             "experiments/core/history_view.hpp", "experiments/core/text_projection.hpp",
             "experiments/core/terminal.hpp", "experiments/core/screen.hpp", "experiments/core/unicode_width.hpp"]
    meta = dict(status="running", platform=platform.platform(), started_utc=datetime.now(timezone.utc).isoformat(),
                exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                size_client_sha256=hashlib.sha256((exe.parent / "shell_size_client.exe").read_bytes()).hexdigest(),
                helpers={name: hashlib.sha256((exe.parent / name).read_bytes()).hexdigest() for name in
                         ("shell_test_launcher.exe", "shell_input_client.exe", "shell_clipboard_reader.exe")},
                sources={f: hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files},
                input_endpoint="scripted Ctrl-C WM_CHAR acceptance to TF_AFTER state and GDI submission; not physical keyboard", presented_done=None)
    try:
        for repeat in range(args.repeats):
            for mode in ("normal", "close", "resize", "resize-close", "ui", "ui-edit", "ui-main", "ui-dpi", "ui-history", "ui-keyboard", "ui-paste"):
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
