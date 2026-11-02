"""Offline geometry publication guards; no original, sockets, REA or native UI."""
from __future__ import annotations
import copy
from contextlib import contextmanager, ExitStack
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from research.ui import export_resize_geometry_reference as exporter
from research.ui import resize_geometry_reference as probe
from research.ui.test_resize_geometry_reference import recording, save_mechanics
from research.ui.test_export_mode_state_reference import fixture as mode_fixture


def save(directory, data, synchronize=True):
    if synchronize:
        result = data[probe.RESULT_FILE]
        payloads = [{"kind": "probe_ready", "station_isolated": True, "scenario": "resize-geometry-v1", "case_id": result["case_id"]},
                    {"kind": "readiness", "phase": "ui", "ready": True, "has_tree": True,
                     "has_engine": True, "has_editor": False, "peer_ready": False}]
        payloads.extend({"kind": "resize_geometry_snapshot", **copy.deepcopy(value)} for value in result["snapshots"])
        payloads.append({"kind": "resize_geometry_result", **copy.deepcopy(result)})
        data["evidence.json"]["data"]["evidence"]["normalized_result"]["messages"] = [
            {"type": "send", "script_id": "synthetic-script", "payload": value} for value in payloads]
    for name, value in data.items():
        (directory / name).write_text(json.dumps(value), encoding="utf-8")


def fixture(root, rows=14, actual_height=569, widgets=False, unchanged=False):
    directory, data, _ = mode_fixture(root)
    data.pop("mode-state-result.json")
    for path in directory.glob("ack-*"):
        path.unlink()
    peer, result = recording(rows=rows, actual_height=actual_height, widgets=widgets)
    if unchanged:
        peer.update(naws_history=[[91,20]], after_event_count=1, frozen_grid=[91,20], columns=91, rows=20)
        result["observations"][1]["peer"].update(grid=[91,20], event_count=1)
        result["observations"][1]["terminal"]["capacity"] = 20
    save_mechanics(directory, peer, result)
    data.update({name: exporter.read(directory / name) for name in ("cleanup.json", "profile-check.json")})
    data.update({"peer.json": peer, probe.RESULT_FILE: result})
    plan = {**probe.fixed_contract(), "execution_authorized": True, "out": str(directory), "source": str(root / "official")}
    run = data["run.json"]
    run.update(sha256=probe.EXE_SHA256, files_sha256=probe.MANIFEST_SHA256)
    template = (probe.ROOT / "research/ui/qt_r1_text.js").read_text(encoding="utf-8").split("// Scenario driver", 1)[0]
    template += probe.probe_fragment()
    run["template_sha256"] = hashlib.sha256(template.encode()).hexdigest()
    generated = exporter.expected_probe(plan, run, probe.ROOT)
    (directory / "probe.js").write_bytes(generated)
    run["script_sha256"] = hashlib.sha256(generated).hexdigest()
    data["evidence.json"]["data"]["evidence"]["normalized_result"]["source_sha256"] = run["script_sha256"]
    save(directory, data)
    (directory / "mechanics-check.json").write_text(json.dumps(probe.audit_mechanics(directory)), encoding="utf-8")
    return directory, data, plan


