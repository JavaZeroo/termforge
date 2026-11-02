"""Geometry peer/controller fixtures; in-memory connection, no target or sockets."""
import copy
import hashlib
import json
from pathlib import Path
import shutil
import socket
import tempfile
import unittest
from unittest.mock import patch
from research.ui import resize_geometry_reference as probe


def naws(columns,rows):return bytes([255,250,31])+columns.to_bytes(2,"big")+rows.to_bytes(2,"big")+bytes([255,240])


class Connection:
    def __init__(self,peer,mode,rows):self.peer,self.mode,self.rows=peer,mode,rows;self.first=True;self.sent=[];self.applied=False;self.calls=0
    def settimeout(self,_):pass
    def close(self):pass
    def sendall(self,data):self.sent.append(data)
    def recv(self,_):
        self.calls+=1
        if self.calls>20:raise RuntimeError("Synthetic receive bound")
        s,d=self.peer.state,self.peer.directory
        if self.first:self.first=False;return naws(91,20)
        if s.get("complete"):return b""
        if self.mode=="foreign-input":return b"SECRET"
        if self.mode=="unarmed":return naws(91,19)
        if s["phase"]==0 and not s["resize_armed"]:
            if self.mode!="no-ack-0":(d/"ack-0").write_text("observed")
            (d/"resize-request-1").write_text(json.dumps({"phase":1,"window":[1360,570 if self.mode=="wrong-request" else 569]}))
            raise socket.timeout()
        if s["resize_armed"] and not self.applied:
            self.applied=True
            grid=[92 if self.mode=="wrong-axis" else 91,self.rows]
            if self.mode=="no-change":grid=[91,20]
            extra=0 if self.mode in ("no-change","stale") else 1
            (d/"geometry-ready-1").write_text(json.dumps({"phase":1,"grid":grid,"event_count":len(s["naws_history"])+extra}))
            if self.mode!="no-ack-1":(d/"ack-1").write_text("observed")
            if self.mode=="no-change":raise socket.timeout()
            return naws(*grid)
        raise socket.timeout()


def receive(mode="normal",rows=14):
    with tempfile.TemporaryDirectory() as tmp:
        cls=probe.peer_class();peer=cls.__new__(cls);peer.directory=Path(tmp)
        peer.state={"connected":False,"columns":None,"rows":None,"received_hex":"","sent_sha256":None,
          "application_sent_bytes":0,"naws_history":[],"phase":None,"resize_armed":False,"case_id":probe.CASE_ID}
        peer.stop=type("NeverStopped",(),{"is_set":lambda _:False})()
        (peer.directory/"fixture-ready").write_text(json.dumps({"grid":[91,20],"event_count":0 if mode=="stale-initial" else 1}))
        connection=Connection(peer,mode,rows)
        peer.listener=type("Listener",(),{"accept":lambda _:(connection,("127.0.0.1",12345))})()
        peer._serve();return peer.state,connection.sent


def widget_rows():
    nodes=[]
    for index,role in enumerate(("terminal","editor","viewport")):
        nodes.append({"id":index,"parent_id":None if index==0 else index-1,"role":role,"rect":[0,0,799,359],"size":[800,360],
                      "global_origin":[100,100],"is_splitter":False,"is_handle":False,"visible":True})
    return {"status":"observed","error":None,"nodes":nodes,"roles":{"terminal":0,"editor":1,"viewport":2}}


def snapshot(index,phase,reason,rows,height,widgets=False):
    return {"sequence":index,"phase":phase,"reason":reason,"elapsed_ms":1000+index*700,"requested_window":list(probe.WINDOWS[phase]),
      "window":{"rect":[102,102,1461,101+height],"size":[1360,height],"error":None},
      "peer":{"grid":[91,rows],"event_count":1 if phase==0 else 2,"resize_armed":phase==1,"error":None},
      "terminal":{"connected":True,"protocol":"Telnet","capacity":rows,"line_count":0,"error":None},
      "widgets":widget_rows() if widgets else {"status":"unavailable","error":"terminal_not_unique","nodes":[],"roles":{"terminal":None,"editor":None,"viewport":None}}}


def recording(case=probe.CASE_ID,rows=14,actual_height=569,widgets=False):
    probe.validate_case(case);peer,_=receive(rows=rows)
    first=snapshot(0,0,"initial-stable",20,749,widgets)
    request=snapshot(1,1,"request-returned",20,actual_height,widgets);request["peer"]["event_count"]=1
    last=snapshot(2,1,"final-stable",rows,actual_height,widgets)
    return peer,{"schema":"termforge-resize-geometry-result-v1","status":"observation","error":None,"case_id":case,
      "semantic_acceptance":False,"requested_windows":[list(v) for v in probe.WINDOWS],"terminal_input_bytes":0,
      "observations":[first,last],"snapshots":[first,request,last],"widget_geometry_complete":widgets}


