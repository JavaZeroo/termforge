"""Synthetic external recordings exercise the publication boundary, without REA."""
from __future__ import annotations
import copy
from contextlib import ExitStack
import hashlib
import json
from pathlib import Path, PureWindowsPath
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from research.ui import attribute_state_reference as probe
from research.ui import export_attribute_state_reference as exporter
from research.ui.test_sgr1_reference import capture_fixture, NORMAL, HEAVY, BG

ROOT = exporter.ROOT


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value), encoding="utf-8")


def sync(directory, data):
    captured = data[probe.RESULT_FILE]
    attribute = captured["captures"][0]["label"].split()[0] if captured["captures"] else "underline"
    payloads = [{"kind": "probe_ready", "station_isolated": True, "scenario": probe.case_id(attribute),
                 "module_guards": captured["module_guards"]},
                {"kind": "readiness", "phase": "ui", "ready": True, "has_tree": True, "has_engine": True,
                 "has_editor": False, "peer_ready": True},
                *[{"kind": "attribute_state_raster_phase", **copy.deepcopy(row)} for row in captured["captures"]],
                {"kind": "attribute_state_raster_result", **copy.deepcopy(captured)}]
    normalized = data["evidence.json"]["data"]["evidence"]["normalized_result"]
    normalized["messages"] = [{"type": "send", "script_id": "synthetic-script", "payload": row} for row in payloads]
    for name, value in data.items():
        write(directory / name, value)


