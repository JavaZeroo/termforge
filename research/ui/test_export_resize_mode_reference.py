"""Synthetic stateful frame publication guards; no target, sockets, REA or native UI."""
from __future__ import annotations
import copy
from contextlib import contextmanager
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from research.ui import export_resize_mode_reference as exporter
from research.ui import resize_mode_reference as probe
from research.ui import mode_state_reference as original_probe
from research.ui import export_mode_state_reference as original_exporter
from research.ui.test_resize_mode_reference import recording
from research.ui.test_export_mode_state_reference import fixture as mode_fixture, complete_fixture as mode_complete_fixture


def save(directory, data, synchronize=True):
    if synchronize:
        result = data[probe.RESULT_FILE]
        payloads = [{"kind": "probe_ready", "station_isolated": True, "scenario": "resize-mode-v1", "case_id": result["case_id"]},
                    {"kind": "readiness", "phase": "ui", "ready": True, "has_tree": True,
                     "has_engine": True, "has_editor": False, "peer_ready": False}]
        payloads.extend({"kind": "resize_mode_phase", **copy.deepcopy(row)} for row in result["observations"])
        payloads.append({"kind": "resize_mode_result", **copy.deepcopy(result)})
        data["evidence.json"]["data"]["evidence"]["normalized_result"]["messages"] = [
            {"type": "send", "script_id": "synthetic-script", "payload": value} for value in payloads]
    for name, value in data.items():
        (directory / name).write_text(json.dumps(value), encoding="utf-8")


def marker_rows(lines):
    result, offset = [], 0
    for row, line in enumerate(lines):
        for label in exporter.MARKERS:
            column = line.find(label)
            while column >= 0:
                result.append({"label": label, "document_line": row, "column": column, "position": offset+column,
                               "rect": {"x": column*8, "y": row*18, "width": 8, "height": 18}})
                column = line.find(label, column+1)
        offset += len(line)+1
    return result


def fixture(root, case="active-primary"):
    directory, data, _ = mode_fixture(root)
    data.pop("mode-state-result.json")
    # A complete fixture contains only this protocol's declared acknowledgments.
    for path in directory.glob("ack-*"):
        path.unlink()
    state, result = recording(case)
    for observation in result["observations"]:
        terminal = observation["terminal"]
        terminal["markers"] = marker_rows(terminal["lines"])
    data["peer.json"], data[probe.RESULT_FILE] = state, result
    plan = {**probe.fixed_contract(case), "execution_authorized": True, "out": str(directory), "source": str(root / "official")}
    run = data["run.json"]
    run.update(sha256=probe.EXE_SHA256, files_sha256=probe.MANIFEST_SHA256)
    template = (probe.ROOT / "research/ui/qt_r1_text.js").read_text(encoding="utf-8").split("// Scenario driver", 1)[0]
    template += probe.probe_fragment(case)
    run["template_sha256"] = hashlib.sha256(template.encode()).hexdigest()
    generated = exporter.expected_probe(plan, run, probe.ROOT)
    (directory / "probe.js").write_bytes(generated)
    run["script_sha256"] = hashlib.sha256(generated).hexdigest()
    data["evidence.json"]["data"]["evidence"]["normalized_result"]["source_sha256"] = run["script_sha256"]
    (directory / "fixture-ready").write_text(json.dumps({"grid": state["frozen_grids"][0], "event_count": state["freeze_event_counts"][0]}))
    for index in range(len(probe.phases(case))):
        (directory / f"ack-{index}").write_text("observed")
    for index in probe.resize_phases(case):
        (directory / f"resize-request-{index}").write_text(json.dumps({"phase": index, "window": list(probe.windows(case)[index])}))
        (directory / f"resize-ready-{index}").write_text(json.dumps({"phase": index,
            "grid": state["frozen_grids"][index], "event_count": state["freeze_event_counts"][index]}))
    save(directory, data)
    (directory / "mechanics-check.json").write_text(json.dumps(probe.audit_mechanics(directory, case)))
    return directory, data, plan


