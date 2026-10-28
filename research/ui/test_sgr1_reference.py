"""Public SGR1 plan and scalar-analysis seams; never launch the original."""
from __future__ import annotations
import copy
import hashlib
import json
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
BG, FG = 0xff101010, 0xffeeeeee
NORMAL, HEAVY = [BG, FG, BG, FG], [FG, FG, BG, FG]


def roi(group, sample, pixels, shape=(2, 2), background=None):
    from research.ui.raster_raw_analysis import classify_pixels
    return {"group": group, "sample": sample, "h_rect": [0, 0, *shape],
            "space_rect": [0, 0, *shape], **classify_pixels(pixels, background or [BG] * len(pixels))}


def frame(pixels=HEAVY):
    return [roi("sample", 0, pixels), roi("sample", 1, pixels),
            roi("control-normal", 0, NORMAL), roi("control-normal", 1, NORMAL),
            roi("control-sgr1", 0, HEAVY), roi("control-sgr1", 1, HEAVY)]


def capture_fixture(root):
    """Create synthetic external records, not a replay of original pixels."""
    from research.ui.test_raster_raw_reference import fixture as raw_fixture
    from research.ui.sgr1_reference import SOURCE_FILES, AUDIT_FILES, TARGETS, phase_records, build_probe_fragment
    directory, plan_path, data = raw_fixture(root)
    result = data.pop("raster-raw-result.json")
    result.update(status="captured-five-sgr1-raster-phases", reason=None)
    records = phase_records()
    for index, capture in enumerate(result["captures"]):
        capture.update(label=TARGETS[index]["label"], rois=[])
        capture["image"].update(viewport_width=128, width=128, viewport_height=2, height=2, grab_rect=[0, 0, 127, 1])
        labels = list(dict.fromkeys(row["label"] for row in TARGETS[index]["markers"]))
        line = " ".join(labels)
        terminal = capture["terminal"]
        terminal.update(lines=[line], line_count=1, markers=[], is_alt=index in (1, 2, 3), is_main=index not in (1, 2, 3),
                        caret_rect={"x": 128, "y": 0, "width": 0, "height": 2})
        for ordinal, descriptor in enumerate(TARGETS[index]["markers"]):
            label, group, sample = [descriptor[key] for key in ("label", "group", "sample")]
            column = line.index(label) + descriptor["offset"]
            h, space = [ordinal*4+2, 0, 2, 2], [ordinal*4, 0, 2, 2]
            terminal["markers"].append({"ordinal": ordinal, "label": label, "group": group, "sample": sample,
                                        "document_line": 0, "column": column, "position": 100+column,
                                        "character": "H", "preceding_character": " ",
                                        "h_rect": dict(zip(("x", "y", "width", "height"), h)),
                                        "space_rect": dict(zip(("x", "y", "width", "height"), space))})
            pixels = HEAVY if group == "control-sgr1" or (group == "sample" and index == 4) else NORMAL
            capture["rois"].append({"ordinal": ordinal, "group": group, "sample": sample, "h_rect": h, "space_rect": space,
                                    "h_pixels": pixels.copy(), "space_pixels": [BG]*4})
        records[index]["cursor_reply"] = capture["cursor_reply"]
    data["sgr1-raster-result.json"] = result
    data["cleanup.json"] = {"active": 0, "total": 3, "terminated": 0}
    data["peer.json"]["records"] = records
    template = (ROOT / "research/ui/qt_r1_text.js").read_text(encoding="utf-8").split("// Scenario driver", 1)[0] + build_probe_fragment()
    data["run.json"]["template_sha256"] = hashlib.sha256(template.encode()).hexdigest()
    plan = json.loads(plan_path.read_text())
    plan.update(issue=146, case_id="sgr1-capture-v1", execution_authorized=True, phases=phase_records(),
                rea_prepared_identity={"version": "6.7.0", "shim_sha256": "a"*64, "package_json_sha256": "b"*64, "entry_sha256": "c"*64},
                source_files_sha256={name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in SOURCE_FILES},
                offline_audit_files_sha256={name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in AUDIT_FILES},
                generated_capture_sha256=hashlib.sha256(build_probe_fragment().encode()).hexdigest())
    plan_path.write_text(json.dumps(plan), encoding="utf-8")
    write_capture_records(directory, data)
    return directory, plan_path, data