def fixture(root, attribute="underline"):
    directory, plan_path, data = capture_fixture(root)
    captured = data.pop("sgr1-raster-result.json")
    captured.update(status=probe.COMPLETE, reason=None)
    for index, capture in enumerate(captured["captures"]):
        target = probe.targets(attribute)[index]
        capture["label"] = target["label"]
        labels = list(dict.fromkeys(row["label"] for row in target["markers"]))
        line = " ".join(labels)
        terminal = capture["terminal"]
        terminal["lines"] = [line]
        for marker, roi, descriptor in zip(terminal["markers"], capture["rois"], target["markers"]):
            marker.update(label=descriptor["label"], group=descriptor["group"], sample=descriptor["sample"])
            marker["column"] = line.index(descriptor["label"])+descriptor["offset"]
            marker["position"] = 100+marker["column"]
            roi.update(group=descriptor["group"], sample=descriptor["sample"])
    data[probe.RESULT_FILE] = captured
    records = probe.phase_records(attribute)
    for record in records:
        record["cursor_reply"] = [1, 1]
    data["peer.json"]["records"] = records
    source_root = root / "frozen-source"
    hashes = {}
    for name in probe.SOURCE_FILES:
        path = source_root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / name, path)
        hashes[name] = exporter.sha(path)
    write(source_root / "reference-source.json", {"schema": "termforge-mode-reference-source-v1",
          "source_base_commit": "0"*40, "source_files_sha256": hashes})
    package = root / "reference-package"
    manifest = {}
    for name in probe.MODULE_SHA256:
        payload = ("synthetic package fixture: "+name).encode()
        for base in (package, directory / "app"):
            base.mkdir(parents=True, exist_ok=True)
            (base / name).write_bytes(payload)
        manifest[name] = hashlib.sha256(payload).hexdigest()
    manifest_hash = hashlib.sha256(json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    data["files.json"] = manifest
    plan = exporter.read(plan_path)
    plan.update(schema="termforge-attribute-state-plan-v1", issue=27, attribute=attribute,
                case_id=probe.case_id(attribute), requested_sgr=probe.attribute_code(attribute), actual_original_runs=0,
                source=str(package), out=str(directory), rea="synthetic-prepared-entry.cmd",
                **probe.source_identity(root=source_root, source_files=probe.SOURCE_FILES),
                module_sha256=manifest, files_manifest_sha256=manifest_hash,
                offline_audit_files_sha256={name: hashes[name] for name in probe.AUDIT_FILES},
                phases=probe.phase_records(attribute), classification=probe.classification_contract(attribute),
                generated_capture_sha256=hashlib.sha256(probe.build_probe_fragment(attribute).encode()).hexdigest())
    write(plan_path, plan)
    plan_path.with_suffix(".attempted").write_text(exporter.ATTEMPT)
    template = (ROOT / "research/ui/qt_r1_text.js").read_text(encoding="utf-8").split("// Scenario driver", 1)[0] + probe.build_probe_fragment(attribute)
    run = data["run.json"]
    run.update(pid=123, tid=124, window_station="TermForgeR1-123", sha256=manifest["WindTerm.exe"], files_sha256=manifest_hash,
               fixture_sha256=hashes["research/ui/r1_vt_fixture.py"], template_sha256=hashlib.sha256(template.encode()).hexdigest(),
               scenario="menu-wrap", profile="synthetic-local-only", clipboard="owned-window-station")
    (directory / "probe.js").write_bytes(exporter.expected_probe(plan, run, source_root))
    run["script_sha256"] = exporter.sha(directory / "probe.js")
    evidence = data["evidence.json"]["data"]["evidence"]
    evidence.update(operation="instrument_with_frida", raw_result=None)
    evidence["normalized_result"].update(mode="attach", source_kind="file", state="running", pid=run["pid"],
                source_sha256=run["script_sha256"], source_path=str(PureWindowsPath(plan["out"]) / "probe.js"),
                scripts=[{"script_id": "synthetic-script"}])
    session = {"session.group": "TermForge Lab", "session.label": "TF_R1_LOOPBACK", "session.protocol": "Telnet",
               "session.target": "127.0.0.1", "session.uuid": "00000000-0000-4000-8000-000000000001", "session.port": 12345}
    write(directory / "profile/.wind/profiles/default.v10/terminal/user.sessions", [session])
    (directory / "fixture-ready").write_text("stable-NAWS-capacity")
    sync(directory, data)
    return directory, plan_path, data, source_root, package


def constants(directory):
    """Substitute only package trust anchors for a four-file synthetic package."""
    manifest = exporter.read(directory / "files.json")
    stack = ExitStack()
    stack.enter_context(patch.object(exporter, "FILE_COUNT", len(manifest)))
    stack.enter_context(patch.object(exporter, "MODULE_SHA256", manifest))
    stack.enter_context(patch.object(exporter, "MANIFEST_SHA256", hashlib.sha256(json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode()).hexdigest()))
    return stack


def export_fixture(directory, plan, destination, source_root, package):
    with constants(directory):
        return exporter.export(directory, plan, destination, exporter.sha(plan), source_root, package)


class AttributeExportTests(unittest.TestCase):
    def test_six_attributes_export_only_synthetic_scalars_with_distinct_controls(self):
        for attribute in probe.ATTRIBUTES:
            with self.subTest(attribute=attribute), tempfile.TemporaryDirectory() as temp:
                root = Path(temp); directory, plan, _, source, package = fixture(root, attribute)
                output = root / "public/result.json"
                result = export_fixture(directory, plan, output, source, package)
                self.assertTrue(result["raw_capture_complete"])
                self.assertEqual([row["unique_control"] for row in result["same_frame_signature_comparisons"]],
                                 [None, "SGR0-control-output", "SGR0-control-output", "SGR0-control-output", f"SGR{probe.ATTRIBUTES[attribute]}-control-output"])
                text = output.read_text()
                for forbidden in ("h_pixels", "space_pixels", str(root), "session.target", '"pid"', "private_error"):
                    self.assertNotIn(forbidden, text)
                self.assertFalse(result["internal_attribute_verified"])
                self.assertIsNone(result["presented_done"])

    def test_identical_controls_and_hidden_no_ink_keep_output_only_semantics(self):
        for same in (False, True):
            with self.subTest(same=same), tempfile.TemporaryDirectory() as temp:
                root = Path(temp); directory, plan, data, source, package = fixture(root, "hidden")
                for capture in data[probe.RESULT_FILE]["captures"]:
                    for roi in capture["rois"]:
                        if same or roi["group"] == "control-attribute" or (roi["group"] == "sample" and capture["phase"] == 4):
                            roi["h_pixels"] = [BG]*4
                sync(directory, data)
                result = export_fixture(directory, plan, root / "public.json", source, package)
                frames = result["same_frame_signature_comparisons"]
                if same:
                    self.assertTrue(all(row["unique_control"] is None and row["reason"] == "controls-indistinguishable" for row in frames))
                else:
                    self.assertEqual(frames[4]["unique_control"], "SGR8-control-output")

    def test_plan_source_package_probe_and_profile_mutations_cannot_publish(self):
        def bad_plan(plan):
            value = exporter.read(plan); value["execution_authorized"] = False; write(plan, value)
        changes = [
            lambda d, p, data, s, pkg: bad_plan(p),
            lambda d, p, data, s, pkg: (s / probe.SOURCE_FILES[0]).write_bytes(b"changed source"),
            lambda d, p, data, s, pkg: (d / "app/Qt5Gui.dll").write_bytes(b"changed copy"),
            lambda d, p, data, s, pkg: (pkg / "Qt5Core.dll").write_bytes(b"changed source package"),
            lambda d, p, data, s, pkg: (d / "probe.js").write_bytes(b"changed injection"),
            lambda d, p, data, s, pkg: p.with_suffix(".attempted").write_text("not the fixed marker"),
            lambda d, p, data, s, pkg: write(d / "profile/.wind/profiles/default.v10/terminal/user.sessions", []),
            lambda d, p, data, s, pkg: (d / "stderr.txt").write_text("unexpected output"),
        ]
        for index, change in enumerate(changes):
            with self.subTest(index=index), tempfile.TemporaryDirectory() as temp:
                root = Path(temp); args = fixture(root); change(*args)
                destination = root / "public/result.json"
                with self.assertRaises(ValueError):
                    export_fixture(args[0], args[1], destination, args[3], args[4])
                self.assertFalse(destination.parent.exists())

    def test_stale_external_plan_hash_is_rejected_even_if_rewritten_plan_is_valid(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); directory, plan, _, source, package = fixture(root)
            pinned = exporter.sha(plan)
            plan.write_text(plan.read_text()+" ")
            with constants(directory), self.assertRaisesRegex(ValueError, "Authorized plan bytes"):
                exporter.export(directory, plan, root / "public.json", pinned, source, package)

    def test_capture_wire_token_and_native_bounds_reject_before_output(self):
        mutations = [
            lambda data: data[probe.RESULT_FILE]["captures"][1]["terminal"]["lines"].__setitem__(0, "PRIVATE-HOST"),
            lambda data: data[probe.RESULT_FILE]["captures"][1]["terminal"]["lines"].__setitem__(0, "EN H HES H H"),
            lambda data: data[probe.RESULT_FILE]["captures"][1]["rois"][0]["h_rect"].__setitem__(2, 0),
            lambda data: data[probe.RESULT_FILE]["captures"][1]["rois"][0]["h_pixels"].__setitem__(0, -1),
            lambda data: data[probe.RESULT_FILE]["captures"][1]["terminal"]["markers"][1].update(position=103),
            lambda data: data[probe.RESULT_FILE]["captures"][1]["terminal"].update(selection_anchor=999),
            lambda data: data[probe.RESULT_FILE]["captures"][1]["image"].update(image_dpr=2),
            lambda data: data[probe.RESULT_FILE].update(grab_phases=[0, 1, 1, 3, 4]),
            lambda data: data["peer.json"].update(received_hex="00"),
            lambda data: data["peer.json"]["records"][1].update(source_hex="1b5b306d"),
            lambda data: data["peer.json"]["naws_history"].append([92, 25]),
            lambda data: data["cleanup.json"].update(active=1),
        ]
        for index, mutation in enumerate(mutations):
            with self.subTest(index=index), tempfile.TemporaryDirectory() as temp:
                root = Path(temp); directory, plan, data, source, package = fixture(root)
                mutation(data); sync(directory, data)
                destination = root / "public/result.json"
                with self.assertRaises(ValueError):
                    export_fixture(directory, plan, destination, source, package)
                self.assertFalse(destination.parent.exists())

    def test_provider_identity_message_and_script_mismatch_reject(self):
        for change in (
            lambda e: e.update(confidence="inferred"),
            lambda e: e["normalized_result"].update(messages_truncated=True),
            lambda e: e["normalized_result"]["messages"][0].update(script_id="foreign-script"),
            lambda e: e["normalized_result"]["messages"][-1]["payload"].update(status=probe.PARTIAL),
            lambda e: e["normalized_result"].update(source_path="C:/foreign/probe.js"),
        ):
            with tempfile.TemporaryDirectory() as temp:
                root = Path(temp); directory, plan, data, source, package = fixture(root)
                change(data["evidence.json"]["data"]["evidence"])
                write(directory / "evidence.json", data["evidence.json"])
                with self.assertRaises(ValueError):
                    export_fixture(directory, plan, root / "public.json", source, package)
                self.assertFalse((root / "public.json").exists())

    def test_partial_prefix_cannot_synthesize_later_phases(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); directory, plan, data, source, package = fixture(root)
            captured = data[probe.RESULT_FILE]
            captured.update(status=probe.PARTIAL, reason="selection-endpoints-unavailable", captures=captured["captures"][:2], grab_phases=[0])
            captured["captures"][1].update(capture_valid=False, reason=captured["reason"], image=None, rois=[],
                                           constructed=[], destroyed=[], grab_attempted=False)
            captured["captures"][1]["terminal"]["selection_anchor"] = None
            data["peer.json"].update(complete=False, records=data["peer.json"]["records"][:2], received_hex="1b5b313b3152"*2)
            for index in range(1, 5):
                (directory / f"ack-{index}").unlink()
            sync(directory, data)
            result = export_fixture(directory, plan, root / "public.json", source, package)
            self.assertFalse(result["raw_capture_complete"])
            self.assertEqual(result["observed_phase_count"], 2)
            self.assertIsNone(result["phases"][1]["attribute_signature_comparison"])

    def test_stop_before_source_freeze_does_not_invent_grid_or_phases(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); directory, plan, data, source, package = fixture(root)
            data[probe.RESULT_FILE].update(status=probe.PARTIAL, reason="first-run-guard", captures=[], grab_phases=[])
            data["peer.json"].update(records=[], received_hex="", complete=False, rows=None, columns=None,
                                     frozen_grid=None, freeze_event_count=None, naws_history=[])
            for index in range(5):
                (directory / f"ack-{index}").unlink()
            (directory / "fixture-ready").unlink()
            sync(directory, data)
            result = export_fixture(directory, plan, root / "public.json", source, package)
            self.assertIsNone(result["frozen_grid"])
            self.assertEqual(result["phases"], [])
            self.assertFalse(result["raw_capture_complete"])

    def test_missing_ack_and_invalid_capture_advancement_cannot_publish(self):
        for invalid in (False, True):
            with self.subTest(invalid=invalid), tempfile.TemporaryDirectory() as temp:
                root = Path(temp); directory, plan, data, source, package = fixture(root)
                if invalid:
                    data[probe.RESULT_FILE]["captures"][1].update(capture_valid=False, reason="native-or-observation-error")
                    sync(directory, data)
                else:
                    (directory / "ack-2").write_text("observed")
                with self.assertRaises(ValueError):
                    export_fixture(directory, plan, root / "public.json", source, package)
                self.assertFalse((root / "public.json").exists())

    def test_boolean_plan_counters_are_not_integer_bounds(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); directory, plan, _, source, package = fixture(root)
            value = exporter.read(plan); value["maximum_original_runs"] = True; write(plan, value)
            with self.assertRaisesRegex(ValueError, "noninteger"):
                export_fixture(directory, plan, root / "public.json", source, package)

    def test_duplicate_or_nonfinite_json_rejects_and_existing_destination_is_preserved(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); directory, plan, _, source, package = fixture(root)
            path = root / "invalid.json"
            for raw in ('{"key":1,"key":2}', '{"key":NaN}'):
                path.write_text(raw)
                with self.assertRaises(ValueError):
                    exporter.read(path)
            destination = root / "public.json"; destination.write_bytes(b"preserve")
            with self.assertRaisesRegex(ValueError, "must be new"):
                export_fixture(directory, plan, destination, source, package)
            self.assertEqual(destination.read_bytes(), b"preserve")

    def test_normal_and_optimized_export_bytes_are_identical(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); directory, plan, _, source, package = fixture(root)
            outputs = []
            for optimized in (False, True):
                destination = root / f"out-{optimized}.json"
                code = "from pathlib import Path; import sys; from research.ui.test_export_attribute_state_reference import export_fixture; export_fixture(*map(Path,sys.argv[1:]))"
                process = subprocess.run([sys.executable, "-B", *(["-O"] if optimized else []), "-c", code,
                    str(directory), str(plan), str(destination), str(source), str(package)], cwd=ROOT, capture_output=True, text=True, timeout=20)
                self.assertEqual(process.returncode, 0, process.stderr)
                outputs.append(destination.read_bytes())
            self.assertEqual(*outputs)


if __name__ == "__main__":
    unittest.main()
