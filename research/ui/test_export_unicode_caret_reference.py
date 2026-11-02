"""Synthetic stateful Unicode publication guards; no target, sockets, REA or native UI."""
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

from research.ui import export_unicode_caret_reference as exporter
from research.ui import unicode_caret_reference as probe
from research.ui import mode_state_reference as original_probe
from research.ui import export_mode_state_reference as original_exporter
from research.ui.test_unicode_caret_reference import recording
from research.ui.test_export_mode_state_reference import fixture as mode_fixture, complete_fixture as mode_complete_fixture


def save(directory, data, synchronize=True):
    if synchronize:
        result = data[probe.RESULT_FILE]
        payloads = [{"kind": "probe_ready", "station_isolated": True, "scenario": "unicode-caret-v1", "case_id": result["case_id"]},
                    {"kind": "readiness", "phase": "ui", "ready": True, "has_tree": True,
                     "has_engine": True, "has_editor": False, "peer_ready": False}]
        payloads.extend({"kind": "unicode_caret_phase", **copy.deepcopy(row)} for row in result["observations"])
        payloads.append({"kind": "unicode_caret_result", **copy.deepcopy(result)})
        data["evidence.json"]["data"]["evidence"]["normalized_result"]["messages"] = [
            {"type": "send", "script_id": "synthetic-script", "payload": value} for value in payloads]
    for name, value in data.items():
        (directory / name).write_text(json.dumps(value), encoding="utf-8")


