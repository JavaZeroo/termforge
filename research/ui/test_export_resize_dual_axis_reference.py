"""Synthetic three-stage dual-axis resize publication guards; no target, sockets, REA or native UI."""
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

from research.ui import export_resize_dual_axis_reference as exporter
from research.ui import resize_dual_axis_reference as probe
from research.ui import mode_state_reference as original_probe
from research.ui import export_mode_state_reference as original_exporter
from research.ui.test_resize_dual_axis_reference import recording
from research.ui.test_export_mode_state_reference import fixture as mode_fixture, complete_fixture as mode_complete_fixture


def save(directory, data, synchronize=True):
    if synchronize:
        result = data[probe.RESULT_FILE]
        payloads = [{"kind": "probe_ready", "station_isolated": True, "scenario": "resize-dual-axis-v1", "case_id": result["case_id"]},
                    {"kind": "readiness", "phase": "ui", "ready": True, "has_tree": True,
                     "has_engine": True, "has_editor": False, "peer_ready": False}]
        payloads.extend({"kind": "resize_dual_axis_phase", **copy.deepcopy(row)} for row in result["observations"])
        payloads.append({"kind": "resize_dual_axis_result", **copy.deepcopy(result)})
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
                result.append({"label": label, "document_line": row, "column": column, "position": offset+column, "line_start_api": offset,
                               "rect": {"x": column*8, "y": row*18, "width": 8, "height": 18}})
                column = line.find(label, column+1)
        offset += len(line)+1
    return result


def fixture(root, case="dual-axis-retention"):
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
def complete_fixture(root, case="dual-axis-retention"):
    with mode_complete_fixture(root) as original:
        old_directory, _, old_plan, _, source_root, official = original
        with patch.object(probe, "EXE_SHA256", original_probe.EXE_SHA256), \
             patch.object(probe, "MANIFEST_SHA256", original_probe.MANIFEST_SHA256), \
             patch.dict(exporter._PROVENANCE, FILE_COUNT=original_exporter.FILE_COUNT):
            new_root = root / "dual-axis"
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


