"""Real shell messages/output/frame/lifecycle regression; all windows are owned."""
import copy
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import subprocess
import struct
import tempfile
import unittest
from unittest import mock

from experiments.build_paths import ROOT, binary
from experiments.shell.run import (execute, validate, validate_main_geometry, validate_dpi_geometry,
                                   validate_monitor_geometry, validate_local_shells, validate_launch_failure,
                                   validate_main_split)
from experiments.shell.history_checks import validate_history, validate_native_scroll
from experiments.shell.clipboard_checks import require_hosted_clipboard, validate_clipboard_workflow
from experiments.shell.selection_checks import validate_selection, validate_selection_workflow, _validate_copy_popup, _validate_copy_contention
from experiments.shell.find_checks import (validate_find, validate_find_workflow,
                                           validate_find_stream, validate_find_stream_workflow)


def synthetic_main_geometry(initial, resized):
    snapshots = []
    for phase, (width, height), empty in zip(
            ("empty-1360", "session-1360", "session-2048"), (initial, initial, resized), (True, False, False)):
        side = int((width - 2) / 6 + 0.5)
        left, right = side + 1, width - side - 1
        upper = int((max(100, height - 90) - 1) * (2 / 3) + 0.5)
        toolbar_top = 52 if empty else 59
        toolbar_height = 36 if empty else 34
        terminal_top = toolbar_top + toolbar_height
        terminal = [left, terminal_top, right - left, upper + 33 - terminal_top]
        snapshots.append(dict(
            phase=phase, dpi=96, client_pixels=[width, height], window=[width, height],
            main_group=[left, 33, right - left, toolbar_top - 33],
            main_toolbar=[left, toolbar_top, right - left, toolbar_height],
            main_address=[left + 103, toolbar_top + 4, right - left - 133, toolbar_height - 8],
            main_terminal=terminal, main_child=None if empty else terminal.copy(),
            shell_terminal=[0, upper + 84, width, max(upper + 110, height - 57) - upper - 84]))
    return {"ui_font_family": "Roboto Mono", "snapshots": snapshots}


def narrow_workspace_fixture():
    return ("TermForge-workspace 2\n"
            "left 0.16666666666666666\nright 0.16666666666666666\n"
            "upper 0.66666666666666663\nleft_top 0.5\nright_top 0.5\n"
            "sides 1\nfocus 0\nwindow 0\nwindow-y 0\nwindow-width 1008\nwindow-height 681\n"
            "main-focused 0\nbottom-active 0\nmain-active -1\nsplit-index -1\n"
            "split-active 0\nsplit-right-focus 0\nbottom-count 1\nmain-count 0\n"
            "session bottom cmd - - 636d642031 100\n").encode("ascii")