@contextmanager
def complete_fixture(root):
    """Inert five-file package pins exercise the real identity checker."""
    official = root / "official"
    official.mkdir()
    for name in exporter.common.MODULES:
        (official / name).write_bytes(("inert geometry module "+name).encode())
    files = {name: exporter.sha(official / name) for name in exporter.common.MODULES}
    manifest = hashlib.sha256(json.dumps(files, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    with ExitStack() as patches:
        patches.enter_context(patch.object(probe, "EXE_SHA256", files["WindTerm.exe"]))
        patches.enter_context(patch.object(probe, "MANIFEST_SHA256", manifest))
        patches.enter_context(patch.dict(exporter._PROVENANCE, FILE_COUNT=len(files)))
        directory, data, plan = fixture(root, widgets=True)
        (directory / "app").mkdir()
        for name in files:
            (directory / "app" / name).write_bytes((official / name).read_bytes())
        (directory / "files.json").write_text(json.dumps(files), encoding="utf-8")
        sessions = directory / "profile/.wind/profiles/default.v10/terminal/user.sessions"
        sessions.parent.mkdir(parents=True)
        sessions.write_text(json.dumps([{"session.group": "TermForge Lab", "session.label": "TF_R1_LOOPBACK",
            "session.protocol": "Telnet", "session.target": "127.0.0.1", "session.port": 12345,
            "session.uuid": "00000000-0000-4000-8000-000000000001"}]), encoding="utf-8")
        source_root = root / "source-archive"
        for name in probe.SOURCE_FILES:
            path = source_root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes((probe.ROOT / name).read_bytes())
        archive = {"schema": probe.common.ARCHIVE_SCHEMA, "source_base_commit": "b"*40,
                   "source_files_sha256": {name: exporter.sha(source_root / name) for name in probe.SOURCE_FILES}}
        (source_root / probe.common.ARCHIVE_MANIFEST).write_text(json.dumps(archive), encoding="utf-8")
        plan.update(probe.common.source_identity(root=source_root, source_files=probe.SOURCE_FILES))
        plan.update(launcher_source_sha256=hashlib.sha256(probe.launcher_source(probe.CASE_ID).encode()).hexdigest(),
                    windows_version="10.0.26300", python_version="3.12.10 (synthetic runtime)", python_sha256="c"*64,
                    rea_prepared_identity={"version": "6.7.0", "node_executable_sha256": "d"*64,
                        "shim_sha256": "e"*64, "package_json_sha256": "f"*64, "entry_sha256": "0"*64})
        plan_path = root / "plan.json"
        plan_path.write_text(json.dumps(plan), encoding="utf-8")
        plan_path.with_suffix(".attempted").write_text(probe.ATTEMPT, encoding="utf-8")
        yield directory, data, plan, plan_path, source_root, official


class GeometryExportTests(unittest.TestCase):
    def test_actual_rows_height_and_optional_widgets_are_observations(self):
        for rows, height, widgets in ((1,569,False),(10,569,True),(14,569,True),(20,749,False),(200,600,False)):
            with self.subTest(rows=rows,height=height,widgets=widgets), tempfile.TemporaryDirectory() as temporary:
                directory,data,plan = fixture(Path(temporary).resolve(), rows, height, widgets)
                result = exporter.capture_facts(directory,plan)
                self.assertEqual(result["observations"],data[probe.RESULT_FILE]["observations"])
                self.assertEqual(result["snapshots"],data[probe.RESULT_FILE]["snapshots"])
                self.assertEqual(result["observations"][1]["peer"]["grid"],[91,rows])
                self.assertEqual(result["observations"][1]["window"]["size"],[1360,height])
                self.assertEqual(result["widget_geometry_complete"],widgets)
                self.assertEqual([result[name] for name in ("terminal_input_bytes","DSR_requests","dsr_request_bytes","application_reply_bytes")],[0]*4)
                self.assertEqual(result["application_input_sha256"],hashlib.sha256(b"").hexdigest())
                self.assertNotIn(str(directory),json.dumps(result))

    def test_unchanged_grid_without_new_NAWS_is_not_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory,_,plan=fixture(Path(temporary).resolve(),rows=20,actual_height=749,unchanged=True)
            result=exporter.capture_facts(directory,plan)
            self.assertEqual(result["naws_history"],[[91,20]])
            self.assertEqual(result["resize_boundary"],{"before_event_count":1,"after_event_count":1})

    def test_recursive_whitelist_rejects_private_fields(self):
        for target in ("result","snapshot","window","peer","terminal","widgets","roles","node"):
            with self.subTest(target=target),tempfile.TemporaryDirectory() as temporary:
                directory,data,plan=fixture(Path(temporary).resolve(),widgets=True)
                result=data[probe.RESULT_FILE];value=result["observations"][1]
                dest=result if target=="result" else value if target=="snapshot" else value["widgets"]["nodes"][0] if target=="node" else value["widgets"]["roles"] if target=="roles" else value[target]
                dest["private_path"]="C:/private/SECRET"
                save(directory,data)
                with self.assertRaises(ValueError):exporter.capture_facts(directory,plan)

    def test_private_enum_values_and_ambiguous_scalar_types_fail(self):
        for target,key,value in (("window","error","SECRET"),("terminal","protocol","SECRET"),
            ("terminal","capacity",True),("widgets","error","SECRET"),("peer","error","SECRET"),
            ("peer","event_count",True),("snapshot","phase",True),("snapshot","reason","SECRET"),
            ("snapshot","elapsed_ms",float("nan")),("node","role","SECRET"),("node","id",False),
            ("node","is_splitter",1),("node","global_origin",[0,"SECRET"]),("result","terminal_input_bytes",False)):
            with self.subTest(target=target,key=key),tempfile.TemporaryDirectory() as temporary:
                directory,data,plan=fixture(Path(temporary).resolve(),widgets=True)
                result=data[probe.RESULT_FILE];snapshot=result["observations"][1]
                dest=result if target=="result" else snapshot if target=="snapshot" else snapshot["widgets"]["nodes"][0] if target=="node" else snapshot[target]
                dest[key]=value;save(directory,data)
                with self.assertRaises(ValueError):exporter.capture_facts(directory,plan)

    def test_no_terminal_payload_reply_or_DSR_can_be_added(self):
        for target,key,value in (("peer","received_hex","1b5b313b3152"),("peer","application_sent_bytes",4),
                                ("peer","sent_sha256","a"*64),("result","terminal_input_bytes",4)):
            with self.subTest(key=key),tempfile.TemporaryDirectory() as temporary:
                directory,data,plan=fixture(Path(temporary).resolve())
                data["peer.json" if target=="peer" else probe.RESULT_FILE][key]=value;save(directory,data)
                with self.assertRaises(ValueError):exporter.capture_facts(directory,plan)

    def test_failed_partial_mechanics_ack_and_cleanup_are_rejected(self):
        for changed in ("failed","partial","ack","ready","stderr","launcher","cleanup","profile","saved-audit","axis","event","stable-time","capacity","snapshot-bound"):
            with self.subTest(changed=changed),tempfile.TemporaryDirectory() as temporary:
                directory,data,plan=fixture(Path(temporary).resolve())
                result=data[probe.RESULT_FILE]
                if changed=="failed":result.update(status="failed",error="deadline")
                elif changed=="partial":result["observations"].pop()
                elif changed=="ack":(directory/"ack-1").write_text("not observed")
                elif changed=="ready":(directory/"geometry-ready-1").write_text("{}")
                elif changed=="stderr":(directory/"stderr.txt").write_bytes(b"fault")
                elif changed=="launcher":(directory/"launcher-failure.json").write_text("{}")
                elif changed=="cleanup":data["cleanup.json"]["active"]=1
                elif changed=="profile":data["profile-check.json"]["session_count"]=2
                elif changed=="saved-audit":(directory/"mechanics-check.json").write_text("{}")
                elif changed=="axis":data["peer.json"]["naws_history"][-1]=[90,14]
                elif changed=="event":result["snapshots"][1]["peer"]["event_count"]=2
                elif changed=="stable-time":result["snapshots"][-1]["elapsed_ms"]=result["snapshots"][1]["elapsed_ms"]+599
                elif changed=="capacity":result["observations"][1]["terminal"]["capacity"]=15
                else:result["snapshots"]*=33
                save(directory,data)
                with self.assertRaises(ValueError):exporter.capture_facts(directory,plan)

    def test_widget_graph_cycle_geometry_and_missing_roles_fail(self):
        for changed in ("cycle","role","size","origin","count"):
            with self.subTest(changed=changed),tempfile.TemporaryDirectory() as temporary:
                directory,data,plan=fixture(Path(temporary).resolve(),widgets=True)
                widgets=data[probe.RESULT_FILE]["observations"][1]["widgets"]
                if changed=="cycle":widgets["nodes"][0]["parent_id"]=2
                elif changed=="role":widgets["roles"]["viewport"]=1
                elif changed=="size":widgets["nodes"][0]["size"][0]+=1
                elif changed=="origin":widgets["nodes"][0]["global_origin"][0]=1000001
                else:widgets["nodes"]*=22
                save(directory,data)
                with self.assertRaises(ValueError):exporter.capture_facts(directory,plan)

    def test_provider_identity_template_and_partial_recording_fail(self):
        for changed in ("exe","manifest","template","script","evidence","truncated","outer","provider","source-path","provider-snapshot","provider-ready","script-error"):
            with self.subTest(changed=changed),tempfile.TemporaryDirectory() as temporary:
                directory,data,plan=fixture(Path(temporary).resolve())
                evidence=data["evidence.json"]["data"]["evidence"];normalized=evidence["normalized_result"]
                if changed=="exe":data["run.json"]["sha256"]="0"*64
                elif changed=="manifest":data["run.json"]["files_sha256"]="0"*64
                elif changed=="template":data["run.json"]["template_sha256"]="0"*64
                elif changed=="script":(directory/"probe.js").write_bytes(b"changed")
                elif changed=="evidence":evidence["evidence_id"]="SECRET"
                elif changed=="truncated":normalized["messages_truncated"]=True
                elif changed=="outer":data["evidence.json"]["ok"]=False
                elif changed=="provider":evidence["provider"]["id"]="other"
                elif changed=="source-path":normalized["source_path"]="C:/private/other.js"
                elif changed=="provider-snapshot":normalized["messages"].pop(3)
                elif changed=="provider-ready":normalized["messages"][0]["payload"]["case_id"]="wrong"
                else:normalized["messages"][2]["type"]="error"
                save(directory,data,synchronize=not changed.startswith("provider-") and changed!="script-error")
                with self.assertRaises(ValueError):exporter.capture_facts(directory,plan)

    def test_raw_private_metadata_is_never_forwarded(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory,data,plan=fixture(Path(temporary).resolve(),widgets=True)
            before=exporter.capture_facts(directory,plan)
            for name in ("peer.json","run.json","cleanup.json","evidence.json"):
                data[name]["private_path"]="C:/private/SECRET"
            save(directory,data)
            self.assertEqual(exporter.capture_facts(directory,plan),before)

    def test_duplicate_nonfinite_and_oversized_json_are_rejected(self):
        for value in ('{"a":1,"a":2}','{"a":NaN}','{"a":Infinity}', ' '* (4*1024*1024+1)):
            with self.subTest(value=value[:30]),tempfile.TemporaryDirectory() as temporary:
                path=Path(temporary)/"data.json";path.write_text(value,encoding="utf-8")
                with self.assertRaises(ValueError):exporter.read(path)


class CompleteGeometryExportTests(unittest.TestCase):
    def test_complete_archive_publishes_only_geometry_and_hashes(self):
        with tempfile.TemporaryDirectory() as temporary,complete_fixture(Path(temporary).resolve()) as values:
            directory,_,_,plan_path,source_root,official=values
            destination=Path(temporary)/"public/result.json"
            with patch.object(probe.common.subprocess,"run",side_effect=AssertionError("No Git or target execution")):
                result=exporter.export(directory,plan_path,destination,exporter.sha(plan_path),source_root,official)
            self.assertEqual(result["schema"],"termforge-original-resize-geometry-reference-v1")
            self.assertFalse(result["semantic_acceptance"])
            self.assertIsNone(result["height_policy_classification"])
            self.assertEqual(result["actual_original_runs"],1)
            self.assertEqual(len(result["provenance"]["execution_source_files_sha256"]),9)
            self.assertEqual(set(result["offline_export_source_files_sha256"]),set(exporter.EXPORT_SOURCES))
            self.assertEqual(set(result["provenance"]["raw_files"]),set(exporter.RAW_NAMES))
            public=destination.read_text(encoding="utf-8")
            for forbidden in (str(directory),str(source_root),"private_path",'"pid"','"window_station"','"profile"','"lines"','"ptr"'):
                self.assertNotIn(forbidden,public)
            with self.assertRaisesRegex(ValueError,"must be new"):
                exporter.export(directory,plan_path,destination,exporter.sha(plan_path),source_root,official)

    def test_plan_attempt_source_package_and_profile_identity_fail_closed(self):
        for changed in ("plan-sha","contract","authorization","attempt","source","archive","launcher","exe-copy","module-source","manifest","session","rea","inventory"):
            with self.subTest(changed=changed),tempfile.TemporaryDirectory() as temporary,complete_fixture(Path(temporary).resolve()) as values:
                directory,data,plan,plan_path,source_root,official=values
                expected=exporter.sha(plan_path)
                if changed=="plan-sha":expected="0"*64
                elif changed=="contract":plan["controller_contract"]["initial_grid"]=[91,True]
                elif changed=="authorization":plan["execution_authorized"]=False
                elif changed=="attempt":plan_path.with_suffix(".attempted").write_text("retry")
                elif changed=="source":(source_root/probe.SOURCE_FILES[0]).write_bytes(b"changed")
                elif changed=="archive":(source_root/probe.common.ARCHIVE_MANIFEST).write_text("{}")
                elif changed=="launcher":plan["launcher_source_sha256"]="0"*64
                elif changed=="exe-copy":(directory/"app/WindTerm.exe").write_bytes(b"changed")
                elif changed=="module-source":(official/"Qt5Core.dll").write_bytes(b"changed")
                elif changed=="manifest":(directory/"files.json").write_text("{}")
                elif changed=="session":(directory/"profile/.wind/profiles/default.v10/terminal/user.sessions").write_text("[]")
                elif changed=="rea":plan["rea_prepared_identity"]["version"]="6.8.0"
                else:plan["source_files_sha256"].pop(probe.SOURCE_FILES[0])
                if changed in ("contract","authorization","launcher","rea","inventory"):
                    plan_path.write_text(json.dumps(plan));expected=exporter.sha(plan_path)
                destination=Path(temporary)/"rejected.json"
                with self.assertRaises((ValueError,KeyError)):
                    exporter.export(directory,plan_path,destination,expected,source_root,official)
                self.assertFalse(destination.exists())


if __name__ == "__main__":
    unittest.main()