def save_mechanics(directory,peer,result):
    files={"peer.json":peer,probe.RESULT_FILE:result,"cleanup.json":{"active":0,"total":1,"terminated":1},
      "profile-check.json":{"only_synthetic_session":True,"session_count":1},
      "fixture-ready":{"grid":[91,20],"event_count":peer["freeze_event_count"]},
      "resize-request-1":{"phase":1,"window":[1360,569]},
      "geometry-ready-1":{"phase":1,"grid":peer["frozen_grid"],"event_count":peer["after_event_count"]}}
    for name,value in files.items():(directory/name).write_text(json.dumps(value),encoding="utf-8")
    (directory/"stderr.txt").write_bytes(b"")
    for index in (0,1):(directory/f"ack-{index}").write_text("observed")


class GeometryTests(unittest.TestCase):
    def test_fixed_request_and_no_terminal_input(self):
        plan=probe.fixed_contract();self.assertEqual(plan["required_final_rows"],None)
        self.assertEqual(plan["windows"],[[1360,749],[1360,569]])
        self.assertEqual(plan["terminal_input_bytes"],0);self.assertEqual(plan["DSR_requests"],0)
        for rows in (1,10,14,20,200):
            state,sent=receive(rows=rows);self.assertTrue(state.get("complete"),state)
            self.assertEqual(state["frozen_grid"],[91,rows]);self.assertEqual(sent,[bytes([255,251,1,255,251,3,255,253,31])])
    def test_no_NAWS_change_is_still_an_actual_measurement(self):
        state,sent=receive("no-change")
        self.assertTrue(state["complete"]);self.assertEqual(state["before_event_count"],state["after_event_count"])
    def test_wrong_axis_foreign_input_and_stale_handshakes_stop(self):
        for mode in ("foreign-input","unarmed","wrong-axis","wrong-request","stale","stale-initial","no-ack-0","no-ack-1"):
            with self.subTest(mode=mode):
                state,_=receive(mode);self.assertFalse(state.get("complete",False));self.assertTrue(state.get("error"))
        state,_=receive(rows=201);self.assertTrue(state.get("error"))
    def test_strict_mechanics_preserves_unexpected_actual_height_and_optional_widget_failure(self):
        for rows,height,widgets in ((14,569,False),(14,641,True),(20,749,False)):
            with tempfile.TemporaryDirectory() as tmp:
                directory=Path(tmp);peer,result=recording(rows=rows,actual_height=height,widgets=widgets)
                save_mechanics(directory,peer,result)
                audit=probe.audit_mechanics(directory)
                self.assertEqual(audit["widget_geometry_complete"],widgets);self.assertFalse(audit["semantic_acceptance"])
    def test_mechanics_rejects_missing_measurements_and_unsafe_graphs(self):
        for what in ("baseline","capacity","phase","input","ack","window","graph-cycle","wrong-role","private-error","time","stale-sample","short-stability","bool-phase"):
            with self.subTest(what=what),tempfile.TemporaryDirectory() as tmp:
                directory=Path(tmp);peer,result=recording(widgets=True)
                if what=="baseline":result["observations"][0]["peer"]["grid"]=[91,19]
                elif what=="capacity":result["observations"][1]["terminal"]["capacity"]=10
                elif what=="phase":result["observations"].pop()
                elif what=="input":peer["application_sent_bytes"]=1
                elif what=="window":result["snapshots"][1]["window"]["rect"][3]+=1
                elif what=="graph-cycle":result["snapshots"][1]["widgets"]["nodes"][0]["parent_id"]=2
                elif what=="wrong-role":result["snapshots"][1]["widgets"]["roles"]["viewport"]=0
                elif what=="private-error":result["snapshots"][1]["widgets"]["error"]="SECRET"
                elif what=="time":result["snapshots"][1]["elapsed_ms"]=-1
                elif what=="stale-sample":result["snapshots"][1]["peer"]["event_count"]=2
                elif what=="short-stability":result["observations"][1]["elapsed_ms"]=1900
                elif what=="bool-phase":result["snapshots"][0]["phase"]=False
                save_mechanics(directory,peer,result)
                if what=="ack":(directory/"ack-1").write_text("wrong")
                with self.assertRaises(ValueError):probe.audit_mechanics(directory)
    def test_peer_namespace_does_not_mutate_frozen_source(self):
        path=probe.ROOT/"research/ui/r1_vt_fixture.py";before=path.read_bytes()
        a,b=probe.peer_class(),probe.peer_class();self.assertIsNot(a._serve.__globals__,b._serve.__globals__)
        self.assertEqual(path.read_bytes(),before);compile(probe.launcher_source(),"<launcher>","exec")