def marker_rows(lines):
    result,offset=[],0
    for row,line in enumerate(lines):
        for label in exporter.MARKERS:
            column=line.find(label)
            while column>=0:
                prefix=line[:column];end=line[:column+len(label)]
                values=[('utf16-start',len(prefix.encode('utf-16-le'))//2),('scalar-start',column),('utf8-start',len(prefix.encode('utf-8'))),
                        ('utf16-end',len(end.encode('utf-16-le'))//2),('scalar-end',len(end)),('utf8-end',len(end.encode('utf-8')))]
                queries=[{'basis':basis,'offset':value,'position':offset+value,'rect':{'x':offset+value,'y':row*18,'width':8,'height':18}} for basis,value in values]
                result.append({'label':label,'document_line':row,'text_offset_utf16':column,'text_offset_scalar':column,
                               'text_prefix_utf8_bytes':len(prefix.encode('utf-8')),'line_start_api':offset,'queries':queries})
                column=line.find(label,column+1)
        offset+=len(line)+1
    return result


def refresh_terminal(terminal,phase):
    terminal['markers']=marker_rows(terminal['lines'])
    position=[87,83,85,86][phase]
    terminal['caret_api']={'caretLine':7,'caretColumn':0,'caretPosition':position}
    terminal['caret_query']={'line_start_api':23,'position':position,'rect':{'x':position,'y':126,'width':8,'height':18}}


def fixture(root, case="dsr-only-q"):
    directory, data, _ = mode_fixture(root)
    data.pop("mode-state-result.json")
    # A complete fixture contains only this protocol's declared acknowledgments.
    for path in directory.glob("ack-*"):
        path.unlink()
    state, result = recording(case)
    for observation in result["observations"]:
        terminal = observation["terminal"]
        refresh_terminal(terminal,observation["phase"])
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
def complete_fixture(root, case="dsr-only-q"):
    with mode_complete_fixture(root) as original:
        old_directory, _, old_plan, _, source_root, official = original
        with patch.object(probe, "EXE_SHA256", original_probe.EXE_SHA256), \
             patch.object(probe, "MANIFEST_SHA256", original_probe.MANIFEST_SHA256), \
             patch.dict(exporter._PROVENANCE, FILE_COUNT=original_exporter.FILE_COUNT):
            new_root = root / "unicode"
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


class UnicodeCaretExportTests(unittest.TestCase):
    def test_three_cases_preserve_all_samples_UTF8_catalog_and_candidate_units(self):
        for case in probe.CASES:
            with self.subTest(case=case),tempfile.TemporaryDirectory() as temporary:
                directory,data,plan=fixture(Path(temporary).resolve(),case)
                result=exporter.capture_facts(directory,plan)
                self.assertEqual(result['stateful_observation_count'],4)
                self.assertEqual(result['before_next_input_stage'],1);self.assertEqual(result['before_Q_stage'],2)
                self.assertEqual([v['phase'] for v in result['resize_boundaries']],list(probe.resize_phases(case)))
                self.assertIsNone(result['text_units']['cpp_cell_column_mapping'])
                for i,phase in enumerate(result['phases']):
                    doc=phase['document_projection'];public=doc['synthetic_text'];catalog=sorted(probe.synthetic_lines(case,i))
                    rebuilt=[bytes.fromhex(row['utf8_hex']).decode('utf-8') for row in public['lines']]
                    self.assertEqual(rebuilt,data[probe.RESULT_FILE]['observations'][i]['terminal']['lines'])
                    self.assertEqual(public['catalog_sha256'],hashlib.sha256(json.dumps(catalog,separators=(',',':')).encode()).hexdigest())
                    for row,line in zip(public['lines'],rebuilt):
                        self.assertEqual(catalog[row['catalog_index']],line)
                        self.assertEqual(row['utf8_bytes'],len(line.encode('utf-8')))
                        self.assertEqual(row['utf16_code_units'],len(line.encode('utf-16-le'))//2)
                    self.assertEqual(doc['synthetic_lines_sha256'],hashlib.sha256(json.dumps(rebuilt,separators=(',',':')).encode()).hexdigest())
                    self.assertEqual(doc['caret_query']['requested_position_api_value'],doc['caret_api']['caretPosition'])
                    self.assertIsNone(doc['caret_query']['position_unit_classification'])
                self.assertEqual(result['phases'][0]['cursor_reply'],[8,63])
                self.assertNotIn(str(directory),json.dumps(result))

    def test_Z_has_distinct_byte_and_text_queries_and_caret_is_independent(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory,data,plan=fixture(Path(temporary).resolve())
            facts=exporter.capture_facts(directory,plan);doc=facts['phases'][0]['document_projection']
            z=next(v for v in doc['markers'] if v['label']=='Z');queries={q['basis']:q for q in z['queries']}
            self.assertEqual(queries['utf16-start']['requested_position_api_value'],85)
            self.assertEqual(queries['scalar-start']['requested_position_api_value'],85)
            self.assertEqual(queries['utf8-start']['requested_position_api_value'],86)
            self.assertEqual(z['line_text_prefix_utf8_bytes'],63)
            self.assertEqual(doc['caret_query']['requested_position_api_value'],87)
            self.assertEqual(doc['caret_query']['matching_text_boundary_scalar_offsets'],{'utf8':[63],'utf16':[],'scalar':[]})
            after=facts['phases'][2]['document_projection']['caret_query']
            self.assertEqual(after['relative_api_value'],62)
            self.assertEqual(after['matching_text_boundary_scalar_offsets']['utf8'],[61])
            self.assertNotIn('column',z)

    def test_post_mark_Q_retention_overwrite_and_insertion_are_recorded_without_policy(self):
        for replacement in (' '*60+'\u0302Q',' '*60+'Q',' '*60+'\u0302Qe\u0301',' '*60+'e\u0301Z\u0302Q'):
            with self.subTest(replacement=replacement),tempfile.TemporaryDirectory() as temporary:
                directory,data,plan=fixture(Path(temporary).resolve())
                terminal=data[probe.RESULT_FILE]['observations'][3]['terminal'];terminal['lines'][7]=replacement
                refresh_terminal(terminal,3)
                for marker in terminal['markers']:
                    for query in marker['queries']:query['rect'].update(x=400,width=0)
                save(directory,data)
                result=exporter.capture_facts(directory,plan)
                self.assertEqual(bytes.fromhex(result['phases'][3]['document_projection']['synthetic_text']['lines'][7]['utf8_hex']).decode('utf-8'),replacement)

    def test_initial_baseline_future_Q_duplicates_and_private_unicode_rejected(self):
        for phase,lines in ((0,['éZ']),(2,['Q']),(3,['Z','Z']),(3,['é','e']),(3,['PRIVATE']),(3,['😀']),(3,[' '*92])):
            with self.subTest(phase=phase,lines=lines),tempfile.TemporaryDirectory() as temporary:
                directory,data,plan=fixture(Path(temporary).resolve())
                terminal=data[probe.RESULT_FILE]['observations'][phase]['terminal'];terminal.update(lines=lines,line_count=len(lines));refresh_terminal(terminal,phase);save(directory,data)
                with self.assertRaises(ValueError):exporter.capture_facts(directory,plan)

    def test_each_query_basis_arithmetic_bounds_and_original_caret_binding_are_checked(self):
        for kind in ('missing-query','basis','offset','position','line-start','rect','caret-position','caret-rect'):
            with self.subTest(kind=kind),tempfile.TemporaryDirectory() as temporary:
                directory,data,plan=fixture(Path(temporary).resolve());terminal=data[probe.RESULT_FILE]['observations'][2]['terminal'];marker=terminal['markers'][0]
                if kind=='missing-query':marker['queries'].pop()
                elif kind=='basis':marker['queries'][0]['basis']='utf8-start'
                elif kind=='offset':marker['queries'][0]['offset']+=1
                elif kind=='position':marker['queries'][0]['position']+=1
                elif kind=='line-start':marker['line_start_api']='PRIVATE'
                elif kind=='rect':marker['queries'][0]['rect']['width']=1361
                elif kind=='caret-position':terminal['caret_query']['position']+=1
                else:terminal['caret_query']['rect']['x']='PRIVATE'
                save(directory,data)
                with self.assertRaises(ValueError):exporter.capture_facts(directory,plan)

    def test_action_source_case_missing_pre_action_and_stale_or_extra_protocol_events_are_rejected(self):
        for changed in ("action", "case", "missing-baseline", "pre-action-ack", "stale-ready", "extra-reply", "unarmed-naws", "window", "dsr-overflow"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary:
                directory, data, plan = fixture(Path(temporary).resolve(),"resize-mark-q")
                result, peer = data[probe.RESULT_FILE], data["peer.json"]
                if changed == "action": peer["records"][2]["source_hex"] = "0d51"
                elif changed == "case": result["case_id"] = "cup-position-q"
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
                elif changed == "marker-position-bound": terminal["markers"][0]["queries"][0]["position"] = 262145
                elif changed == "rect": terminal["markers"][0]["queries"][0]["rect"]["x"] = "private"
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
                elif changed == "provider-ready": evidence["normalized_result"]["messages"][0]["payload"]["case_id"] = "resize-mark-q"
                save(directory, data, synchronize=not changed.startswith("provider-"))
                with self.assertRaises(ValueError): exporter.capture_facts(directory, plan)

    def test_unlisted_private_metadata_is_not_forwarded(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory, data, plan = fixture(Path(temporary).resolve())
            before = exporter.capture_facts(directory, plan)
            data[probe.RESULT_FILE]["observations"][2]["terminal"]["private_note"] = "SECRET"
            data["peer.json"]["private_path"] = "C:/private"
            save(directory, data)
            self.assertEqual(exporter.capture_facts(directory, plan), before)

    def test_three_complete_ten_source_archives_and_package_identity(self):
        for case in probe.CASES:
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temporary, complete_fixture(Path(temporary).resolve(), case) as values:
                directory, _, _, plan_path, source_root, official = values
                result = exporter.export(directory, plan_path, Path(temporary) / "public/result.json",
                                         exporter.sha(plan_path), source_root, official)
                self.assertEqual(result["schema"], "termforge-original-unicode-caret-reference-v1")
                self.assertEqual(result["case_id"], case)
                self.assertEqual(set(result["provenance"]["execution_source_files_sha256"]), set(probe.SOURCE_FILES))
                self.assertEqual(set(result["offline_export_source_files_sha256"]), set(exporter.EXPORT_SOURCES))
                self.assertIsNone(result["unicode_policy_classification"])
                self.assertFalse(result["semantic_acceptance"])

    def test_changed_package_source_profile_attempt_plan_and_destination_are_rejected(self):
        for changed in ("module", "source", "manifest", "profile", "attempt", "plan", "destination"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary, complete_fixture(Path(temporary).resolve()) as values:
                directory, _, _, plan_path, source_root, official = values
                expected = exporter.sha(plan_path)
                destination = Path(temporary) / "public/result.json"
                if changed == "module": (directory / "app/Qt5Core.dll").write_bytes(b"changed")
                elif changed == "source": (source_root / "research/ui/unicode_caret_reference.py").write_bytes(b"changed")
                elif changed == "manifest": (directory / "files.json").write_text('{}')
                elif changed == "profile": (directory / "profile/.wind/profiles/default.v10/terminal/user.sessions").write_text('[]')
                elif changed == "attempt": plan_path.with_suffix('.attempted').write_text('not Unicode')
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
                    "research.ui.export_unicode_caret_reference", "--directory", str(directory), "--plan", str(plan_path),
                    "--expected-plan-sha256", exporter.sha(plan_path), "--destination", str(destination)]
                completed = subprocess.run(command, cwd=probe.ROOT, capture_output=True, text=True, encoding="utf-8", timeout=10)
                self.assertNotEqual(completed.returncode, 0)
                self.assertIn("Only fixed unicode-source synthetic document lines", completed.stderr)
                self.assertFalse(destination.parent.exists())


if __name__ == "__main__":
    unittest.main()