def validate_workspace_geometry(data, requested, *, restoring, origin=(0, 0)):
    if not isinstance(data, dict) or data.get("schema") != "termforge-workspace-restore-v1":
        raise ValueError("workspace geometry identity mismatch")
    dpi = data.get("dpi")
    if type(dpi) is not int or dpi <= 0:
        raise ValueError("workspace geometry requires actual DPI")
    api = ctypes.WinDLL("user32", use_last_error=True)
    api.AdjustWindowRectExForDpi.argtypes = [ctypes.POINTER(wintypes.RECT), wintypes.DWORD,
                                               wintypes.BOOL, wintypes.DWORD, wintypes.DWORD]
    api.AdjustWindowRectExForDpi.restype = wintypes.BOOL
    physical = lambda value: (value * dpi + 48) // 96
    logical = lambda value: (value * 96 + dpi // 2) // dpi
    client_width, client_height = (physical(value) for value in requested)
    frame = wintypes.RECT(0, 0, client_width, client_height)
    if not api.AdjustWindowRectExForDpi(ctypes.byref(frame), 0x00CF0000, False, 0, dpi):
        raise ctypes.WinError(ctypes.get_last_error())
    frame_width, frame_height = frame.right - frame.left, frame.bottom - frame.top
    x, y = origin
    desired = wintypes.RECT(x, y, x + frame_width, y + frame_height)
    expected_window = [x, y, frame_width, frame_height]
    if restoring:
        api.MonitorFromRect.argtypes = [ctypes.POINTER(wintypes.RECT), wintypes.DWORD]
        api.MonitorFromRect.restype = wintypes.HWND
        monitor = api.MonitorFromRect(ctypes.byref(desired), 2)
        if not monitor:
            raise ValueError("workspace monitor query failed")
        class MonitorInfo(ctypes.Structure):
            _fields_ = [("size", wintypes.DWORD), ("monitor", wintypes.RECT),
                        ("work", wintypes.RECT), ("flags", wintypes.DWORD)]
        api.GetMonitorInfoW.argtypes = [wintypes.HWND, ctypes.POINTER(MonitorInfo)]
        api.GetMonitorInfoW.restype = wintypes.BOOL
        info = MonitorInfo(ctypes.sizeof(MonitorInfo))
        if not api.GetMonitorInfoW(monitor, ctypes.byref(info)):
            raise ctypes.WinError(ctypes.get_last_error())
        work = info.work
        outer_width = min(frame_width, work.right - work.left)
        outer_height = min(frame_height, work.bottom - work.top)
        x = max(work.left, min(x, max(work.left, work.right - outer_width)))
        y = max(work.top, min(y, max(work.top, work.bottom - outer_height)))
        expected_window = [x, y, outer_width, outer_height]
    actual_width = expected_window[2] - (frame_width - client_width)
    actual_height = expected_window[3] - (frame_height - client_height)
    if data.get("window") != expected_window or data.get("client") != [actual_width, actual_height]:
        raise ValueError(f"workspace native bounds mismatch: {data.get('window')} {data.get('client')} "
                         f"expected {expected_window} {[actual_width, actual_height]}")
    return [logical(x), logical(y), logical(actual_width), logical(actual_height)]


class MainGeometryOracleTest(unittest.TestCase):
    def test_actual_client_bounds_drive_geometry(self):
        for initial, resized in (((1008, 681), (1008, 681)), ((1360, 860), (2048, 1120))):
            with self.subTest(initial=initial, resized=resized):
                validate_main_geometry(synthetic_main_geometry(initial, resized))
        data = synthetic_main_geometry((1360, 860), (2048, 1120))
        for phase, field in ((0, "main_group"), (1, "main_child"), (2, "client_pixels"), (0, "dpi")):
            with self.subTest(corruption=(phase, field)):
                changed = copy.deepcopy(data)
                if field == "dpi":
                    del changed["snapshots"][phase][field]
                else:
                    changed["snapshots"][phase][field][0] += 1
                with self.assertRaises(ValueError):
                    validate_main_geometry(changed)


@unittest.skipUnless(os.name == "nt", "ConPTY shell requires Windows")
class ShellTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe = binary("termforge_shell")
        parent = Path(os.environ.get("TERMFORGE_TEST_ARTIFACTS", ROOT / "build/test-artifacts")) / "shell"
        parent.mkdir(parents=True, exist_ok=True)
        cls.out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))

    def test_mouse_selection_copy_model_zoom_sessions_and_eviction(self):
        for repeat in range(2):
            out = self.out / f"selection-{repeat}"
            record = execute(self.exe, "ui-selection", out)
            data = json.loads((out / "selection.json").read_text(encoding="utf-8"))
            for phase, field, value in ((1, "text", "missing"), (2, "focused", False),
                                        (6, "zoom", 100), (10, "status", "ok")):
                bad = copy.deepcopy(data); bad["snapshots"][phase]["sessions"][0][field] = value
                with self.subTest(phase=phase, field=field), self.assertRaises(ValueError):
                    validate_selection(bad, False, record)
            for field, value in (("desktop_gone", False), ("job_after", {"active": 1})):
                bad_record = copy.deepcopy(record); bad_record[field] = value
                with self.subTest(cleanup=field), self.assertRaises(ValueError):
                    validate_selection(data, False, bad_record)
            for phase, field, value in (("autoscroll-up-held", "ticks", 0),
                                        ("autoscroll-up-stopped", "top", 0),
                                        ("autoscroll-focus-stopped", "main_focused", True),
                                        ("autoscroll-capture-release", "capture_transferred", False),
                                        ("autoscroll-capture-stopped", "captured", True),
                                        ("select-all-shortcut", "text", "missing Unicode"),
                                        ("select-all-command", "action", "actual-popup-unobserved"),
                                        ("input-after-select-all", "raw_main", "x\x01")):
                bad = copy.deepcopy(data)
                next(row for row in bad["interactions"] if row["phase"] == phase)[field] = value
                with self.subTest(interaction=phase, field=field), self.assertRaises(ValueError):
                    validate_selection(bad, False, record)
            for field, value in (("interaction_input", "direct-business-callback"), ("menu_popup_verified", False),
                                 ("popup_observer_idle", False)):
                bad = copy.deepcopy(data); bad[field] = value
                with self.subTest(interaction_contract=field), self.assertRaises(ValueError):
                    validate_selection(bad, False, record)
            for field, value in (("actual_return_command", 14), ("highlighted_state", 0), ("popup_hwnd", 0),
                                 ("timed_out", True), ("navigation_keys", 0), ("last_highlighted_command", 12),
                                 ("item_rect", [0, 0, 0, 0])):
                bad = copy.deepcopy(data); bad["popup_menu"][field] = value
                with self.subTest(actual_popup=field), self.assertRaises(ValueError):
                    validate_selection(bad, False, record)
            bad = copy.deepcopy(data); del bad["popup_menu"]
            with self.assertRaises(ValueError):
                validate_selection(bad, False, record)
            bad = copy.deepcopy(data); bad["interactions"].pop()
            with self.assertRaises(ValueError):
                validate_selection(bad, False, record)
            for index, (bottom, main) in enumerate(((b"x", b""), (b"", b"x\x01"), (b"", b"\x1bx"), (b"", b"xx"))):
                rejected = out / f"negative-selection-bytes-{index}"; rejected.mkdir()
                (rejected / "selection.json").write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")
                (rejected / "selection-popup.json").write_bytes((out / "selection-popup.json").read_bytes())
                (rejected / "selection-bottom.bin").write_bytes(bottom); (rejected / "selection-main.bin").write_bytes(main)
                with self.subTest(raw_selection_bytes=index), self.assertRaises(ValueError):
                    validate_selection_workflow(rejected, False, record)

    def test_selection_autoscroll_with_short_and_tall_workspace(self):
        for height in (681, 860):
            with self.subTest(client_height=height):
                state = self.out / f"selection-height-{height}.layout"
                state.write_bytes(narrow_workspace_fixture().replace(
                    b"window-height 681", f"window-height {height}".encode("ascii")))
                out = self.out / f"selection-height-{height}"
                execute(self.exe, "ui-selection", out, settings_source=state)
                data = json.loads((out / "selection.json").read_text(encoding="utf-8"))
                starts = [row for row in data["interactions"] if row["phase"] in (
                    "autoscroll-up-start", "autoscroll-down-start", "autoscroll-capture-start")]
                self.assertEqual([row["anchor_line"] for row in starts], [75, 50, 40])
                self.assertTrue(all(row["top"] <= row["anchor_line"] < row["top"] + row["page"]
                                    for row in starts))

    def test_find_real_edit_navigation_unicode_output_isolation_and_cancel(self):
        for repeat in range(2):
            out = self.out / f"find-{repeat}"
            record = execute(self.exe, "ui-find", out)
            data = json.loads((out / "find.json").read_text(encoding="utf-8"))
            # These mutations start from actual green HWND/ConPTY evidence.
            # Internal flags cannot hide wrong revealed text, cell geometry,
            # focus, stale results or a cleanup failure.
            for phase, field, wrong in ((0, "query", "wrong"), (7, "case_sensitive", True),
                                        (7, "focus_control", 4101), (8, "query", "needle"),
                                        (9, "yellow_cells", 1), (11, "anchor", 1),
                                        (12, "current", {"line_id": 1}), (13, "terminal_focused", False)):
                bad = copy.deepcopy(data); bad["snapshots"][phase]["sessions"][0 if phase != 13 else 1][field] = wrong
                with self.subTest(phase=phase, field=field), self.assertRaises(ValueError):
                    validate_find(bad, record)
            for phase, field, wrong in ((0, "line", "TF_FIND_078 needle 中文 e\u0301"),
                                        (2, "begin", 11), (8, "end", 22), (11, "version", 99999)):
                bad = copy.deepcopy(data); bad["snapshots"][phase]["sessions"][0]["current"][field] = wrong
                with self.subTest(current_phase=phase, field=field), self.assertRaises(ValueError):
                    validate_find(bad, record)
            bad = copy.deepcopy(data); bad["snapshots"][1]["sessions"][0]["blue_cells"] = 0
            with self.assertRaises(ValueError):
                validate_find(bad, record)
            bad = copy.deepcopy(data); bad["snapshots"][0]["sessions"][0]["current_cells"][12]["bg"] = 4
            with self.assertRaises(ValueError):
                validate_find(bad, record)
            for field, wrong in (("initial_running_checks", 0), ("input_path", "direct-business-callback")):
                bad = copy.deepcopy(data); bad[field] = wrong
                with self.subTest(contract=field), self.assertRaises(ValueError):
                    validate_find(bad, record)
            bad = copy.deepcopy(data); bad["snapshots"].pop()
            with self.assertRaises(ValueError):
                validate_find(bad, record)
            for field, wrong in (("desktop_gone", False), ("job_after", {"active": 1}),
                                 ("cleanup_errors", ["injected worker leak"]), ("timed_out", True)):
                bad_record = copy.deepcopy(record); bad_record[field] = wrong
                with self.subTest(cleanup=field), self.assertRaises(ValueError):
                    validate_find(data, bad_record)
            # Preserve the real samples. Alter copied artifacts to prove that
            # the raw receiver, not the JSON's accepted-input flags, is checked.
            for index, (bottom, main) in enumerate(((b"+#", b"needle"), (b"+#", b"x\r"),
                                                    (b"+#", b"x\x1b"), (b"+#", b"xx"),
                                                    (b"+#needle", b"x"), (b"# +", b"x"))):
                bad_out = out / f"negative-bytes-{index}"; bad_out.mkdir()
                (bad_out / "find.json").write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")
                (bad_out / "find-bottom.bin").write_bytes(bottom); (bad_out / "find-main.bin").write_bytes(main)
                with self.subTest(raw_bytes=index), self.assertRaises(ValueError):
                    validate_find_workflow(bad_out, record)

    def test_find_continuous_output_partial_navigation_replace_cancel_and_target_close(self):
        for repeat in range(2):
            out = self.out / f"find-stream-{repeat}"
            record = execute(self.exe, "ui-find-stream", out, timeout=40)
            data = json.loads((out / "find-stream.json").read_text(encoding="utf-8"))
            # Mutate actual window/receiver observations: a partial result must
            # never masquerade as a global end, survive replacement/cancel, or
            # keep the closed target's native windows and worker alive.
            for phase, session, field, wrong in (
                    (0, 0, "current", copy.deepcopy(data["snapshots"][1]["sessions"][0]["current"])),
                    (1, 0, "status_label", "Searching… 1 / 80 — wrapped"),
                    (2, 0, "status_label", "Searching… partial match (no wrap)"),
                    (3, 0, "current", copy.deepcopy(data["snapshots"][1]["sessions"][0]["current"])),
                    (4, 0, "query", "needle"),
                    (4, 0, "current", copy.deepcopy(data["snapshots"][3]["sessions"][0]["current"])),
                    (4, 0, "needle_highlight_cells", 6),
                    (5, 0, "chinese_highlight_cells", 0),
                    (6, 0, "cancelled", False),
                    (6, 0, "source_tick_seen", False),
                    (6, 0, "terminal_focused", False),
                    (8, 0, "running", False),
                    (9, 0, "exists", True),
                    (9, 1, "terminal_focused", False)):
                bad = copy.deepcopy(data); bad["snapshots"][phase]["sessions"][session][field] = wrong
                with self.subTest(phase=phase, field=field), self.assertRaises(ValueError):
                    validate_find_stream(bad, record)
            for phase, field, wrong in ((1, "begin", 11), (5, "end", 22), (5, "version", 99999)):
                bad = copy.deepcopy(data); bad["snapshots"][phase]["sessions"][0]["current"][field] = wrong
                with self.subTest(current_phase=phase, field=field), self.assertRaises(ValueError):
                    validate_find_stream(bad, record)
            for field, wrong in (("input_path", "direct-business-callback"), ("search_work_budget", 4096),
                                 ("worker_deadline_ms", 0)):
                bad = copy.deepcopy(data); bad[field] = wrong
                with self.subTest(contract=field), self.assertRaises(ValueError):
                    validate_find_stream(bad, record)
            for field in ("closed_find_hwnd_gone", "closed_terminal_hwnd_gone"):
                bad = copy.deepcopy(data); bad["snapshots"][9][field] = False
                with self.subTest(native_closure=field), self.assertRaises(ValueError):
                    validate_find_stream(bad, record)
            for phase, replacement in ((6, copy.deepcopy(data["snapshots"][0]["sessions"][0]["tail"])),
                                        (7, {"id": 1, "version": 1, "text": "TF_STREAM_TICK_00000000"}),
                                        (8, copy.deepcopy(data["snapshots"][6]["sessions"][0]["tail"]))):
                bad = copy.deepcopy(data); target = bad["snapshots"][phase]["sessions"][0]
                if phase == 7:
                    target["lines"] = [line for line in target["lines"] if line["text"] != "TF_STREAM_STOPPED"]
                else:
                    target["tail"] = replacement; target["lines"][-1] = replacement
                with self.subTest(worker_phase=phase), self.assertRaises(ValueError):
                    validate_find_stream(bad, record)
            bad = copy.deepcopy(data); bad["snapshots"].pop()
            with self.assertRaises(ValueError):
                validate_find_stream(bad, record)
            for field, wrong in (("desktop_gone", False), ("job_after", {"active": 1}),
                                 ("cleanup_errors", ["injected output worker leak"]), ("timed_out", True)):
                bad_record = copy.deepcopy(record); bad_record[field] = wrong
                with self.subTest(cleanup=field), self.assertRaises(ValueError):
                    validate_find_stream(data, bad_record)
            for index, (bottom, main) in enumerate(((b"~y-~needle", b"x"), (b"~y-~\r", b"x"),
                                                    (b"~y-~\x1b", b"x"), (b"~yy-~", b"x"),
                                                    (b"~y-~", b"xx"), (b"~y-~", b"x\x1b"))):
                bad_out = out / f"negative-stream-bytes-{index}"; bad_out.mkdir()
                (bad_out / "find-stream.json").write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")
                (bad_out / "stream-bottom.bin").write_bytes(bottom); (bad_out / "stream-main.bin").write_bytes(main)
                with self.subTest(raw_stream_bytes=index), self.assertRaises(ValueError):
                    validate_find_stream_workflow(bad_out, record)

    @unittest.skipUnless(os.environ.get("GITHUB_ACTIONS") == "true" and
                         os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted",
                         "system copy UI gate requires a disposable hosted VM")
    def test_mouse_selection_to_real_system_clipboard(self):
        for repeat in range(2):
            out = self.out / f"copy-system-{repeat}"
            record = execute(self.exe, "ui-copy-system", out)
            data = json.loads((out / "selection.json").read_text(encoding="utf-8"))
            self.assertEqual(data["schema"], "termforge-window-selection-v5")
            self.assertEqual(data["copy_contention"]["schema"], "termforge-product-copy-contention-v2")
            self.assertTrue(record["selection_workflow"]["interactions"]["actual_copy_popup"])
            self.assertTrue(record["selection_workflow"]["interactions"]["copy_contention"])
            for field, wrong in (("actual_return_command", 15), ("target_command", 15),
                                 ("initial_state", 3), ("highlighted_state", 0), ("popup_closed", False)):
                bad = copy.deepcopy(data); bad["copy_popup_menu"][field] = wrong
                with self.subTest(actual_copy_popup=field), self.assertRaises(ValueError):
                    validate_selection(bad, True, record)
            for field, wrong in (("clipboard", "wrong copied text"), ("main_focused", False),
                                 ("raw_main", "xx"), ("raw_bottom", "x")):
                bad = copy.deepcopy(data)
                next(row for row in bad["interactions"] if row["phase"] == "system-copy-popup")[field] = wrong
                with self.subTest(copy_target=field), self.assertRaises(ValueError):
                    validate_selection(bad, True, record)
            bad = copy.deepcopy(data); bad["copy_popup_sequence_after"] = bad["copy_popup_sequence_before"]
            with self.assertRaises(ValueError): validate_selection(bad, True, record)
            for field, wrong in (("native_error", 0), ("error_visible", False), ("owner_occupied_at_error", False),
                                 ("error_status", "Selected text copied"), ("sequence_at_error", 0)):
                bad = copy.deepcopy(data); bad["copy_contention"][field] = wrong
                with self.subTest(copy_contention=field), self.assertRaises(ValueError): validate_selection(bad, True, record)
            bad = copy.deepcopy(data); bad["copy_contention"]["owner_release"]["thread_exited"] = False
            with self.assertRaises(ValueError): validate_selection(bad, True, record)

    def test_real_history_scroll_anchor_eviction_and_hidden_sessions(self):
        for repeat in range(2):
            out = self.out / f"history-{repeat}"
            execute(self.exe, "ui-history", out)
            data = json.loads((out / "history.json").read_text(encoding="utf-8"))
            gestures = [json.loads(line) for line in (out / "native-scroll.jsonl").read_text(encoding="utf-8").splitlines()]
            for field, value in (("native_command", False), ("final", 49), ("initial", 50),
                                 ("tracks", 0), ("physical_mouse", True), ("tracked", [50, 50])):
                bad = copy.deepcopy(gestures); bad[0][field] = value
                with self.subTest(gesture=field), self.assertRaises(ValueError):
                    validate_native_scroll(bad)
            # An internal success flag must not hide incorrect copied content,
            # a wrong viewport, a live caret in history, or an invalid bound.
            for index, field, value in ((0, "lines", ["wrong"]), (1, "top", 1),
                                        (5, "cursor_row", 0), (6, "anchor", 0), (8, "maximum", 101)):
                bad = copy.deepcopy(data); bad["snapshots"][index][field] = value
                with self.subTest(field=field), self.assertRaises(ValueError):
                    validate_history(bad)

    def test_keyboard_message_path_to_real_console_bytes(self):
        for repeat in range(2):
            execute(self.exe, "ui-keyboard", self.out / f"keyboard-{repeat}")

    def test_paste_confirmation_and_hidden_target_preserve_visible_focus(self):
        for repeat in range(2):
            out = self.out / f"paste-{repeat}"
            execute(self.exe, "ui-paste", out)
            data = json.loads((out / "paste-workflow.json").read_text(encoding="utf-8"))
            self.assertEqual(data["schema"], "termforge-window-paste-v2")
            self.assertEqual(data["keyboard"], {"default_enter_accept": True, "default_escape_cancel": True,
                                                "tab_enter_accept": True})
            self.assertEqual((out / "input.bin").read_bytes(), b"DEFAULT_ENTER_ACCEPT\r" + b"P\xe6\xb1\x89\r" * 5000)

    @unittest.skipUnless(os.environ.get("GITHUB_ACTIONS") == "true" and
                         os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted",
                         "real system clipboard UI gate requires a disposable hosted VM")
    def test_system_clipboard_shortcut_to_window_confirmation_and_receiver(self):
        for repeat in range(2):
            execute(self.exe, "ui-paste-system", self.out / f"paste-system-{repeat}")

    @unittest.skipUnless(os.environ.get("GITHUB_ACTIONS") == "true" and
                         os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted",
                         "product clipboard read gate requires a disposable hosted VM")
    def test_system_clipboard_product_read_errors_cancel_and_close(self):
        for repeat in range(2):
            execute(self.exe, "ui-clipboard-system", self.out / f"clipboard-system-{repeat}", timeout=60)

    def test_commands_unicode_editing_interrupt_and_normal_exit(self):
        for repeat in range(3):
            result = execute(self.exe, "normal", self.out / f"normal-{repeat}")["result"]
            for field, wrong in (("ctrl_c_verified", False), ("active_children_after", 1),
                                 ("output_queue_high_water", 262145), ("input_rejected", 1),
                                 ("max_consumed_per_tick", 65537),
                                 ("presented_done", 1)):
                broken = copy.deepcopy(result)
                broken[field] = wrong
                with self.subTest(field=field), self.assertRaises(ValueError):
                    validate(broken, "normal")

    def test_close_during_continuous_output(self):
        for repeat in range(3):
            execute(self.exe, "close", self.out / f"close-{repeat}")

    def test_resize_child_dimensions_and_interrupt(self):
        for repeat in range(3):
            row = execute(self.exe, "resize", self.out / f"resize-{repeat}")["result"]
            for field, wrong in (("resize_verified", 2), ("columns", 80), ("rows", 24)):
                broken = copy.deepcopy(row); broken[field] = wrong
                with self.subTest(field=field), self.assertRaises(ValueError):
                    validate(broken, "resize")

    def test_close_while_resizing_continuous_output(self):
        for repeat in range(3):
            execute(self.exe, "resize-close", self.out / f"resize-close-{repeat}")

    def test_watchdog_contains_real_shell_tree(self):
        execute(self.exe, "watchdog", self.out / "watchdog", timeout=3)

    def test_assignment_failure_reaps_gated_launcher_and_preserves_failure(self):
        launched = []
        popen = subprocess.Popen

        def launch(*args, **kwargs):
            process = popen(*args, **kwargs)
            launched.append(process)
            return process

        out = self.out / "assignment-failure"
        with mock.patch("experiments.shell.run.subprocess.Popen", side_effect=launch), \
                mock.patch("experiments.shell.run.OwnedJob.assign", side_effect=OSError("injected assignment failure")):
            with self.assertRaisesRegex(OSError, "injected assignment failure"):
                execute(self.exe, "normal", out)
        self.assertEqual(len(launched), 1)
        self.assertIsNotNone(launched[0].poll(), "unassigned gated launcher survived")
        record = json.loads((out / "sample.json").read_text(encoding="utf-8"))
        self.assertEqual(record["status"], "failed")
        self.assertEqual(record["error"], "injected assignment failure")
        self.assertTrue(record["desktop_gone"])
        self.assertNotIn("cleanup_errors", record)

    def test_desktop_creation_failure_closes_job_and_keeps_evidence(self):
        from experiments.conpty.run import OwnedJob
        jobs = []

        def create_job():
            job = OwnedJob()
            jobs.append(job)
            return job

        out = self.out / "desktop-failure"
        with mock.patch("experiments.shell.run.OwnedJob", side_effect=create_job), \
                mock.patch("experiments.shell.run.OwnedTestDesktop", side_effect=OSError("injected desktop failure")):
            with self.assertRaisesRegex(OSError, "injected desktop failure"):
                execute(self.exe, "normal", out)
        self.assertEqual(len(jobs), 1)
        self.assertIsNone(jobs[0].handle)
        record = json.loads((out / "sample.json").read_text(encoding="utf-8"))
        self.assertEqual(record["status"], "failed")
        self.assertEqual(record["error"], "injected desktop failure")

    def test_queues_apply_bounded_backpressure_and_reject_input(self):
        result = subprocess.run([str(binary("shell_queue_probe"))], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_invalid_arguments_create_no_session(self):
        result = subprocess.run([str(self.exe), "--test", "invalid", "unused.bmp"],
                                capture_output=True, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
        self.assertEqual(result.returncode, 2)
        self.assertIn(b"invalid shell test mode", result.stderr)

    def test_workspace_sessions_resize_and_cleanup(self):
        narrow = self.out / "narrow-workspace.layout"
        narrow.write_bytes(narrow_workspace_fixture())
        for repeat in range(3):
            row = execute(self.exe, "ui", self.out / f"ui-{repeat}",
                          settings_source=narrow if repeat == 2 else None)["result"]
            for field, wrong in (("isolation_verified", False), ("resize_verified", False),
                                 ("sessions_closed", 1), ("active_children_after", 1)):
                broken = copy.deepcopy(row); broken[field] = wrong
                with self.subTest(field=field), self.assertRaises(ValueError):
                    validate(broken, "ui")
            out = self.out / f"ui-{repeat}"
            geometry = json.loads((out / "restore-geometry.json").read_text(encoding="utf-8"))
            data = (out / "frame.bmp").read_bytes()
            offset = struct.unpack_from("<I", data, 10)[0]
            width, height = struct.unpack_from("<ii", data, 18)
            self.assertLess(height, 0)
            width, height = width, -height
            self.assertEqual([width, height], geometry["client"])
            self.assertEqual(geometry["ratios"], [1 / 6, 1 / 6, 2 / 3, .5, .5])
            self.assertTrue(geometry["sides"])
            self.assertFalse(geometry["focus_mode"])
            dpi = geometry["dpi"]
            self.assertGreater(dpi, 0)
            physical = lambda value: (value * dpi + 48) // 96
            logical = lambda value: (value * 96 + dpi // 2) // dpi
            logical_width, logical_height = logical(width), logical(height)
            left = int((logical_width - 2) / 6 + 0.5)
            center_right = logical_width - left - 1
            upper = int((max(100, logical_height - 90) - 1) * (2 / 3) + 0.5)
            points = ((physical(left), physical(100), (68, 68, 68)),
                      (physical(center_right), physical(100), (68, 68, 68)),
                      (physical(500), physical(33 + upper), (68, 68, 68)),
                      (physical(500), physical(35 + upper), (51, 51, 51)),
                      (physical(10), physical(upper + 83), (0, 122, 204)),
                      (physical(500), physical(logical_height - 28), (38, 38, 38)))
            self.assertGreaterEqual(offset, 54)
            self.assertLessEqual(offset + width * height * 4, len(data))
            for x, y, rgb in points:
                self.assertTrue(0 <= x < width and 0 <= y < height)
                b, g, r = data[offset + (y * width + x) * 4:][:3]
                self.assertEqual((r, g, b), rgb, (x, y))

    def test_workspace_rename_drag_and_fresh_process_restore(self):
        narrow = self.out / "narrow-edit-workspace.layout"
        narrow.write_bytes(narrow_workspace_fixture())
        for repeat in range(3):
            first = self.out / f"edit-{repeat}"
            execute(self.exe, "ui-edit", first, settings_source=narrow if repeat == 2 else None)
            execute(self.exe, "ui-restore", self.out / f"restore-{repeat}",
                    settings_source=first / "workspace.layout")

    def test_workspace_v2_multiple_shells_split_and_geometry_restore(self):
        def check_geometry(data, requested=(1200, 760), restoring=False):
            bounds = validate_workspace_geometry(data, requested, restoring=restoring, origin=(90, 60))
            if not restoring:
                self.assertEqual(data["window"][:2], [90, 60])
            self.assertEqual(data["ratios"], [.24, .21, .57, .38, .69])
            self.assertFalse(data["sides"])
            self.assertFalse(data["focus_mode"])
            self.assertEqual(data["focused"], "bottom")
            self.assertEqual((data["bottom_active"], data["main_active"], data["split_index"]), (0, 0, 1))
            self.assertTrue(data["split_active"])
            self.assertTrue(data["split_right_focus"])
            expected = (("bottom", "cmd", "底部 cmd", 100),
                        ("main", "cmd", "主 cmd", 150),
                        ("main", "powershell", "研究 PowerShell", 75))
            self.assertEqual(len(data["tabs"]), len(expected))
            for tab, (group, kind, title, zoom) in zip(data["tabs"], expected):
                self.assertEqual((tab["group"], tab["kind"], tab["title"], tab["zoom"]),
                                 (group, kind, title, zoom))
                self.assertTrue(tab["visible"])
                self.assertGreater(tab["rect"][2], 0)
                self.assertGreater(tab["rect"][3], 0)
                self.assertTrue(all(value > 0 for value in tab["grid"] + tab["cell"]))
            self.assertEqual(data["tabs"][1]["rect"][0] + data["tabs"][1]["rect"][2],
                             data["tabs"][2]["rect"][0])
            return bounds

        for repeat in range(2):
            first = self.out / f"workspace-v2-save-{repeat}"
            second = self.out / f"workspace-v2-restore-{repeat}"
            first_row = execute(self.exe, "ui-workspace-save", first)
            second_row = execute(self.exe, "ui-workspace-restore", second,
                                 settings_source=first / "workspace.layout")
            expected_bounds = []
            for row, out, restoring in ((first_row, first, False), (second_row, second, True)):
                self.assertNotIn("frame_sha256", row)
                self.assertFalse((out / "frame.bmp").exists())
                self.assertEqual(row["job_after"]["active"], 0)
                self.assertTrue(row["desktop_gone"])
                expected_bounds.append(check_geometry(
                    json.loads((out / "restore-geometry.json").read_text(encoding="utf-8")), restoring=restoring))
            first_bounds, second_bounds = expected_bounds
            first_geometry = json.loads((first / "restore-geometry.json").read_text(encoding="utf-8"))
            second_geometry = json.loads((second / "restore-geometry.json").read_text(encoding="utf-8"))
            invariant = lambda data: {key:value for key, value in data.items() if key not in ("window", "client", "tabs")}
            self.assertEqual(invariant(first_geometry), invariant(second_geometry))
            tab_identity = lambda data: [{key:tab[key] for key in ("group", "kind", "title", "zoom")} for tab in data["tabs"]]
            self.assertEqual(tab_identity(first_geometry), tab_identity(second_geometry))
            if first_bounds == second_bounds:
                self.assertEqual(first_geometry, second_geometry)
            first_config = (first / "workspace.layout").read_text(encoding="utf-8")
            second_config = (second / "workspace.layout").read_text(encoding="utf-8")
            bounds_names = {"window", "window-y", "window-width", "window-height"}
            without_bounds = lambda text: [line for line in text.splitlines() if line.split()[0] not in bounds_names]
            self.assertEqual(without_bounds(first_config), without_bounds(second_config))
            second_fields = dict(line.split(maxsplit=1) for line in second_config.splitlines())
            for key, expected_value in zip(("window", "window-y", "window-width", "window-height"), second_bounds):
                self.assertEqual(second_fields[key], str(expected_value))
            rows = [line.split() for line in first_config.splitlines()]
            self.assertEqual(rows[0], ["TermForge-workspace", "2"])
            self.assertEqual([row[0] for row in rows[:20]],
                             ["TermForge-workspace", "left", "right", "upper", "left_top", "right_top",
                              "sides", "focus", "window", "window-y", "window-width", "window-height",
                              "main-focused", "bottom-active", "main-active", "split-index", "split-active",
                              "split-right-focus", "bottom-count", "main-count"])
            fields = {row[0]: row[1] for row in rows[1:20]}
            ratio_names = ("left", "right", "upper", "left_top", "right_top")
            self.assertEqual({name: float(fields[name]) for name in ratio_names},
                             dict(zip(ratio_names, (.24, .21, .57, .38, .69))))
            self.assertEqual({name: value for name, value in fields.items() if name not in ratio_names},
                             {"sides": "0", "focus": "0", "window": "90", "window-y": "60",
                              "window-width": "1200", "window-height": "760", "main-focused": "0",
                              "bottom-active": "0", "main-active": "0", "split-index": "1",
                              "split-active": "1", "split-right-focus": "1", "bottom-count": "1", "main-count": "2"})
            expected = (("bottom", "cmd", None, "底部 cmd", "100"),
                        ("main", "cmd", first / "restore-cmd 目录", "主 cmd", "150"),
                        ("main", "powershell", first / "restore-powershell 空格", "研究 PowerShell", "75"))
            self.assertEqual(len(rows[20:]), 3)
            for row, (group, kind, directory, title, zoom) in zip(rows[20:], expected):
                self.assertEqual(len(row), 7)
                self.assertEqual(row[:3], ["session", group, kind])
                self.assertEqual(row[3], "-")
                decoded_directory = None if row[4] == "-" else Path(bytes.fromhex(row[4]).decode("utf-8"))
                self.assertEqual(decoded_directory, directory)
                self.assertEqual(bytes.fromhex(row[5]).decode("utf-8"), title)
                self.assertEqual(row[6], zoom)
            for forbidden in ("\narguments ", "\nhistory ", "\nenvironment ", "\nprocess ", "\npid "):
                self.assertNotIn(forbidden, first_config)
            clamped_source = self.out / f"workspace-v2-clamped-source-{repeat}"
            clamped_source.write_bytes(first_config.replace("window-width 1200\n", "window-width 65535\n").encode("utf-8"))
            self.assertNotEqual(clamped_source.read_bytes(), first_config.encode("utf-8"))
            clamped = self.out / f"workspace-v2-clamped-{repeat}"
            execute(self.exe, "ui-workspace-restore", clamped, settings_source=clamped_source)
            clamped_bounds = check_geometry(json.loads((clamped / "restore-geometry.json").read_text(encoding="utf-8")),
                                           (65535, 760), restoring=True)
            self.assertLess(clamped_bounds[2], 65535)
            clamped_config = (clamped / "workspace.layout").read_text(encoding="utf-8")
            self.assertEqual(without_bounds(clamped_config), without_bounds(first_config))
            clamped_fields = dict(line.split(maxsplit=1) for line in clamped_config.splitlines())
            for key, expected_value in zip(("window", "window-y", "window-width", "window-height"), clamped_bounds):
                self.assertEqual(clamped_fields[key], str(expected_value))

    def test_workspace_v1_empty_and_invalid_recovery(self):
        def fields(path):
            result = {}
            for line in path.read_text(encoding="utf-8").splitlines():
                key, value = line.split(maxsplit=1)
                result[key] = value
            return result

        for repeat in range(2):
            legacy = ("TermForge-layout 1\nleft 0.24\nright 0.21\nupper 0.57\n"
                      "left_top 0.38\nright_top 0.69\nsides 1\nfocus 0\n").encode("ascii")
            source = self.out / f"workspace-v1-source-{repeat}"
            source.write_bytes(legacy)
            out = self.out / f"workspace-v1-{repeat}"
            execute(self.exe, "ui-workspace-v1", out, settings_source=source)
            self.assertEqual(source.read_bytes(), legacy)
            recovered = fields(out / "workspace.layout")
            self.assertEqual(recovered["TermForge-workspace"], "2")
            self.assertEqual([float(recovered[key]) for key in ("left", "right", "upper", "left_top", "right_top")],
                             [.24, .21, .57, .38, .69])
            native = json.loads((out / "restore-geometry.json").read_text(encoding="utf-8"))
            bounds = validate_workspace_geometry(native, (1360, 860), restoring=True)
            for key, expected_value in zip(("window", "window-y", "window-width", "window-height"), bounds):
                self.assertEqual(recovered[key], str(expected_value))
            self.assertEqual((recovered["sides"], recovered["focus"], recovered["bottom-count"], recovered["main-count"]),
                             ("1", "0", "1", "0"))
            session = (out / "workspace.layout").read_text(encoding="utf-8").splitlines()[-1].split()
            self.assertEqual(session[:5], ["session", "bottom", "cmd", "-", "-"])
            self.assertEqual(bytes.fromhex(session[5]).decode("utf-8"), "cmd 1")
            self.assertEqual(session[6], "125")

            corrupt_source = self.out / f"workspace-corrupt-source-{repeat}"
            corrupt_source.write_bytes(b"TermForge-workspace 2\nleft 0.24\n")
            corrupt = self.out / f"workspace-corrupt-{repeat}"
            execute(self.exe, "ui-workspace-corrupt", corrupt, settings_source=corrupt_source)
            fallback = fields(corrupt / "workspace.layout")
            self.assertEqual(fallback["TermForge-workspace"], "2")
            self.assertEqual([float(fallback[key]) for key in ("left", "right", "upper", "left_top", "right_top")],
                             [1 / 6, 1 / 6, 2 / 3, .5, .5])
            self.assertEqual((fallback["bottom-count"], fallback["main-count"]), ("1", "0"))
            self.assertNotEqual(float(fallback["left"]), .24)
            self.assertEqual((corrupt / "workspace.layout").read_text(encoding="utf-8").splitlines()[-1].split()[-1], "100")

            future_source = self.out / f"workspace-future-source-{repeat}"
            future_source.write_bytes(b"TermForge-workspace 3\n")
            future = self.out / f"workspace-future-{repeat}"
            row = execute(self.exe, "ui-workspace-future", future, settings_source=future_source)
            self.assertTrue(row["result"]["configuration_preserved"])
            self.assertTrue(row["result"]["settings_save_failed"])
            self.assertEqual((future / "workspace.layout").read_bytes(), b"TermForge-workspace 3\n")
            self.assertEqual(list(future.glob("workspace.layout.tmp-*")), [])
            self.assertEqual(row["job_after"]["active"], 0)
            self.assertTrue(row["desktop_gone"])

    def test_workspace_v2_empty_session_roundtrip(self):
        for repeat in range(2):
            first = self.out / f"workspace-empty-create-{repeat}"
            second = self.out / f"workspace-empty-restore-{repeat}"
            execute(self.exe, "ui-workspace-empty-create", first)
            empty = (first / "workspace.layout").read_text(encoding="utf-8")
            self.assertTrue(empty.startswith("TermForge-workspace 2\n"))
            self.assertNotIn("\nsession ", empty)
            fields = dict(line.split(maxsplit=1) for line in empty.splitlines())
            self.assertEqual((fields["bottom-count"], fields["main-count"], fields["bottom-active"],
                              fields["main-active"], fields["main-focused"], fields["split-index"]),
                             ("0", "0", "-1", "-1", "0", "-1"))
            row = execute(self.exe, "ui-workspace-empty", second, settings_source=first / "workspace.layout")
            self.assertEqual(row["result"]["sessions_closed"], 0)
            self.assertEqual(row["job_after"]["active"], 0)
            self.assertTrue(row["data_only"])
            self.assertFalse((second / "frame.bmp").exists())
            self.assertEqual((second / "workspace.layout").read_text(encoding="utf-8"), empty)

    def test_workspace_write_failure_retry_and_close_discard(self):
        for repeat in range(2):
            legacy = ("TermForge-layout 1\nleft 0.24\nright 0.21\nupper 0.57\n"
                      "left_top 0.38\nright_top 0.69\nsides 1\nfocus 0\n").encode("ascii")
            source = self.out / f"writer-v1-source-{repeat}"
            source.write_bytes(legacy)
            seed = self.out / f"writer-seed-{repeat}"
            execute(self.exe, "ui-workspace-v1", seed, settings_source=source)
            original = (seed / "workspace.layout").read_bytes()
            self.assertEqual(original.splitlines()[-1].split()[-1], b"125")

            retry = self.out / f"workspace-write-retry-{repeat}"
            row = execute(self.exe, "ui-workspace-write-failure", retry,
                          settings_source=seed / "workspace.layout")
            self.assertTrue(row["result"]["retry_saved"])
            self.assertFalse(row["result"]["settings_save_failed"])
            recovered = (retry / "workspace.layout").read_bytes()
            self.assertEqual(recovered.splitlines()[-1].split()[-1], b"150")
            self.assertEqual(list(retry.glob("workspace.layout.tmp-*")), [])
            self.assertEqual(row["job_after"]["active"], 0)

            failure = self.out / f"workspace-close-failure-{repeat}"
            row = execute(self.exe, "ui-workspace-close-failure", failure,
                          settings_source=seed / "workspace.layout")
            self.assertTrue(row["result"]["configuration_preserved"])
            self.assertTrue(row["result"]["settings_save_failed"])
            self.assertTrue(row["result"]["settings_discarded"])
            self.assertEqual((failure / "original-workspace.layout").read_bytes(), original)
            self.assertTrue((failure / "workspace.layout").is_dir())
            self.assertEqual((failure / "workspace.layout" / "sentinel").read_bytes(), b"keep")
            self.assertEqual(list(failure.glob("workspace.layout.tmp-*")), [])
            self.assertFalse((failure / "frame.bmp").exists())
            self.assertEqual(row["job_after"]["active"], 0)
            self.assertTrue(row["desktop_gone"])

    def test_main_and_bottom_sessions_focus_geometry_and_cleanup(self):
        narrow = self.out / "narrow-main-workspace.layout"
        narrow.write_bytes(narrow_workspace_fixture())
        for repeat in range(3):
            out = self.out / f"main-{repeat}"
            row = execute(self.exe, "ui-main", out, settings_source=narrow if repeat == 2 else None)["result"]
            for field, wrong in (("main_sessions_verified", False), ("focus_verified", False),
                                 ("geometry_verified", False), ("sessions_closed", 3),
                                 ("ui_font_family", "Consolas"), ("presented_done", 1)):
                broken = copy.deepcopy(row)
                broken[field] = wrong
                with self.subTest(field=field), self.assertRaises(ValueError):
                    validate(broken, "ui-main")
            geometry = json.loads((out / "geometry.json").read_text(encoding="utf-8"))
            validate_main_geometry(geometry)
            for index, field in ((0, "main_group"), (1, "main_toolbar"),
                                 (1, "main_child"), (2, "main_terminal")):
                broken = copy.deepcopy(geometry)
                broken["snapshots"][index][field][2] += 1
                with self.subTest(phase=index, field=field), self.assertRaises(ValueError):
                    validate_main_geometry(broken)
            broken = copy.deepcopy(geometry)
            broken["snapshots"].pop()
            with self.assertRaises(ValueError):
                validate_main_geometry(broken)

    def test_per_session_zoom_and_controlled_dpi_transitions(self):
        for repeat in range(2):
            out = self.out / f"dpi-{repeat}"
            row = execute(self.exe, "ui-dpi", out)["result"]
            for field, wrong in (("dpi_and_zoom_verified", False), ("sessions_closed", 1),
                                 ("active_children_after", 1), ("presented_done", 1)):
                broken = copy.deepcopy(row)
                broken[field] = wrong
                with self.subTest(field=field), self.assertRaises(ValueError):
                    validate(broken, "ui-dpi")
            geometry = json.loads((out / "dpi.json").read_text(encoding="utf-8"))
            for field, wrong in (("controlled_messages", False), ("real_monitor_transition", True)):
                broken = copy.deepcopy(geometry)
                broken[field] = wrong
                with self.subTest(field=field), self.assertRaises(ValueError):
                    validate_dpi_geometry(broken)
            for phase, session, field in ((1, 0, "physical_rect"), (1, 2, "cell_pixels"),
                                           (2, 1, "grid"), (3, 2, "logical_rect")):
                broken = copy.deepcopy(geometry)
                broken["snapshots"][phase]["sessions"][session][field][-1] += 1
                with self.subTest(phase=phase, field=field), self.assertRaises(ValueError):
                    validate_dpi_geometry(broken)
            broken = copy.deepcopy(geometry)
            broken["snapshots"][1]["sessions"][0]["display_dpi"] = 96
            with self.assertRaises(ValueError):
                validate_dpi_geometry(broken)
            broken = copy.deepcopy(geometry)
            broken["snapshots"].pop()
            with self.assertRaises(ValueError):
                validate_dpi_geometry(broken)

    @unittest.skipUnless(os.name == "nt" and ctypes.windll.user32.GetSystemMetrics(80) > 1,
                         "real multi-monitor transition requires at least two monitors")
    def test_real_monitor_round_trip_preserves_workspace(self):
        for repeat in range(2):
            out = self.out / f"monitors-{repeat}"
            row = execute(self.exe, "ui-monitors", out)["result"]
            broken = copy.deepcopy(row)
            broken["real_monitor_transition"] = False
            with self.assertRaises(ValueError):
                validate(broken, "ui-monitors")
            geometry = json.loads((out / "monitors.json").read_text(encoding="utf-8"))
            for field, wrong in (("real_monitor_transition", False), ("distinct_dpi", "unknown"),
                                 ("presented_done", 1)):
                broken = copy.deepcopy(geometry)
                broken[field] = wrong
                with self.subTest(field=field), self.assertRaises(ValueError):
                    validate_monitor_geometry(broken)
            broken = copy.deepcopy(geometry)
            broken["snapshots"][1]["window_dpi"] = 0
            with self.assertRaises(ValueError):
                validate_monitor_geometry(broken)
            broken = copy.deepcopy(geometry)
            broken["snapshots"][1]["window_rect"] = [0, 0, 0, 0]
            with self.assertRaises(ValueError):
                validate_monitor_geometry(broken)
            broken = copy.deepcopy(geometry)
            broken["snapshots"][0]["sessions"][0]["visible"] = "unknown"
            with self.assertRaises(ValueError):
                validate_monitor_geometry(broken)

    def test_local_shell_launch_working_directory_and_cleanup(self):
        for repeat in range(2):
            out = self.out / f"shells-{repeat}"
            row = execute(self.exe, "ui-shells", out)["result"]
            broken = copy.deepcopy(row)
            broken["shells_verified"] = 3
            with self.assertRaises(ValueError):
                validate(broken, "ui-shells")
            evidence = json.loads((out / "shells.json").read_text(encoding="utf-8"))
            for field, wrong in (("shells", ["cmd"]), ("main_sessions", 1), ("working_directory", ""),
                                 ("normal_exit", False)):
                broken = copy.deepcopy(evidence)
                broken[field] = wrong
                with self.subTest(field=field), self.assertRaises(ValueError):
                    validate_local_shells(broken)

    def test_launch_failure_is_visible_and_recoverable(self):
        for repeat in range(2):
            out = self.out / f"launch-failure-{repeat}"
            row = execute(self.exe, "ui-launch-failure", out)["result"]
            broken = copy.deepcopy(row)
            broken["recovered"] = False
            with self.assertRaises(ValueError):
                validate(broken, "ui-launch-failure")
            evidence = json.loads((out / "launch-failure.json").read_text(encoding="utf-8"))
            for field, wrong in (("error", "unknown"), ("recovered", False), ("sessions_closed", 1)):
                broken = copy.deepcopy(evidence)
                broken[field] = wrong
                with self.subTest(field=field), self.assertRaises(ValueError):
                    validate_launch_failure(broken)

    def test_main_split_background_restore_close_and_focus(self):
        for repeat in range(2):
            out = self.out / f"split-{repeat}"
            row = execute(self.exe, "ui-split", out)["result"]
            broken = copy.deepcopy(row)
            broken["split_verified"] = False
            with self.assertRaises(ValueError):
                validate(broken, "ui-split")
            evidence = json.loads((out / "split.json").read_text(encoding="utf-8"))
            for field, wrong in (("independent_output", False), ("background_output", False),
                                 ("restored", False), ("right_closed", False), ("left_alive", False)):
                broken = copy.deepcopy(evidence)
                broken[field] = wrong
                with self.subTest(field=field), self.assertRaises(ValueError):
                    validate_main_split(broken)
            broken = copy.deepcopy(evidence)
            broken["snapshots"][0]["right_visible"] = False
            with self.assertRaises(ValueError):
                validate_main_split(broken)
            broken = copy.deepcopy(evidence)
            broken["snapshots"][0]["right_rect"][0] -= 1
            with self.assertRaises(ValueError):
                validate_main_split(broken)


class SelectionCopyPopupSchemaTests(unittest.TestCase):
    """Pure schema examples do not constitute actual system clipboard evidence."""
    @staticmethod
    def example():
        text = "\r\n".join([f"TF_FIND_{index:03d} needle 中文 e\u0301" for index in range(80)] + ["TF_FIND_READY 080", ""])
        def row(phase, action, cf):
            return {"phase": phase, "action": action, "clipboard": cf, "text": text,
                    "main_focused": True, "main_active": True, "selected": True, "frame_matches_full": True,
                    "raw_bottom": "", "raw_main": "x", "status": "ok", "captured": False, "overlay_cells": 100,
                    "product_status": "Selected text copied"}
        return {"schema": "termforge-window-selection-v4", "system_clipboard": True,
                "copy_popup_verified": True, "copy_popup_observer_idle": True,
                "copy_popup_sequence_before": 100, "copy_popup_sequence_after": 102,
                "copy_popup_menu": {"schema": "termforge-owned-popup-menu-v1", "input_path": "queued-owner-mouse-and-popup-keys",
                    "owned_non_input_desktop": True, "target_command": 14, "target_index": 9, "initial_state": 0,
                    "highlighted_state": 128, "actual_return_command": 14, "popup_hwnd": 42, "navigation_keys": 7,
                    "last_highlighted_command": 14, "popup_class": "#32768", "enabled_commands": [8, 1, 2, 6, 5, 17, 12, 14, 15, 16],
                    "popup_visible_observed": True, "highlighted": True, "returned": True, "popup_closed": True,
                    "timed_out": False, "escape_cleanup": False, "endmenu_cleanup": False, "failure": None,
                    "popup_rect": [10, 10, 300, 500], "item_rect": [10, 300, 300, 320]},
                "interactions": [row("system-copy-popup-prepared", "prepare-Copy-popup", "TF_COPY_SENTINEL 中文 e\u0301"),
                                 row("system-copy-popup", "actual-Session-popup-Copy", text)]}

    def test_old_v3_is_readable_but_never_claims_the_new_copy_popup_gate(self):
        self.assertFalse(_validate_copy_popup({"schema": "termforge-window-selection-v3", "system_clipboard": True}))
        changed = self.example(); changed["schema"] = "termforge-window-selection-v3"
        with self.assertRaises(ValueError): _validate_copy_popup(changed)
        changed = self.example(); changed["system_clipboard"] = False
        with self.assertRaises(ValueError): _validate_copy_popup(changed)

    def test_native_copy_return_highlight_cf_and_target_are_independent_gates(self):
        data = self.example(); self.assertTrue(_validate_copy_popup(data))
        for field, wrong in (("target_command", 15), ("target_index", 8), ("actual_return_command", 15),
                             ("initial_state", 3), ("highlighted_state", 0), ("last_highlighted_command", 15),
                             ("popup_hwnd", 0), ("timed_out", True), ("popup_closed", False),
                             ("enabled_commands", [8, 1, 2, 6, 5, 17, 12, 15, 16])):
            bad = copy.deepcopy(data); bad["copy_popup_menu"][field] = wrong
            with self.subTest(popup=field), self.assertRaises(ValueError): _validate_copy_popup(bad)
        for field, wrong in (("copy_popup_verified", False), ("copy_popup_observer_idle", False),
                             ("copy_popup_sequence_before", 0), ("copy_popup_sequence_after", 100),
                             ("copy_popup_sequence_after", True)):
            bad = copy.deepcopy(data); bad[field] = wrong
            with self.subTest(observation=field), self.assertRaises(ValueError): _validate_copy_popup(bad)
        for index, field, wrong in ((0, "clipboard", data["interactions"][1]["clipboard"]),
                                    (1, "clipboard", "TF_COPY_SENTINEL 中文 e\u0301"), (1, "clipboard", "你好"),
                                    (1, "action", "workspace-command-copy"), (1, "main_focused", False),
                                    (1, "main_active", False), (1, "text", ""), (1, "selected", False),
                                    (1, "raw_bottom", "x"), (1, "raw_main", "xx"), (1, "captured", True)):
            bad = copy.deepcopy(data); bad["interactions"][index][field] = wrong
            with self.subTest(row=index, field=field), self.assertRaises(ValueError): _validate_copy_popup(bad)

    @classmethod
    def contention_example(cls):
        data = cls.example(); data["schema"] = "termforge-window-selection-v5"
        failure = "Copy failed: OpenClipboard: 5"
        data["copy_contention"] = {"schema": "termforge-product-copy-contention-v1", "entry": "queued-Ctrl-Shift-C", "target": "main",
            "owner_occupied_before": True, "owner_occupied_at_error": True, "error_visible": True, "native_error": 5,
            "failure_step": "OpenClipboard", "error_frame": "selection-copy-occupied-error.bmp", "error_status": failure,
            "native_window_title": "TermForge — " + failure, "sequence_before": 200, "sequence_at_error": 200,
            "sequence_after_owner_join": 200, "sequence_after_retry": 202,
            "owner_release": {"reason": "ordinary", "proof": {field: False for field in
                ("owner_entered", "requester_exited", "target_quiescent", "payload_unchanged")},
                "publication_attempted": False, "publication_succeeded": False, "release_event": True, "thread_exited": True,
                "native_error": 0, "worker_error": 0, "failure_step": "", "secondary_failure_step": "",
                "secondary_native_error": 0, "secondary_worker_error": 0, "expected_requester_gone": False}}
        for phase, action, cf, status in (
                ("system-copy-occupied-prepared", "prepare-occupied-Copy", "TF_COPY_SENTINEL 中文 e\u0301", "Selected text copied"),
                ("system-copy-occupied", "queued-Ctrl-Shift-C-occupied", None, failure),
                ("system-copy-occupied-released", "release-and-join-occupied-owner", "TF_COPY_SENTINEL 中文 e\u0301", failure),
                ("system-copy-occupied-retry", "queued-Ctrl-Shift-C-retry", data["interactions"][1]["clipboard"], "Selected text copied")):
            row = copy.deepcopy(data["interactions"][1]); row.update(phase=phase, action=action, clipboard=cf, product_status=status)
            data["interactions"].append(row)
        return data

    def test_occupied_product_copy_keeps_original_error_sentinel_target_and_strict_join(self):
        data = self.contention_example()
        self.assertTrue(_validate_copy_popup(data))
        self.assertEqual(_validate_copy_contention(data)["native_error"], 5)
        self.assertIsNone(_validate_copy_contention(self.example()))
        bad = copy.deepcopy(data); bad["schema"] = "termforge-window-selection-v4"
        with self.assertRaises(ValueError): _validate_copy_contention(bad)
        for field, wrong in (("native_error", 0), ("native_error", True), ("error_status", "Copy failed: GlobalLock: 5"),
                             ("native_window_title", "wrong native title"), ("owner_occupied_before", False),
                             ("owner_occupied_at_error", False), ("error_visible", False), ("sequence_at_error", 201),
                             ("sequence_after_owner_join", 201), ("sequence_after_retry", 200), ("entry", "direct-WM_COPY")):
            bad = copy.deepcopy(data); bad["copy_contention"][field] = wrong
            with self.subTest(contention=field), self.assertRaises(ValueError): _validate_copy_contention(bad)
        for field, wrong in (("reason", "requester-cancelled"), ("release_event", False), ("thread_exited", False),
                             ("native_error", 1418), ("worker_error", 1418), ("expected_requester_gone", True),
                             ("secondary_native_error", 5), ("secondary_failure_step", "CloseClipboard-release")):
            bad = copy.deepcopy(data); bad["copy_contention"]["owner_release"][field] = wrong
            with self.subTest(owner_release=field), self.assertRaises(ValueError): _validate_copy_contention(bad)
        for phase, field, wrong in (("system-copy-occupied", "product_status", "Copy failed: OpenClipboard: 0"),
                                    ("system-copy-occupied", "clipboard", "unexpected read while owner held"),
                                    ("system-copy-occupied-released", "clipboard", "changed sentinel"),
                                    ("system-copy-occupied-released", "main_focused", False),
                                    ("system-copy-occupied-retry", "clipboard", "wrong copied text"),
                                    ("system-copy-occupied-retry", "raw_main", "xx"),
                                    ("system-copy-occupied-retry", "raw_bottom", "x"),
                                    ("system-copy-occupied-retry", "action", "workspace-command-copy")):
            bad = copy.deepcopy(data); next(row for row in bad["interactions"] if row["phase"] == phase)[field] = wrong
            with self.subTest(phase=phase, field=field), self.assertRaises(ValueError): _validate_copy_contention(bad)


class ClipboardWindowGateTests(unittest.TestCase):
    @unittest.skipUnless(os.name == "nt", "native clipboard guard requires the Windows binary")
    def test_native_clipboard_mode_refuses_non_hosted_before_window_creation(self):
        environment = os.environ.copy()
        environment.pop("GITHUB_ACTIONS", None)
        environment.pop("RUNNER_ENVIRONMENT", None)
        with tempfile.TemporaryDirectory() as temporary:
            frame = Path(temporary) / "must-not-create.bmp"
            result = subprocess.run([str(binary("termforge_shell")), "--test-owned", "ui-clipboard-system", str(frame)],
                                    env=environment, capture_output=True, timeout=5,
                                    creationflags=subprocess.CREATE_NO_WINDOW)
            self.assertEqual(result.returncode, 2)
            self.assertIn(b"requires a disposable GitHub-hosted VM", result.stderr)
            self.assertFalse(frame.exists())

    def test_hosted_guards_reject_missing_or_partial_identity_before_any_work(self):
        for environment in ({}, {"GITHUB_ACTIONS": "true"}, {"RUNNER_ENVIRONMENT": "github-hosted"},
                            {"GITHUB_ACTIONS": "true", "RUNNER_ENVIRONMENT": "self-hosted"}):
            with self.subTest(environment=environment), self.assertRaises(ValueError):
                require_hosted_clipboard(environment)
            for mode in ("ui-paste-system", "ui-clipboard-system", "ui-copy-system"):
                with mock.patch.dict(os.environ, environment, clear=True), mock.patch.object(Path, "mkdir") as mkdir:
                    with self.assertRaises(ValueError):
                        execute(Path("unused.exe"), mode, Path("must-not-create"))
                    mkdir.assert_not_called()
        require_hosted_clipboard({"GITHUB_ACTIONS": "true", "RUNNER_ENVIRONMENT": "github-hosted"})

    def test_clipboard_evidence_rejects_leaks_missing_phases_and_false_cleanup(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            cases = []
            for name in ("occupied", "oversize", "cancel", "close"):
                payload = b"B" * 256 + (b"" if name == "close" else b"RETRY_" + name.encode("ascii"))
                (directory / f"{name}.bin").write_bytes(payload)
                (directory / f"{name}-error.bmp").write_bytes(b"BM" + bytes(52))
                delayed = name in ("cancel", "close")
                owner = None if name == "oversize" else dict(
                    reason="requester-cancelled" if name == "cancel" else "target-closed" if name == "close" else "ordinary",
                    proof={field: delayed for field in ("owner_entered", "requester_exited", "target_quiescent", "payload_unchanged")},
                    publication_attempted=delayed, publication_succeeded=name == "close",
                    release_event=True, thread_exited=True, failure_step="SetClipboardData-render" if name == "cancel" else "",
                    native_error=1418 if name == "cancel" else 0, worker_error=1418 if name == "cancel" else 0,
                    secondary_failure_step="", secondary_native_error=0, secondary_worker_error=0,
                    expected_requester_gone=name == "cancel")
                cases.append(dict(name=name, action="product-paste-command", baseline_bytes=256, helper_pid=42,
                    receiver_bytes=len(payload), owner_entered=name in ("cancel", "close"),
                    prompt_seen=False, transfer_seen=False, other_during=True, other_after=True,
                    error_visible=name in ("occupied", "oversize"), helper_exited=True, busy_cleared=True,
                    target_closed=name == "close", retry_verified=name != "close", owner_release=owner))
            data = dict(schema="termforge-window-clipboard-v1", disposable_hosted=True, cases=cases)

            def write(value):
                (directory / "clipboard-workflow.json").write_text(json.dumps(value), encoding="utf-8")

            write(data)
            summary = validate_clipboard_workflow(directory)
            self.assertEqual(set(summary), {"occupied", "oversize", "cancel", "close"})
            self.assertFalse(summary["cancel"]["owner_release"]["publication_succeeded"])
            self.assertTrue(summary["cancel"]["owner_release"]["expected_requester_gone"])
            self.assertTrue(summary["close"]["owner_release"]["publication_succeeded"])
            mutations = [("action", "synthetic"), ("helper_pid", 0), ("helper_pid", True),
                         ("baseline_bytes", 0), ("receiver_bytes", 256), ("prompt_seen", True),
                         ("transfer_seen", True), ("other_during", False), ("other_after", False),
                         ("helper_exited", False), ("busy_cleared", False), ("retry_verified", False)]
            for field, value in mutations:
                bad = copy.deepcopy(data); bad["cases"][0][field] = value; write(bad)
                with self.subTest(field=field), self.assertRaises(ValueError):
                    validate_clipboard_workflow(directory)
            for index, field, value in ((0, "error_visible", False), (2, "owner_entered", False),
                                        (3, "target_closed", False), (3, "retry_verified", True)):
                bad = copy.deepcopy(data); bad["cases"][index][field] = value; write(bad)
                with self.subTest(index=index, field=field), self.assertRaises(ValueError):
                    validate_clipboard_workflow(directory)
            for field, value in (("reason", "ordinary"), ("release_event", False), ("thread_exited", False),
                                 ("publication_attempted", False), ("publication_succeeded", True),
                                 ("failure_step", "GlobalLock-render"), ("native_error", 5), ("native_error", True),
                                 ("worker_error", 0), ("expected_requester_gone", False),
                                 ("secondary_failure_step", "GetMessageW"), ("secondary_native_error", 6),
                                 ("secondary_worker_error", 6)):
                bad = copy.deepcopy(data); bad["cases"][2]["owner_release"][field] = value; write(bad)
                with self.subTest(owner_field=field), self.assertRaises(ValueError):
                    validate_clipboard_workflow(directory)
            for field in ("owner_entered", "requester_exited", "target_quiescent", "payload_unchanged"):
                bad = copy.deepcopy(data); bad["cases"][2]["owner_release"]["proof"][field] = False; write(bad)
                with self.subTest(missing_proof=field), self.assertRaises(ValueError):
                    validate_clipboard_workflow(directory)
            bad = copy.deepcopy(data); bad["cases"][0]["owner_release"] = copy.deepcopy(data["cases"][2]["owner_release"]); write(bad)
            with self.assertRaises(ValueError):
                validate_clipboard_workflow(directory)
            bad = copy.deepcopy(data); del bad["cases"][2]["owner_release"]; write(bad)
            with self.assertRaises(ValueError):
                validate_clipboard_workflow(directory)
            bad = copy.deepcopy(data); bad["cases"].pop(); write(bad)
            with self.assertRaises(ValueError):
                validate_clipboard_workflow(directory)
            write(data)
            original = (directory / "cancel.bin").read_bytes()
            for changed in (original + b"\x1b", original + b"LATE_MUST_NOT_SEND", original[:-1], original + b"RETRY_cancel"):
                (directory / "cancel.bin").write_bytes(changed)
                with self.subTest(payload=changed[-24:]), self.assertRaises(ValueError):
                    validate_clipboard_workflow(directory)
            (directory / "cancel.bin").write_bytes(original)
            (directory / "occupied-error.bmp").write_bytes(b"")
            with self.assertRaises(ValueError):
                validate_clipboard_workflow(directory)


if __name__ == "__main__":
    unittest.main()
