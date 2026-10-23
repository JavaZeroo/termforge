"""Real shell messages/output/frame/lifecycle regression; all windows are owned."""
import copy
import json
import os
from pathlib import Path
import subprocess
import struct
import tempfile
import unittest
from unittest import mock

from experiments.build_paths import ROOT, binary
from experiments.shell.run import execute, validate, validate_main_geometry, validate_dpi_geometry
from experiments.shell.history_checks import validate_history, validate_native_scroll


@unittest.skipUnless(os.name == "nt", "ConPTY shell requires Windows")
class ShellTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe = binary("termforge_shell")
        parent = Path(os.environ.get("TERMFORGE_TEST_ARTIFACTS", ROOT / "build/test-artifacts")) / "shell"
        parent.mkdir(parents=True, exist_ok=True)
        cls.out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))

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
            execute(self.exe, "ui-paste", self.out / f"paste-{repeat}")

    @unittest.skipUnless(os.environ.get("GITHUB_ACTIONS") == "true" and
                         os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted",
                         "real system clipboard UI gate requires a disposable hosted VM")
    def test_system_clipboard_shortcut_to_window_confirmation_and_receiver(self):
        for repeat in range(2):
            execute(self.exe, "ui-paste-system", self.out / f"paste-system-{repeat}")

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
        for repeat in range(2):
            row = execute(self.exe, "ui", self.out / f"ui-{repeat}")["result"]
            for field, wrong in (("isolation_verified", False), ("resize_verified", False),
                                 ("sessions_closed", 1), ("active_children_after", 1)):
                broken = copy.deepcopy(row); broken[field] = wrong
                with self.subTest(field=field), self.assertRaises(ValueError):
                    validate(broken, "ui")
            data = (self.out / f"ui-{repeat}" / "frame.bmp").read_bytes()
            offset = struct.unpack_from("<I", data, 10)[0]
            self.assertEqual(struct.unpack_from("<ii", data, 18), (1360, -860))
            # Independently recorded reference coordinates, not values read from
            # workspace layout code. Check separators, pane header and active tab.
            for x, y, rgb in ((226, 100, (68, 68, 68)), (1133, 100, (68, 68, 68)),
                              (500, 546, (68, 68, 68)), (500, 548, (51, 51, 51)),
                              (10, 596, (0, 122, 204)), (500, 832, (38, 38, 38))):
                b, g, r = data[offset + (y * 1360 + x) * 4:][:3]
                self.assertEqual((r, g, b), rgb, (x, y))

    def test_workspace_rename_drag_and_fresh_process_restore(self):
        for repeat in range(2):
            first = self.out / f"edit-{repeat}"
            execute(self.exe, "ui-edit", first)
            execute(self.exe, "ui-restore", self.out / f"restore-{repeat}",
                    settings_source=first / "workspace.layout")

    def test_main_and_bottom_sessions_focus_geometry_and_cleanup(self):
        for repeat in range(2):
            out = self.out / f"main-{repeat}"
            row = execute(self.exe, "ui-main", out)["result"]
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


if __name__ == "__main__":
    unittest.main()