@contextmanager
def complete_fixture(root, case="active-primary"):
    with mode_complete_fixture(root) as original:
        old_directory, _, old_plan, _, source_root, official = original
        with patch.object(probe, "EXE_SHA256", original_probe.EXE_SHA256), \
             patch.object(probe, "MANIFEST_SHA256", original_probe.MANIFEST_SHA256), \
             patch.dict(exporter._PROVENANCE, FILE_COUNT=original_exporter.FILE_COUNT):
            new_root = root / "frame"
            new_root.mkdir()
            directory, data, case_plan = fixture(new_root, case)
            plan = {**old_plan, **case_plan, "source": str(official)}
            (directory / "app").mkdir()
            for name in original_exporter.MODULES:
                (directory / "app" / name).write_bytes((official / name).read_bytes())
            (directory / "files.json").write_bytes((old_directory / "files.json").read_bytes())
            profile = "profile/.wind/profiles/default.v10/terminal/user.sessions"
            (directory / profile).parent.mkdir(parents=True)
            (directory / profile).write_bytes((old_directory / profile).read_bytes())
            for name in probe.SOURCE_FILES:
                (source_root / name).parent.mkdir(parents=True, exist_ok=True)
                (source_root / name).write_bytes((probe.ROOT / name).read_bytes())
            archive = {"schema": probe.common.ARCHIVE_SCHEMA, "source_base_commit": "b"*40,
                       "source_files_sha256": {name: probe.sha(source_root / name) for name in probe.SOURCE_FILES}}
            (source_root / probe.common.ARCHIVE_MANIFEST).write_text(json.dumps(archive), encoding="utf-8")
            plan.update(probe.common.source_identity(root=source_root, source_files=probe.SOURCE_FILES))
            plan["launcher_source_sha256"] = hashlib.sha256(probe.launcher_source(case).encode()).hexdigest()
            plan_path = new_root / "plan.json"
            plan_path.write_text(json.dumps(plan), encoding="utf-8")
            plan_path.with_suffix(".attempted").write_text(probe.ATTEMPT)
            yield directory, data, plan, plan_path, source_root, official