class GeometryPlanTests(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory(prefix="resize-geometry-tests-");self.addCleanup(temporary.cleanup)
        self.root=Path(temporary.name).resolve()
        for name in probe.SOURCE_FILES:
            target=self.root/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(probe.ROOT/name,target)
        archive={"schema":probe.common.ARCHIVE_SCHEMA,"source_base_commit":"b"*40,
                 "source_files_sha256":{name:probe.sha(self.root/name) for name in probe.SOURCE_FILES}}
        (self.root/"reference-source.json").write_text(json.dumps(archive))
        self.source=self.root/"original";self.source.mkdir();(self.source/"WindTerm.exe").write_bytes(b"synthetic, not an executable")
        for target,name,value in ((probe,"ROOT",self.root),(probe,"EXE_SHA256",probe.sha(self.source/"WindTerm.exe")),
                                  (probe.common,"host_identity",lambda:{"synthetic_host":True}),
                                  (probe.common,"rea_identity",lambda _: {"node_executable":"inert-node"})):
            patcher=patch.object(target,name,value);patcher.start();self.addCleanup(patcher.stop)
        self.out=self.root/"local/out";self.plan=self.root/"local/plan.json"

    def prepare(self,authorized=False):
        return probe.prepare_plan(self.source,self.out,self.root/"rea.cmd",self.plan,"height-geometry",authorized)

    def test_archive_plan_is_offline_and_exactly_nine_source_files(self):
        with patch.object(probe.common.subprocess,"check_output",side_effect=AssertionError("No archive Git")):
            plan=self.prepare()
        self.assertEqual(len(plan["source_files_sha256"]),9)
        self.assertEqual(plan["actual_original_runs"],0);self.assertFalse(self.out.exists())
        with self.assertRaisesRegex(ValueError,"not authorized"):probe.run_plan(self.plan)
        self.assertFalse(self.plan.with_suffix(".attempted").exists())

    def test_changed_case_geometry_or_source_is_rejected_before_attempt(self):
        plan=self.prepare(True)
        for key,value in (("case_id","width-then-height"),("phase_count",99),("initial_grid",[]),("controller_contract",{}),
                          ("source_identity",{"kind":"git-checkout"}),("launcher_source_sha256","0"*64)):
            self.plan.write_text(json.dumps({**plan,key:value}))
            with self.subTest(key=key),self.assertRaises(ValueError):probe.run_plan(self.plan)
            self.assertFalse(self.plan.with_suffix(".attempted").exists())

    def test_local_boundary_existing_plan_and_existing_attempt_remain_enforced(self):
        with self.assertRaisesRegex(ValueError,"local directory"):
            probe.prepare_plan(self.source,self.root/"elsewhere",self.root/"rea.cmd",self.plan,"height-geometry")
        with patch.object(probe,"launcher_source",return_value="def run(*args):\n    pass\n"),patch.object(probe,"audit_mechanics",return_value={}):
            self.prepare(True)
            with self.assertRaisesRegex(ValueError,"must be new"):self.prepare(True)
            # A fake launcher namespace keeps this test independent of target APIs.
            self.out.mkdir()
            self.out.rmdir()
            with patch.object(probe.Path,"write_text",autospec=True) as write_result:
                probe.run_plan(self.plan)
                write_result.assert_called_once()
            with self.assertRaises(FileExistsError):probe.run_plan(self.plan)

    def test_failed_attempt_is_retained_and_cannot_be_replayed(self):
        source = "def run(*args):\n    raise RuntimeError('synthetic launch failure')\n"
        with patch.object(probe,"launcher_source",return_value=source),patch.object(probe,"audit_mechanics") as audit:
            self.prepare(True)
            with self.assertRaisesRegex(RuntimeError,"synthetic launch failure"):
                probe.run_plan(self.plan)
            self.assertEqual(self.plan.with_suffix(".attempted").read_text(),probe.ATTEMPT)
            self.assertFalse(self.out.exists());audit.assert_not_called()
            with self.assertRaises(FileExistsError):probe.run_plan(self.plan)


if __name__ == "__main__":
    unittest.main()
