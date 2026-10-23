"""Portable synthetic UI evidence tests; no WindTerm artifact or process needed."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from research.ui import summarize

EVIDENCE = "ev_" + "1" * 64
FONT = {"family": "Roboto Mono", "resolved_family": "Roboto Mono", "point_size": 9,
        "pixel_size": -1, "resolved_point_size": 9, "resolved_pixel_size": 12, "bold": 0, "exact_match": 0}
DPI = {"logical": [96, 96], "physical": [109, 109], "dpr": 1}


def fixture():
    script = b"// synthetic probe, never executed\n"
    digest = hashlib.sha256(script).hexdigest()
    run = {"pid": 123, "sha256": summarize.EXPECTED, "files_sha256": summarize.EXPECTED_FILES,
           "script_sha256": digest, "probe": "C:/private-user/probe.js", "home": "owned-empty",
           "profile": "synthetic-local-only", "desktop": "owned-non-input", "probe_kind": "typography",
           "qt_scale_factor": None}
    messages = [{"type": "send", "payload": {"kind": "probe_ready"}}]
    for phase in summarize.PHASES:
        width, height = (1360, 860) if "1360" in phase else (2048, 1120)
        connected = phase.startswith("session-")
        rows = []

        def widget(pointer, parent, cls, name, rect, **extra):
            rows.append({"ptr": hex(pointer), "parent": hex(parent), "class": cls, "name": name,
                         "rect": rect, "font": deepcopy(FONT), "dpi": deepcopy(DPI), **extra})

        widget(1, 0, "MainWindow", "", [256, 170, 256 + width - 1, 170 + height - 1])
        widget(2, 1, "QStackedWidget", "", [0, 0, width - 1, height - 1])
        widget(3, 2, "TerminalWindow", "TerminalWindow", [0, 0, width - 1, height - 1])
        widget(4, 3, "KitGroup", "", [200, 33, width - 201, 699])
        widget(5, 4, "TabGroup", "ViewGroup", [0, 0, width - 401, 25 if connected else 18])
        widget(6, 5, "TabBar", "", [0, 0 if connected else 9, width - 461, 25 if connected else 8],
               tabs=[[0, 0, 116, 25]] if connected else [])
        widget(7, 4, "QToolBar", "ToolBar", [0, 26 if connected else 19, width - 401, 59])
        widget(8, 7, "AddressBar", "AddressBar", [103, 4, width - 421, 29])
        widget(9, 3, "ShellPane", "ShellPane", [0, 710, width - 1, height - 61])
        widget(10, 3, "QMenuBar", "MenuBar", [0, 0, width - 1, 32])
        widget(11, 3, "QuickBar", "QuickBar", [0, height - 56, width - 1, height - 30])
        widget(12, 3, "StatusBar", "StatusBar", [0, height - 29, width - 1, height - 1])
        if connected:
            widget(13, 4, "TerminalView", "TerminalView", [0, 60, width - 401, 650])
            widget(14, 13, "ScintillaEditBase", "", [0, 0, width - 401, 566])
            widget(15, 14, "QWidget", "qt_scrollarea_viewport", [1, 1, width - 418, 565])
        # Unknown extra data simulates a private value; whitelist export must ignore it.
        rows[0]["text"] = "private-session-label"
        snapshot = {"kind": "snapshot", "phase": phase, "first_run_import_flag": 0,
                    "total_widgets": len(rows), "widgets": rows,
                    "screen": {"dpr": 1, "logical_dpi_x": 96, "physical_dpi_x": 109}}
        messages.append({"type": "send", "payload": snapshot})
        if connected:
            draw = deepcopy(FONT)
            draw.update(family="Consolas", resolved_family="RobotoMono Nerd Font", point_size=10,
                        resolved_point_size=9.75, resolved_pixel_size=13)
            messages.append({"type": "send", "payload": {"kind": "text_draw_font", "phase": phase,
                             "window_size": [width, height],
                             "widget": {"ptr": "0xf", "class": "QWidget", "name": "qt_scrollarea_viewport"}, "font": draw}})
    messages.append({"type": "send", "payload": {"kind": "scenario_complete"}})
    record = {"evidence_id": EVIDENCE, "operation": "instrument_with_frida",
              "parameters": {"mode": "attach", "pid": 123, "device_id": "local"},
              "normalized_result": {"mode": "attach", "pid": 123, "device_id": "local", "source_kind": "file",
                                    "source_path": run["probe"], "source_sha256": digest, "cleanup_error": None,
                                    "messages_truncated": False, "messages": messages}}
    return {"run.json": run, "cleanup.json": {"active": 0},
            "profile-check.json": {"only_synthetic_session": True, "session_count": 1},
            "evidence.json": {"records": [{"evidence_id": "ev_" + "2" * 64, "private": "ignored-record"}, record]},
            "probe.js": script}


class TypographySummaryTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name)
        self.data = fixture()

    def run_summary(self):
        for name, value in self.data.items():
            if isinstance(value, bytes):
                (self.root / name).write_bytes(value)
            else:
                (self.root / name).write_text(json.dumps(value), encoding="utf-8")
        return summarize.summarize_run(self.root, EVIDENCE)

    def normalized(self):
        return self.data["evidence.json"]["records"][1]["normalized_result"]

    def snapshot(self, phase="session-1360"):
        return next(m["payload"] for m in self.normalized()["messages"] if m["payload"].get("phase") == phase and
                    m["payload"]["kind"] == "snapshot")

    def row(self, pointer):
        return next(w for w in self.snapshot()["widgets"] if w["ptr"] == hex(pointer))

    def test_normalization_and_privacy_allowlist(self):
        summary = self.run_summary()
        self.assertEqual([p["phase"] for p in summary["phases"]], list(summarize.PHASES))
        phase = summary["phases"][2]
        self.assertEqual(phase["roles"]["MainWindow"]["rect"], [0, 0, 1360, 860])
        self.assertEqual(phase["roles"]["MainAddressBar"]["rect"], [303, 63, 837, 26])
        self.assertEqual(phase["roles"]["TerminalViewport"]["rect"], [201, 94, 942, 565])
        self.assertEqual(phase["roles"]["MainTabBar"]["tab_rects"], [[200, 33, 117, 26]])
        self.assertEqual(phase["roles"]["TerminalViewport"]["text_draw_fonts"][0]["resolved_family"], "RobotoMono Nerd Font")
        serialized = json.dumps(summary)
        for private in ("private-user", "private-session-label", "ignored-record", "source_path", '"pid"', '"ptr"', '"parent"', "0xf"):
            self.assertNotIn(private, serialized)
        self.assertEqual(summary["scope"], "ui_geometry_and_font_observation")

    def test_bad_identity_and_scope(self):
        for filename, key, value in (("run.json", "sha256", "0" * 64), ("run.json", "files_sha256", "0" * 64),
                                     ("run.json", "script_sha256", "0" * 64), ("run.json", "pid", 456),
                                     ("run.json", "profile", "host"), ("cleanup.json", "active", 1),
                                     ("profile-check.json", "only_synthetic_session", False),
                                     ("profile-check.json", "session_count", 2)):
            with self.subTest(key=key):
                self.data = fixture()
                self.data[filename][key] = value
                with self.assertRaises(ValueError):
                    self.run_summary()
        self.data = fixture()
        self.data["probe.js"] += b"// changed"
        with self.assertRaisesRegex(ValueError, "script hash"):
            self.run_summary()

    def test_bad_instrument_identity_and_incomplete_messages(self):
        for key, value in (("source_sha256", "0" * 64), ("source_path", "C:/other/probe.js"), ("pid", 456),
                           ("messages_truncated", True), ("cleanup_error", "synthetic failure")):
            with self.subTest(key=key):
                self.data = fixture()
                self.normalized()[key] = value
                with self.assertRaises(ValueError):
                    self.run_summary()
        for message in ({"type": "error", "description": "synthetic error"},
                        {"type": "send", "payload": {"error": "synthetic error"}}):
            self.data = fixture()
            self.normalized()["messages"].insert(1, message)
            with self.assertRaises(ValueError):
                self.run_summary()
        self.data = fixture()
        self.normalized()["messages"].pop()
        with self.assertRaisesRegex(ValueError, "Incomplete UI scenario"):
            self.run_summary()

    def test_missing_duplicate_phase_and_import_flag(self):
        self.normalized()["messages"].remove(next(m for m in self.normalized()["messages"] if m["payload"].get("phase") == "empty-1360"))
        with self.assertRaisesRegex(ValueError, "phase"):
            self.run_summary()
        self.data = fixture()
        self.normalized()["messages"].insert(2, deepcopy(self.normalized()["messages"][1]))
        with self.assertRaisesRegex(ValueError, "phase"):
            self.run_summary()
        self.data = fixture()
        self.snapshot()["first_run_import_flag"] = 1
        with self.assertRaisesRegex(ValueError, "import guard"):
            self.run_summary()

    def test_parent_chain_gates(self):
        for parent in ("0xffff", "0xe", "0xf"):
            with self.subTest(parent=parent):
                self.data = fixture()
                self.row(14)["parent"] = parent
                with self.assertRaisesRegex(ValueError, "parent chain"):
                    self.run_summary()

    def test_geometry_tab_and_session_gates(self):
        for mutation in (lambda: self.row(3)["rect"].__setitem__(2, 1358),
                         lambda: self.row(6).__setitem__("tabs", []),
                         lambda: self.row(6).__setitem__("tabs", [[0, 0, 2000, 25]]),
                         lambda: self.row(13).__setitem__("class", "QWidget"),
                         lambda: self.row(14).__setitem__("class", "MessageBar"),
                         lambda: self.row(15).__setitem__("rect", [1, 1, -2, 100]),
                         lambda: self.row(8).__setitem__("rect", [2000, 0, 2100, 25])):
            self.data = fixture()
            mutation()
            with self.assertRaises(ValueError):
                self.run_summary()

    def test_font_must_be_matched_and_reviewed(self):
        self.normalized()["messages"] = [m for m in self.normalized()["messages"] if m["payload"]["kind"] != "text_draw_font"]
        with self.assertRaisesRegex(ValueError, "text draw"):
            self.run_summary()
        for key, value in (("family", "C:/private-user/font"), ("point_size", float("nan"))):
            self.data = fixture()
            self.row(15)["font"][key] = value
            with self.assertRaises(ValueError):
                self.run_summary()

    def test_draw_font_requires_actual_phase_window_dimensions(self):
        for phase, valid_size in (("session-1360", [1360, 860]), ("session-2048", [2048, 1120])):
            for wrong in (None, [1360, 859], [1360], [1360.0, 860], "1360x860"):
                with self.subTest(phase=phase, wrong=wrong):
                    self.data = fixture()
                    draw = next(m["payload"] for m in self.normalized()["messages"]
                                if m["payload"]["kind"] == "text_draw_font" and m["payload"]["phase"] == phase)
                    if wrong is None:
                        del draw["window_size"]
                    else:
                        draw["window_size"] = wrong
                    with self.assertRaisesRegex(ValueError, "Text draw window dimensions"):
                        self.run_summary()
            self.data = fixture()
            draw = next(m["payload"] for m in self.normalized()["messages"]
                        if m["payload"]["kind"] == "text_draw_font" and m["payload"]["phase"] == phase)
            draw["window_size"] = [2048, 1120] if valid_size == [1360, 860] else [1360, 860]
            with self.assertRaisesRegex(ValueError, "Text draw window dimensions"):
                self.run_summary()

    def test_selected_record_is_unique(self):
        self.data["evidence.json"]["records"].append(deepcopy(self.data["evidence.json"]["records"][1]))
        with self.assertRaisesRegex(ValueError, "duplicate evidence"):
            self.run_summary()

    def test_optional_process_names_do_not_claim_shell_health(self):
        self.assertIsNone(self.run_summary()["observed_process_names"])
        self.data["process-tree.json"] = [
            {"ProcessId": 124, "ParentProcessId": 123, "Name": "winpty-agent.exe"},
            {"ProcessId": 125, "ParentProcessId": 124, "Name": "cmd.exe"}]
        result = self.run_summary()
        self.assertEqual(result["observed_process_names"], ["cmd.exe", "winpty-agent.exe"])
        self.assertNotIn("shell_healthy", result)
        self.data["process-tree.json"][0]["ParentProcessId"] = 125
        with self.assertRaisesRegex(ValueError, "ancestry"):
            self.run_summary()

    def test_scale_factor_does_not_fix_tab_width(self):
        self.data["run.json"]["qt_scale_factor"] = "1.5"
        self.row(6)["tabs"][0][2] = 119
        self.row(6)["dpi"]["dpr"] = 1.5
        result = self.run_summary()
        self.assertEqual(result["phases"][2]["roles"]["MainTabBar"]["tab_rects"][0][2], 120)


if __name__ == "__main__":
    unittest.main()