class ResizeModeExportTests(unittest.TestCase):
    def test_all_three_cases_preserve_every_stateful_observation_and_exact_sources(self):
        for case in probe.CASES:
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temporary:
                directory, _, plan = fixture(Path(temporary).resolve(), case)
                result = exporter.capture_facts(directory, plan)
                self.assertEqual(len(result["phases"]),len(probe.phases(case)))
                self.assertEqual(result["stateful_observation_count"],len(probe.phases(case)))
                self.assertEqual([v["source_hex"] for v in result["phases"]],[v.hex() for _,v in probe.phases(case)])
                self.assertEqual([v["stage_role"] for v in result["phases"]],[label for label,_ in probe.phases(case)])
                self.assertEqual([v["phase"] for v in result["resize_boundaries"]],list(probe.resize_phases(case)))
                self.assertEqual(result["independent_dsr_bounds"],{"row":[1,200],"column":[1,500]})
                for phase in result["phases"]:
                    self.assertNotIn("lines",phase["document_projection"])
                    self.assertEqual(phase["cursor_reply"],[8,91])
                self.assertNotIn(str(directory),json.dumps(result))
                if case=="saved-primary-edge":
                    self.assertEqual(result["unmodified_return_stage"],3)
                    self.assertEqual(result["before_next_character_stage"],3)
                else:
                    self.assertIsNone(result["unmodified_return_stage"])
                    self.assertIsNone(result["before_next_character_stage"])

    def test_missing_tail_wrong_frame_or_changed_edge_are_observations_not_predicted_policy(self):
        scenarios=(("active-primary",1," "*61),("active-primary",2,"UP   T0"),
                   ("active-alternate",3,"DN   R3  AP"),("saved-primary-edge",3," "*9+"AP"),
                   ("saved-primary-edge",4," "*90+"WX"),("saved-primary-edge",4," "*60+"X"))
        for case,index,replacement in scenarios:
            with self.subTest(case=case),tempfile.TemporaryDirectory() as temporary:
                directory,data,plan=fixture(Path(temporary).resolve(),case)
                for row in data[probe.RESULT_FILE]["observations"][index:]:
                    terminal=row["terminal"];terminal["lines"]=[replacement];terminal["line_count"]=1
                    terminal["markers"]=marker_rows(terminal["lines"])
                save(directory,data)
                result=exporter.capture_facts(directory,plan)
                self.assertEqual(result["phases"][index]["document_projection"]["line_count"],1)

    def test_private_text_premature_or_repeated_action_and_unbounded_padding_are_rejected(self):
        for phase, lines in ((0, ["X"]), (1, ["X"]), (2, [" "*90+"W", " "*90+"W"]),
                             (2, [" "*93+"X"]), (3, ["SECRET"]), (3, [" "*90+"WW"]), (3, ["ANCHOR "])):
            with self.subTest(phase=phase, lines=lines), tempfile.TemporaryDirectory() as temporary:
                directory, data, plan = fixture(Path(temporary).resolve())
                terminal = data[probe.RESULT_FILE]["observations"][phase]["terminal"]
                terminal.update(lines=lines, line_count=len(lines), markers=marker_rows(lines))
                save(directory, data)
                with self.assertRaises(ValueError): exporter.capture_facts(directory, plan)

    def test_action_source_case_missing_pre_action_and_stale_or_extra_protocol_events_are_rejected(self):
        for changed in ("action", "case", "missing-baseline", "pre-action-ack", "stale-ready", "extra-reply", "unarmed-naws", "window", "dsr-overflow"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary:
                directory, data, plan = fixture(Path(temporary).resolve())
                result, peer = data[probe.RESULT_FILE], data["peer.json"]
                if changed == "action": peer["records"][2]["source_hex"] = "0d51"
                elif changed == "case": result["case_id"] = "saved-primary-edge"
                elif changed == "missing-baseline": result["observations"].pop(1)
                elif changed == "pre-action-ack": (directory / "ack-2").write_text("not observed")
                elif changed == "stale-ready": (directory / "resize-ready-1").write_text('{}')
                elif changed == "extra-reply": peer["received_hex"] += "1b5b313b3152"
                elif changed == "unarmed-naws": peer["naws_history"].append([90, 20])
                elif changed == "window": result["observations"][2]["window_rect"][2] += 1
                elif changed == "dsr-overflow": peer["records"][1]["response_hex"] = "1b5b383b35303152"
                save(directory, data)
                with self.assertRaises(ValueError): exporter.capture_facts(directory, plan)

    def test_marker_provider_source_identity_and_partial_results_are_rejected(self):
        for changed in ("marker-label", "marker-position-bound", "rect", "caret", "exe", "manifest", "template", "script",
                        "evidence", "truncated", "cleanup", "source-path", "provider-observations", "provider-ready"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary:
                directory, data, plan = fixture(Path(temporary).resolve())
                terminal = data[probe.RESULT_FILE]["observations"][2]["terminal"]
                evidence = data["evidence.json"]["data"]["evidence"]
                if changed == "marker-label": terminal["markers"][0]["label"] = "SECRET"
                # position is an observed API coordinate, not a sum of Python
                # line lengths; enforce its declared bound without inferring
                # an unobserved document-position numbering policy.
                elif changed == "marker-position-bound": terminal["markers"][0]["position"] = 262145
                elif changed == "rect": terminal["markers"][0]["rect"]["x"] = "private"
                elif changed == "caret": terminal["caret_api"]["caretPosition"] = "private"
                elif changed == "exe": data["run.json"]["sha256"] = "0"*64
                elif changed == "manifest": data["run.json"]["files_sha256"] = "0"*64
                elif changed == "template": data["run.json"]["template_sha256"] = "0"*64
                elif changed == "script": (directory / "probe.js").write_bytes(b"changed")
                elif changed == "evidence": evidence["evidence_id"] = "private"
                elif changed == "truncated": evidence["normalized_result"]["messages_truncated"] = True
                elif changed == "cleanup": data["cleanup.json"]["active"] = 1
                elif changed == "source-path": evidence["normalized_result"]["source_path"] = "C:/private/other.js"
                elif changed == "provider-observations": evidence["normalized_result"]["messages"].pop(3)
                elif changed == "provider-ready": evidence["normalized_result"]["messages"][0]["payload"]["case_id"] = "saved-primary-edge"
                save(directory, data, synchronize=not changed.startswith("provider-"))
                with self.assertRaises(ValueError): exporter.capture_facts(directory, plan)

    def test_unlisted_private_metadata_is_not_forwarded(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory, data, plan = fixture(Path(temporary).resolve())
            before = exporter.capture_facts(directory, plan)
            data[probe.RESULT_FILE]["observations"][2]["terminal"]["private_note"] = "SECRET"
            data["peer.json"]["resize_boundaries"][0]["private_path"] = "C:/private"
            save(directory, data)
            self.assertEqual(exporter.capture_facts(directory, plan), before)

    def test_all_three_complete_ten_source_archives_and_package_identity(self):
        for case in probe.CASES:
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temporary, complete_fixture(Path(temporary).resolve(), case) as values:
                directory, _, _, plan_path, source_root, official = values
                result = exporter.export(directory, plan_path, Path(temporary) / "public/result.json",
                                         exporter.sha(plan_path), source_root, official)
                self.assertEqual(result["schema"], "termforge-original-resize-mode-reference-v1")
                self.assertEqual(result["case_id"], case)
                self.assertEqual(set(result["provenance"]["execution_source_files_sha256"]), set(probe.SOURCE_FILES))
                self.assertEqual(set(result["offline_export_source_files_sha256"]), set(exporter.EXPORT_SOURCES))
                self.assertIsNone(result["mode_policy_classification"])
                self.assertFalse(result["semantic_acceptance"])

    def test_changed_package_source_profile_attempt_plan_and_destination_are_rejected(self):
        for changed in ("module", "source", "manifest", "profile", "attempt", "plan", "destination"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary, complete_fixture(Path(temporary).resolve()) as values:
                directory, _, _, plan_path, source_root, official = values
                expected = exporter.sha(plan_path)
                destination = Path(temporary) / "public/result.json"
                if changed == "module": (directory / "app/Qt5Core.dll").write_bytes(b"changed")
                elif changed == "source": (source_root / "research/ui/resize_mode_reference.py").write_bytes(b"changed")
                elif changed == "manifest": (directory / "files.json").write_text('{}')
                elif changed == "profile": (directory / "profile/.wind/profiles/default.v10/terminal/user.sessions").write_text('[]')
                elif changed == "attempt": plan_path.with_suffix('.attempted').write_text('not frame')
                elif changed == "plan": plan_path.write_bytes(plan_path.read_bytes()+b' ')
                elif changed == "destination": destination.parent.mkdir(); destination.write_text('preserved')
                with self.assertRaises(ValueError): exporter.export(directory, plan_path, destination, expected, source_root, official)
                if changed == "destination": self.assertEqual(destination.read_text(), 'preserved')
                else: self.assertFalse(destination.parent.exists())

    def test_duplicate_fields_and_nonfinite_metadata_fail_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "record.json"
            for content in ('{"phase":1,"phase":2}', '{"x":NaN}', '{"x":Infinity}', '{"x":-Infinity}'):
                path.write_text(content)
                with self.subTest(content=content), self.assertRaises(ValueError): exporter.read(path)

    def test_cli_refuses_private_text_normally_and_under_optimization(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            directory, data, plan = fixture(root)
            data[probe.RESULT_FILE]["observations"][1]["terminal"]["lines"][1] = "SECRET"
            save(directory, data)
            plan_path = root / "plan.json"
            plan_path.write_text(json.dumps(plan))
            for optimize in (False, True):
                destination = root / ("optimized/out.json" if optimize else "normal/out.json")
                command = [sys.executable, "-B", *(["-O"] if optimize else []), "-X", "utf8", "-m",
                    "research.ui.export_resize_mode_reference", "--directory", str(directory), "--plan", str(plan_path),
                    "--expected-plan-sha256", exporter.sha(plan_path), "--destination", str(destination)]
                completed = subprocess.run(command, cwd=probe.ROOT, capture_output=True, text=True, encoding="utf-8", timeout=10)
                self.assertNotEqual(completed.returncode, 0)
                self.assertIn("Only fixed mode-source synthetic document lines", completed.stderr)
                self.assertFalse(destination.parent.exists())


if __name__ == "__main__":
    unittest.main()