class ResizeDualAxisExportTests(unittest.TestCase):
    def test_fixed_three_stage_contract_and_literal_input_are_independent_of_prior_height_runs(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory, _, plan = fixture(Path(temporary).resolve())
            result = exporter.capture_facts(directory, plan)
            source = (b"\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J\x1b[2;1HHTOP\x1b[6;1HHARD_A\r\nHARD_B"
                      b"\x1b[12;90Huvw\x1b[19;1HLOWEND")
            self.assertEqual(len(source), 75)
            self.assertEqual(result["stateful_observation_count"], 3)
            self.assertEqual([value["frozen_grid"] for value in result["phases"]], [[91,20],[61,14],[91,20]])
            self.assertEqual([value["window_size"] for value in result["phases"]], [[1360,749],[1000,569],[1360,749]])
            self.assertEqual([value["source_hex"] for value in result["phases"]], [source.hex(), probe.WRITE.hex(), ""])
            self.assertEqual([value["source_sha256"] for value in result["phases"]],
                             [hashlib.sha256(value).hexdigest() for value in (source,probe.WRITE,b"")])
            self.assertEqual(result["source_bytes"], 75 + len(probe.WRITE))
            self.assertEqual(result["dsr_request_bytes"], 12)
            self.assertEqual([phase["stage_role"] for phase in result["phases"]], [
                "initial-body", "dual-shrunk-with-write", "dual-restored-unmodified"])
            self.assertEqual([item["observed_grid"] for item in result["geometry_checks"]],
                             [[91,20],[61,14],[91,20]])
            self.assertEqual([item["terminal_capacity"] for item in result["geometry_checks"]], [20,14,20])
            self.assertEqual([item["event_count"] for item in result["geometry_checks"]], result["freeze_event_counts"])

    def test_later_DSR_above_fourteen_rows_is_observed_without_clamping(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory, data, plan = fixture(Path(temporary).resolve())
            peer = data["peer.json"]
            reply = b"\x1b[200;500R"
            for record in (peer["records"][1], data[probe.RESULT_FILE]["observations"][1]):
                record.update(cursor_reply=[200,500], cursor_reply_private=False, response_hex=reply.hex())
            peer["received_hex"] = "".join(record["response_hex"] for record in peer["records"])
            save(directory,data)
            result = exporter.capture_facts(directory,plan)
            self.assertEqual(result["phases"][1]["cursor_reply"],[200,500])
            self.assertEqual(result["phases"][1]["document_projection"]["capacity"],14)

    def test_old_case_names_and_ten_row_endpoint_are_not_accepted(self):
        for changed in ("height-only", "height-retention-measured", "width-then-height", "ten-row"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary:
                directory, data, plan = fixture(Path(temporary).resolve())
                if changed == "ten-row":
                    data["peer.json"]["frozen_grids"][2] = [61,10]
                    data[probe.RESULT_FILE]["observations"][2]["terminal"]["capacity"] = 10
                else:
                    plan["case_id"] = changed
                save(directory,data)
                with self.assertRaises(ValueError):
                    exporter.capture_facts(directory,plan)

    def test_fixed_case_reconstructs_complete_ASCII_documents_and_exact_sources(self):
        for case in probe.CASES:
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temporary:
                directory, data, plan = fixture(Path(temporary).resolve(), case)
                result = exporter.capture_facts(directory, plan)
                self.assertEqual(result["stateful_observation_count"], len(probe.phases(case)))
                self.assertEqual([p["source_hex"] for p in result["phases"]], [v.hex() for _,v in probe.phases(case)])
                self.assertEqual([p["phase"] for p in result["resize_boundaries"]], list(probe.resize_phases(case)))
                self.assertEqual(result["post_initial_terminal_input"], plan["post_initial_terminal_input"])
                self.assertEqual(result["independent_dsr_bounds"], {"row":[1,200],"column":[1,500]})
                self.assertIsNone(result["text_units"]["cpp_cell_column_mapping"])
                self.assertIsNone(result["text_units"]["document_position_unit_classification"])
                for index, phase in enumerate(result["phases"]):
                    actual = data[probe.RESULT_FILE]["observations"][index]["terminal"]["lines"]
                    public = phase["document_projection"]["synthetic_text"]
                    catalog = sorted(probe.synthetic_lines(case,index))
                    self.assertEqual(public["catalog_sha256"], hashlib.sha256(json.dumps(catalog,separators=(',',':')).encode()).hexdigest())
                    rebuilt = [bytes.fromhex(row["utf8_hex"]).decode('ascii') for row in public["lines"]]
                    self.assertEqual(rebuilt, actual)
                    for row,line in zip(public["lines"],rebuilt):
                        self.assertEqual(catalog[row["catalog_index"]],line)
                        self.assertEqual([row["utf8_bytes"],row["unicode_scalars"],row["utf16_code_units"]],[len(line)]*3)
                    self.assertEqual(phase["document_projection"]["synthetic_lines_sha256"],hashlib.sha256(json.dumps(rebuilt,separators=(',',':')).encode()).hexdigest())
                    self.assertEqual(phase["cursor_reply"],[19,7])
                    for marker in phase["document_projection"]["markers"]:
                        self.assertEqual(marker["line_text_offset_scalar"],marker["line_text_offset_utf16"])
                        self.assertEqual(marker["line_text_prefix_utf8_bytes"],marker["line_text_offset_utf16"])
                        self.assertEqual(marker["requested_position_api_value"],marker["line_start_api_value"]+marker["line_text_offset_utf16"])
                        self.assertNotIn("column",marker)
                self.assertNotIn(str(directory),json.dumps(result))

    def test_later_missing_markers_and_split_text_are_observations(self):
        for case, phase, lines in (("dual-axis-retention",1,["DQ"]), ("dual-axis-retention",2,["DQ","LOWEND"])):
            with self.subTest(case=case,phase=phase), tempfile.TemporaryDirectory() as temporary:
                directory,data,plan = fixture(Path(temporary).resolve(),case)
                terminal=data[probe.RESULT_FILE]["observations"][phase]["terminal"]
                terminal.update(lines=lines,line_count=len(lines),markers=marker_rows(lines));save(directory,data)
                (directory/"mechanics-check.json").write_text(json.dumps(probe.audit_mechanics(directory,case)))
                result=exporter.capture_facts(directory,plan)
                self.assertEqual(result["phases"][phase]["document_projection"]["line_count"],len(lines))
                self.assertEqual(result["phases"][phase]["cursor_reply"],[19,7])

    def test_missing_initial_fixture_private_text_nonASCII_and_duplicates_fail(self):
        for phase,lines in ((0,[""]),(1,["SECRET"]),(1,["é"]),(1,["HTOP","HTOP"]),
                            (1,[" "*92]),(1,["LOWENDQ"]),(1,["uvwuvw"])):
            with self.subTest(phase=phase,lines=lines), tempfile.TemporaryDirectory() as temporary:
                directory,data,plan=fixture(Path(temporary).resolve())
                terminal=data[probe.RESULT_FILE]["observations"][phase]["terminal"]
                terminal.update(lines=lines,line_count=len(lines),markers=marker_rows(lines));save(directory,data)
                with self.assertRaises(ValueError):exporter.capture_facts(directory,plan)

    def test_sampled_geometry_cannot_be_missing_stale_or_disagree_with_frozen_stage(self):
        for changed in ("missing", "grid", "capacity", "event", "bool-grid", "failed-result"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary:
                directory, data, plan = fixture(Path(temporary).resolve())
                result = data[probe.RESULT_FILE]
                measured = result["geometry_checks"][1]
                if changed == "missing": measured.pop("observed_grid")
                elif changed == "grid": measured["observed_grid"] = [91,14]
                elif changed == "capacity": measured["terminal_capacity"] = 20
                elif changed == "event": measured["event_count"] = 1
                elif changed == "bool-grid": measured["observed_grid"][0] = True
                else:
                    result["error"] = "Geometry prediction differed"
                    result["failure_geometry"] = {"window_rect": [0,0,999,568], "observed_grid": [61,13]}
                save(directory, data)
                with self.assertRaises((ValueError, KeyError)): exporter.capture_facts(directory, plan)

    def test_marker_API_arithmetic_and_bounds_are_not_guessed(self):
        for kind in ("missing-start","bad-start","bad-sum","wide-overflow","negative-height","position-bound","label"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temporary:
                directory,data,plan=fixture(Path(temporary).resolve())
                marker=data[probe.RESULT_FILE]["observations"][1]["terminal"]["markers"][0]
                if kind=="missing-start":marker.pop("line_start_api")
                elif kind=="bad-start":marker["line_start_api"]="SECRET"
                elif kind=="bad-sum":marker["line_start_api"]+=1
                elif kind=="wide-overflow":marker["rect"]["width"]=1361
                elif kind=="negative-height":marker["rect"]["height"]=-1
                elif kind=="position-bound":marker["position"]=262145
                else:marker["label"]="SECRET"
                save(directory,data)
                with self.assertRaises((ValueError,KeyError)):exporter.capture_facts(directory,plan)

    def test_source_geometry_capacity_phase_DSR_and_NAWS_tampering_fail(self):
        for changed in ("input","case","missing-stage","ack","ready","reply","unarmed","wrong-axis","outside-interval",
                        "window","capacity","initial-DSR","DSR-overflow"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary:
                directory,data,plan=fixture(Path(temporary).resolve())
                result,peer=data[probe.RESULT_FILE],data["peer.json"]
                if changed=="input":peer["records"][1]["source_hex"]="0d51"
                elif changed=="case":result["case_id"]="width-then-height"
                elif changed=="missing-stage":result["observations"].pop(1)
                elif changed=="ack":(directory/"ack-1").write_text("not observed")
                elif changed=="ready":(directory/"resize-ready-1").write_text('{}')
                elif changed=="reply":peer["received_hex"]+="1b5b313b3152"
                elif changed=="unarmed":peer["naws_history"].append([90,20])
                elif changed in ("wrong-axis","outside-interval"):
                    index=peer["resize_boundaries"][0]["before_event_count"]
                    peer["naws_history"][index]=[92,15] if changed=="wrong-axis" else [91,9]
                elif changed=="window":result["observations"][1]["window_rect"][3]+=1
                elif changed=="capacity":result["observations"][1]["terminal"]["capacity"]=20
                else:peer["records"][0 if changed=="initial-DSR" else 1]["response_hex"]="1b5b31383b3752" if changed=="initial-DSR" else "1b5b3230313b3752"
                save(directory,data)
                with self.assertRaises(ValueError):exporter.capture_facts(directory,plan)

    def test_provider_source_identity_partial_results_and_cleanup_fail(self):
        for changed in ("exe","manifest","template","script","evidence","truncated","cleanup","source-path","provider-observations","provider-ready"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary:
                directory,data,plan=fixture(Path(temporary).resolve())
                evidence=data["evidence.json"]["data"]["evidence"]
                if changed=="exe":data["run.json"]["sha256"]="0"*64
                elif changed=="manifest":data["run.json"]["files_sha256"]="0"*64
                elif changed=="template":data["run.json"]["template_sha256"]="0"*64
                elif changed=="script":(directory/"probe.js").write_bytes(b"changed")
                elif changed=="evidence":evidence["evidence_id"]="SECRET"
                elif changed=="truncated":evidence["normalized_result"]["messages_truncated"]=True
                elif changed=="cleanup":data["cleanup.json"]["active"]=1
                elif changed=="source-path":evidence["normalized_result"]["source_path"]="C:/private/other.js"
                elif changed=="provider-observations":evidence["normalized_result"]["messages"].pop(3)
                else:evidence["normalized_result"]["messages"][0]["payload"]["case_id"]="wrong"
                save(directory,data,synchronize=not changed.startswith("provider-"))
                with self.assertRaises(ValueError):exporter.capture_facts(directory,plan)

    def test_unlisted_metadata_is_not_forwarded(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory,data,plan=fixture(Path(temporary).resolve())
            before=exporter.capture_facts(directory,plan)
            data[probe.RESULT_FILE]["observations"][1]["terminal"]["private_note"]="SECRET"
            data["peer.json"]["resize_boundaries"][0]["private_path"]="C:/private"
            data[probe.RESULT_FILE]["geometry_checks"][2]["private_note"]="SECRET"
            save(directory,data)
            self.assertEqual(exporter.capture_facts(directory,plan),before)

    def test_complete_ten_source_archive_and_package_identity(self):
        for case in probe.CASES:
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temporary, complete_fixture(Path(temporary).resolve(), case) as values:
                directory, _, _, plan_path, source_root, official = values
                result = exporter.export(directory, plan_path, Path(temporary) / "public/result.json",
                                         exporter.sha(plan_path), source_root, official)
                self.assertEqual(result["schema"], "termforge-original-resize-dual-axis-reference-v1")
                self.assertEqual(result["case_id"], case)
                self.assertEqual(set(result["provenance"]["execution_source_files_sha256"]), set(probe.SOURCE_FILES))
                self.assertEqual(set(result["offline_export_source_files_sha256"]), set(exporter.EXPORT_SOURCES))
                self.assertIsNone(result["dual_axis_policy_classification"])
                self.assertFalse(result["semantic_acceptance"])
                self.assertEqual(result["status"],"observation")
                self.assertEqual(result["geometry_basis"], {
                    "geometry": {"reference": "research/ui/results/2026-11-02-resize-geometry-reference/strict-export.json",
                        "strict_export_sha256": "f18a2320c073ed477f773b42076c9968c0c8af8530af19548d5695e66e8cf9b5",
                        "execution_source_base_commit": "3d4c8c2d6222caf0fc024729d9e0b34c020a69cf"},
                    "sequential_width_height": {"reference": "research/ui/results/2026-11-02-resize-combined-reference/strict-export.json",
                        "strict_export_sha256": "6a1561f3e6f6bc480c6a2f086adf7ed4798d5007b04a7bc71c7ae9deba90cfe6",
                        "execution_source_base_commit": "2c7c5929e11d911ad69a7e2b7c420120591862ed"},
                    "dual_axis_grid_previously_observed": False})

    def test_geometry_basis_tampering_is_rejected_even_with_new_plan_hash(self):
        for changed in ("geometry", "sequential_width_height", "source-commit", "previously-observed"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary, complete_fixture(Path(temporary).resolve()) as values:
                directory, _, plan, plan_path, source_root, official = values
                if changed in ("geometry", "sequential_width_height"):
                    plan["geometry_basis"][changed]["strict_export_sha256"] = "0"*64
                elif changed == "source-commit":
                    plan["geometry_basis"]["sequential_width_height"]["execution_source_base_commit"] = "0"*40
                else: plan["geometry_basis"]["dual_axis_grid_previously_observed"] = True
                plan_path.write_text(json.dumps(plan), encoding="utf-8")
                destination = Path(temporary)/"public/result.json"
                with self.assertRaisesRegex(ValueError, "contract differs"):
                    exporter.export(directory,plan_path,destination,exporter.sha(plan_path),source_root,official)
                self.assertFalse(destination.exists())

    def test_changed_package_source_profile_attempt_plan_and_destination_are_rejected(self):
        for changed in ("module", "source", "manifest", "profile", "attempt", "plan", "destination"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary, complete_fixture(Path(temporary).resolve()) as values:
                directory, _, _, plan_path, source_root, official = values
                expected = exporter.sha(plan_path)
                destination = Path(temporary) / "public/result.json"
                if changed == "module": (directory / "app/Qt5Core.dll").write_bytes(b"changed")
                elif changed == "source": (source_root / "research/ui/resize_dual_axis_reference.py").write_bytes(b"changed")
                elif changed == "manifest": (directory / "files.json").write_text('{}')
                elif changed == "profile": (directory / "profile/.wind/profiles/default.v10/terminal/user.sessions").write_text('[]')
                elif changed == "attempt": plan_path.with_suffix('.attempted').write_text('not dual-axis')
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
                    "research.ui.export_resize_dual_axis_reference", "--directory", str(directory), "--plan", str(plan_path),
                    "--expected-plan-sha256", exporter.sha(plan_path), "--destination", str(destination)]
                completed = subprocess.run(command, cwd=probe.ROOT, capture_output=True, text=True, encoding="utf-8", timeout=10)
                self.assertNotEqual(completed.returncode, 0)
                self.assertIn("Only fixed dual-axis source-derived synthetic document lines", completed.stderr)
                self.assertFalse(destination.parent.exists())


if __name__ == "__main__":
    unittest.main()