def write_capture_records(directory, data):
    captured = data["sgr1-raster-result.json"]
    messages = [{"type": "send", "payload": {"kind": "sgr1_raster_phase", **copy.deepcopy(row)}} for row in captured["captures"]]
    messages.append({"type": "send", "payload": {"kind": "sgr1_raster_result", **copy.deepcopy(captured)}})
    data["evidence.json"]["data"]["evidence"]["normalized_result"]["messages"] = messages
    for name, value in data.items():
        (directory/name).write_text(json.dumps(value), encoding="utf-8")


class Sgr1ReferenceTests(unittest.TestCase):
    def test_distinct_repeated_same_frame_controls_classify_exact_ordered_sample(self):
        from research.ui.sgr1_analysis import classify_frame
        self.assertEqual(classify_frame(frame())["unique_control"], "SGR1-control-output")
        self.assertEqual(classify_frame(frame(NORMAL))["unique_control"], "SGR0-control-output")

    def test_indistinguishable_controls_remain_unknown(self):
        from research.ui.sgr1_analysis import classify_frame
        values = frame(NORMAL)
        values[-2:] = [roi("control-sgr1", n, NORMAL) for n in (0, 1)]
        result = classify_frame(values)
        self.assertIsNone(result["unique_control"])
        self.assertEqual(result["reason"], "controls-indistinguishable")

    def test_equal_histograms_with_different_pixel_order_cannot_pass_repeat_gate(self):
        from research.ui.sgr1_analysis import classify_frame
        values = frame()
        values[3] = roi("control-normal", 1, [FG, BG, FG, BG])
        self.assertEqual(values[2]["h"]["histogram"], values[3]["h"]["histogram"])
        self.assertEqual(classify_frame(values)["reason"], "control-repeat-differs")

    def test_color_difference_and_tied_ink_preserve_exact_output_fact_without_weight_claim(self):
        from research.ui.sgr1_analysis import classify_frame
        for pixels in ([0xffdddddd] * 4, [0xffdddddd, FG, 0xffdddddd, FG]):
            values = frame(pixels)
            values[-2:] = [roi("control-sgr1", n, pixels) for n in (0, 1)]
            result = classify_frame(values)
            self.assertEqual(result["unique_control"], "SGR1-control-output")
            self.assertFalse(result["internal_bold_verified"])
            self.assertIsNone(result["font_weight_mechanism"])
            self.assertIn(result["control_color_relation"]["dominant_ink"], ("different", "unknown"))

    def test_dimensions_missing_ink_and_sample_disagreement_do_not_infer_output_match(self):
        from research.ui.sgr1_analysis import classify_frame
        cases = []
        values = frame(); values[-1] = roi("control-sgr1", 1, HEAVY, (1, 4)); cases.append(values)
        values = frame(); values[0] = roi("sample", 0, [BG] * 4); cases.append(values)
        values = frame(); values[1] = roi("sample", 1, NORMAL); cases.append(values)
        values = frame(); values[3] = roi("control-normal", 1, NORMAL, background=[BG, FG, BG, BG]); cases.append(values)
        for values in cases:
            with self.subTest(values=values):
                self.assertIsNone(classify_frame(values)["unique_control"])

    def test_partial_or_duplicated_control_identity_is_rejected(self):
        from research.ui.sgr1_analysis import classify_frame
        for values in (frame()[:-1], frame() + [frame()[-1]], [*frame()[:-1], frame()[-2]]):
            with self.subTest(values=values), self.assertRaises(ValueError):
                classify_frame(values)

    def test_false_plan_cannot_create_attempt_or_target_directory(self):
        from research.ui.sgr1_reference import run_plan
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "proposal.json"
            path.write_text(json.dumps({"issue": 146, "case_id": "sgr1-capture-v1", "maximum_original_runs": 1,
                                        "execution_authorized": False}))
            with self.assertRaisesRegex(ValueError, "not been authorized"):
                run_plan(path)
            self.assertFalse(path.with_suffix(".attempted").exists())
            self.assertEqual([p.name for p in path.parent.iterdir()], ["proposal.json"])

    def test_five_sources_and_generated_native_capture_keep_old_template_unchanged(self):
        from research.ui.sgr1_reference import build_probe_fragment, phases, TARGETS
        old = (ROOT / "research/ui/raster_raw_reference.js").read_bytes()
        script = build_probe_fragment()
        self.assertIn("sgr1_raster_phase", script)
        self.assertIn("sgr1-raster-result.json", script)
        self.assertIn("Date.now()-started<=41000", script)
        self.assertIn("bounded-observation-timer'),42000", script)
        self.assertEqual([len(row["markers"]) for row in TARGETS], [4, 6, 6, 6, 6])
        sources = [data for _, data in phases(25, 91)]
        self.assertEqual(len(sources), 5)
        self.assertTrue(sources[0].endswith(b"\x1b[1mB0 H H"))
        self.assertTrue(sources[1].startswith(b"\x1b[?1049hE0 H H"))
        self.assertTrue(sources[2].endswith(b"\x1b[1mAB H H"))
        self.assertTrue(sources[3].startswith(b"\x1b[?1049hR0 H H"))
        self.assertTrue(sources[3].endswith(b"\x1b[0m"))
        self.assertTrue(sources[4].startswith(b"\x1b[?1049l M1 H H"))
        self.assertEqual((ROOT / "research/ui/raster_raw_reference.js").read_bytes(), old)

    def test_structural_auditor_does_not_mutate_old_case_tables(self):
        from research.ui import raster_raw_analysis
        before = copy.deepcopy((raster_raw_analysis.MARKERS, raster_raw_analysis.GROUPS))
        from research.ui import sgr1_analysis
        self.assertEqual((raster_raw_analysis.MARKERS, raster_raw_analysis.GROUPS), before)
        self.assertIsNot(sgr1_analysis._audit_capture.__globals__, raster_raw_analysis.audit_capture.__globals__)

    @unittest.skipUnless(shutil.which("node"), "Node required for the synthetic native boundary")
    def test_generated_capture_retains_reviewed_native_abi_and_failure_cleanup(self):
        from research.ui.sgr1_reference import build_probe_fragment
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "generated.js"
            source.write_text(build_probe_fragment(), encoding="utf-8", newline="\n")
            result = subprocess.run(["node", str(ROOT / "research/ui/test_sgr1_native_contract.js"), str(source)],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_full_synthetic_capture_exports_identical_bytes_under_normal_and_optimized(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); directory, plan, _ = capture_fixture(root)
            outputs = []
            for optimized in (False, True):
                destination = root / ("optimized.json" if optimized else "normal.json")
                process = subprocess.run([sys.executable, *(["-O"] if optimized else []), str(ROOT / "research/ui/export_sgr1_reference.py"),
                                          "--directory", str(directory), "--plan", str(plan), "--destination", str(destination)],
                                         capture_output=True, text=True, timeout=10)
                self.assertEqual(process.returncode, 0, process.stderr)
                outputs.append(destination.read_bytes())
            self.assertEqual(outputs[0], outputs[1])
            public = json.loads(outputs[0])
            self.assertTrue(public["raw_capture_complete"])
            self.assertEqual(public["observed_phase_count"], 5)
            self.assertEqual([row["unique_control"] for row in public["same_frame_signature_comparisons"]],
                             [None, "SGR0-control-output", "SGR0-control-output", "SGR0-control-output", "SGR1-control-output"])
            self.assertIsNone(public["presented_done"])
            self.assertFalse(public["internal_bold_verified"])
            self.assertNotIn("h_pixels", outputs[0].decode())
            self.assertNotIn("space_pixels", outputs[0].decode())

    def test_aliases_tiny_outside_roi_and_private_text_are_rejected_without_output(self):
        changes = [lambda c: c["terminal"]["markers"][1].update(position=c["terminal"]["markers"][0]["position"]),
                   lambda c: c["terminal"]["markers"][1]["h_rect"].update(x=c["terminal"]["markers"][0]["h_rect"]["x"]),
                   lambda c: c["rois"][0]["h_rect"].__setitem__(2, 0),
                   lambda c: c["rois"][0]["h_rect"].__setitem__(0, 128),
                   lambda c: c["terminal"]["lines"].__setitem__(0, "PRIVATE-HOST-TEXT")]
        from research.ui.export_sgr1_reference import export
        for index, change in enumerate(changes):
            with self.subTest(case=index), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary); directory, plan, data = capture_fixture(root)
                change(data["sgr1-raster-result.json"]["captures"][1])
                write_capture_records(directory, data)
                destination = root / "public/result.json"
                with self.assertRaises(ValueError):
                    export(directory, plan, destination)
                self.assertFalse(destination.parent.exists())

    def test_partial_capture_retains_observed_prefix_without_synthesizing_later_frames(self):
        from research.ui.export_sgr1_reference import export
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); directory, plan, data = capture_fixture(root)
            captured = data["sgr1-raster-result.json"]
            captured.update(status="partial-sgr1-raster-capture", reason="selection-endpoints-unavailable")
            captured["captures"] = captured["captures"][:2]
            capture = captured["captures"][-1]
            capture.update(capture_valid=False, reason="selection-endpoints-unavailable", image=None, rois=[],
                           constructed=[], destroyed=[], grab_attempted=False)
            capture["terminal"]["selection_anchor"] = None
            capture["terminal"]["selection_caret"] = None
            captured["grab_phases"] = [0]
            data["peer.json"]["records"] = data["peer.json"]["records"][:2]
            data["peer.json"]["received_hex"] = "1b5b313b3152" * 2
            data["peer.json"]["complete"] = False
            (directory / "ack-1").unlink()
            write_capture_records(directory, data)
            destination = root / "public.json"
            public = export(directory, plan, destination)
            self.assertFalse(public["raw_capture_complete"])
            self.assertEqual(public["observed_phase_count"], 2)
            self.assertIsNone(public["phases"][-1]["sgr1_signature_comparison"])

    def test_stop_before_source_freeze_does_not_fabricate_a_grid_or_frame(self):
        from research.ui.export_sgr1_reference import export
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); directory, plan, data = capture_fixture(root)
            data["sgr1-raster-result.json"].update(status="partial-sgr1-raster-capture", reason="first-run-guard", captures=[], grab_phases=[])
            peer = data["peer.json"]
            peer.update(records=[], received_hex="", complete=False, columns=None, rows=None, naws_history=[])
            peer.pop("frozen_grid"); peer.pop("freeze_event_count")
            write_capture_records(directory, data)
            public = export(directory, plan, root / "public.json")
            self.assertEqual(public["observed_phase_count"], 0)
            self.assertIsNone(public["frozen_grid"])
            self.assertIsNone(public["freeze_event_count"])
            self.assertFalse(public["raw_capture_complete"])

    @unittest.skipUnless(hasattr(socket, "SO_EXCLUSIVEADDRUSE"), "Windows exclusive-bind peer")
    def test_byte_fragmented_peer_delivers_only_fixed_five_sources_after_each_ack(self):
        from research.ui.sgr1_reference import Peer, phases
        class OneByteConnection:
            def __init__(self, connection): self.connection = connection
            def __getattr__(self, name): return getattr(self.connection, name)
            def recv(self, count): return self.connection.recv(min(count, 1))
        class OneByteListener:
            def __init__(self, listener): self.listener = listener
            def __getattr__(self, name): return getattr(self.listener, name)
            def accept(self):
                connection, address = self.listener.accept()
                return OneByteConnection(connection), address
        class FragmentedPeer(Peer):
            def _serve(self):
                self.listener = OneByteListener(self.listener)
                super()._serve()
        def read_exact(client, count):
            result = bytearray()
            while len(result) < count:
                part = client.recv(count-len(result))
                self.assertTrue(part, "Peer closed before full fixed source")
                result.extend(part)
            return bytes(result)
        def wait_until(predicate):
            deadline = time.monotonic()+2
            while not predicate() and time.monotonic() < deadline:
                time.sleep(.005)
            self.assertTrue(predicate(), "Bounded synthetic peer condition failed")
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary); peer = FragmentedPeer(directory)
            try:
                with socket.create_connection(("127.0.0.1", peer.port), timeout=2) as client:
                    self.assertEqual(read_exact(client, 9), bytes([255,251,1,255,251,3,255,253,31]))
                    client.sendall(bytes([255,250,31,0,91,0,25,255,240]))
                    (directory / "fixture-ready").write_text("synthetic-ready")
                    for index, (_, source) in enumerate(phases(25, 91)):
                        self.assertEqual(read_exact(client, len(source)+4), source+b"\x1b[6n")
                        client.sendall(b"\x1b[1;1R")
                        wait_until(lambda: peer.state["records"][index].get("cursor_reply") == [1, 1])
                        self.assertEqual(peer.state["phase"], index)
                        (directory / f"ack-{index}").write_text("captured")
                    wait_until(lambda: peer.state.get("complete"))
            finally:
                peer.close()
            self.assertIsNone(peer.state.get("error"))
            self.assertEqual(peer.state["frozen_grid"], [91, 25])
            self.assertEqual(peer.state["naws_history"], [[91, 25]])
            self.assertEqual(peer.state["freeze_event_count"], 1)
            self.assertEqual(len(peer.state["records"]), 5)


if __name__ == "__main__":
    unittest.main()
