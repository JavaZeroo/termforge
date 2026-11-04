"""Fixed hidden-primary height sources, actual protected peer and plan guards; no sockets/target."""
import hashlib
import json
from pathlib import Path
import shutil
import socket
import tempfile
import unittest
from unittest.mock import patch

from research.ui import resize_hidden_height_reference as probe


def naws(columns, rows):
    return bytes([255,250,31])+columns.to_bytes(2,"big")+rows.to_bytes(2,"big")+bytes([255,240])


class Connection:
    def __init__(self, peer, mode):
        self.peer, self.mode, self.case = peer, mode, peer.case_id
        self.initial, self.fragment, self.sent, self.resize_sent, self.calls = True, None, [], set(), 0
    def settimeout(self, _): pass
    def close(self): pass
    def sendall(self, data): self.sent.append(data)
    def recv(self, _):
        self.calls += 1
        if self.calls > 100: raise RuntimeError("Synthetic receive bound")
        if self.initial:
            self.initial = False
            return naws(92 if self.mode == "stale-initial" else 91, 20)
        state, directory = self.peer.state, self.peer.directory
        armed = state.get("resize_armed")
        if armed and armed not in self.resize_sent:
            self.resize_sent.add(armed)
            before = probe.required_grids(self.case)[armed-1]
            grid = list(probe.required_grids(self.case)[armed])
            axis = 0 if before[0] != grid[0] else 1
            middle = list(grid); middle[axis] = (before[axis]+grid[axis])//2
            if self.mode == "wrong-axis": grid[1-axis] += 1
            if self.mode == "outside-interval": grid[axis] = max(before[axis],grid[axis])+1
            if self.mode == "wrong-endpoint": grid = middle
            events = [middle, grid] if self.mode == "batched-resize" else [grid]
            count = len(state["naws_history"])+(0 if self.mode == "stale-ready" else len(events))
            (directory/f"resize-ready-{armed}").write_text(json.dumps({"phase":armed,"grid":grid,"event_count":count}))
            return b"".join(naws(*event) for event in events)
        record = state["records"][-1]
        if self.mode == "unarmed": return naws(91,19)
        if self.mode == "unarmed-switch-"+str(record["phase"]): return naws(60,20)
        if record["cursor_reply"] is not None:
            if self.mode == "no-ack-"+str(record["phase"]):
                raise RuntimeError("Synthetic withheld acknowledgment")
            (directory/f'ack-{record["phase"]}').write_text("observed")
            target = record["phase"]+1
            if target in probe.resize_phases(self.case):
                window = list(probe.windows(self.case)[target])
                if self.mode == "wrong-request": window[1] += 1
                (directory/f"resize-request-{target}").write_text(json.dumps({"phase":target,"window":window}))
            raise socket.timeout()
        if self.mode == "duplicate-reply": return b"\x1b[19;7R\x1b[19;7R"
        if self.mode == "foreign-input": return b"SECRET"
        if self.fragment is not None:
            result, self.fragment = self.fragment, None
            return result
        self.fragment = (b"8;501R" if self.mode == "dsr-overflow" else
                         b"0;7R" if self.mode == "dsr-zero" else
                         b"18;7R" if self.mode == "wrong-initial-dsr" else b"19;7R")
        return b"\x1b["


def receive(case="hidden-primary-height", mode="normal"):
    with tempfile.TemporaryDirectory() as temporary:
        peer_type = probe.peer_class(case); peer = peer_type.__new__(peer_type)
        peer.directory = Path(temporary).resolve()
        peer.case_id = case
        (peer.directory/"fixture-ready").write_text(json.dumps({"grid":[91,20],"event_count":1}))
        peer.state = {"connected":False,"columns":None,"rows":None,"phase":None,"records":[]}
        peer.stop = type("NeverStopped",(),{"is_set":lambda _:False})()
        connection = Connection(peer,mode)
        peer.listener = type("Listener",(),{"accept":lambda _:(connection,("127.0.0.1",12345))})()
        peer._serve()
        return peer.state, connection.sent



def initial_lines():
    lines = [""]*20
    lines[1]="HTOP";lines[5]="HARD_A";lines[6]="HARD_B"
    lines[11]=" "*89+"uvw";lines[18]="LOWEND"
    return lines


def recording(case="hidden-primary-height"):
    state, _ = receive(case, "batched-resize")
    result={"status":"observed-fixed-resize-hidden-height-states","case_id":case,"error":None,"observations":[],
            "window_rect_format":"qt-qrect-inclusive-ltrb","geometry_checks":[],"full_active_grid_verified":False,
            "local_shell_transport_verified":False,"presented_done":None}
    for record,window in zip(state["records"],probe.windows(case)):
        phase=record["phase"];lines=initial_lines()
        rect=[113,-27,112+window[0],-28+window[1]]
        terminal={"connected":True,"protocol":"Telnet","capacity":probe.required_grids(case)[phase][1],
                  "line_count":len(lines),"lines":lines,"markers":[],"top":None,"scroll":0,
                  "caret_api":{"caretLine":18,"caretColumn":6,"caretPosition":200}}
        result["observations"].append({**{k:record[k] for k in ("phase","label","frozen_grid","cursor_reply","cursor_reply_private","response_hex")},
                                      "terminal":terminal,"window_rect":rect,"window_size":list(window)})
        result["geometry_checks"].append({"phase":phase,"window_rect":rect,"window_size":list(window),"expected_size":list(window),
                                          "observed_grid":list(probe.required_grids(case)[phase]),"terminal_capacity":terminal["capacity"],
                                          "event_count":state["freeze_event_counts"][phase]})
    return state,result


def save_mechanics(directory, state, result):
    for name,value in (("peer.json",state),(probe.RESULT_FILE,result),("cleanup.json",{"active":0,"total":1,"terminated":1}),
                       ("profile-check.json",{"only_synthetic_session":True,"session_count":1})):
        (directory/name).write_text(json.dumps(value),encoding="utf-8")
    (directory/"stderr.txt").write_bytes(b"")
    for phase in range(len(result["observations"])):(directory/f"ack-{phase}").write_text("observed")


class HiddenHeightReferenceTests(unittest.TestCase):
    def test_fixed_hidden_height_geometry_and_exact_1049_actions(self):
        self.assertEqual([len(probe.phases(c)) for c in probe.CASES],[7])
        expected=(b"\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J\x1b[2;1HHTOP\x1b[6;1HHARD_A\r\nHARD_B"
                  b"\x1b[12;90Huvw\x1b[19;1HLOWEND")
        for case in probe.CASES:
            self.assertEqual(probe.phases(case)[0][1],expected)
            self.assertEqual([value for _,value in probe.phases(case)[1:]],
                             [b"",b"\x1b[?1049h",b"",b"",b"\x1b[?1049l",b""])
            self.assertEqual(sum(len(value) for _,value in probe.phases(case)),91)
            self.assertEqual(probe.resize_phases(case),(1,3,4,6))
            self.assertEqual(probe.fixed_contract(case)["semantic_acceptance"],False)
        self.assertEqual(probe.windows("hidden-primary-height"),[(1360,749),(1000,749),(1000,749),(1000,569),(1000,749),(1000,749),(1360,749)])
        self.assertEqual(probe.required_grids("hidden-primary-height"),[(91,20),(61,20),(61,20),(61,14),(61,20),(61,20),(91,20)])
        self.assertEqual(probe.fixed_contract("hidden-primary-height")["geometry_basis"]["combined"]["strict_export_sha256"],"6a1561f3e6f6bc480c6a2f086adf7ed4798d5007b04a7bc71c7ae9deba90cfe6")

    def test_real_peer_fragmented_DSR_and_bounded_single_axis_NAWS(self):
        for case in probe.CASES:
            state,sent=receive(case,"batched-resize")
            self.assertTrue(state["complete"],state.get("error"));self.assertIsNone(state.get("error"))
            self.assertEqual(state["frozen_grids"],[list(g) for g in probe.required_grids(case)])
            self.assertEqual([r["cursor_reply"] for r in state["records"]],[[19,7]]*len(probe.phases(case)))
            self.assertEqual(sent[-len(probe.phases(case)):],[value+b"\x1b[6n" for _,value in probe.phases(case)])

    def test_wrong_axis_interval_endpoint_or_handshake_stops(self):
        for case in probe.CASES:
            for mode in ("wrong-axis","outside-interval","wrong-endpoint","stale-ready","wrong-request",
                         "stale-initial","unarmed","wrong-initial-dsr","duplicate-reply","foreign-input","dsr-overflow","dsr-zero"):
                with self.subTest(case=case,mode=mode):
                    state,_=receive(case,mode);self.assertFalse(state.get("complete",False));self.assertTrue(state.get("error"))

    def test_every_stage_including_1049_requires_ack(self):
        for case in probe.CASES:
            for phase in range(len(probe.phases(case))-1):
                state,_=receive(case,"no-ack-"+str(phase))
                self.assertTrue(state.get("error"));self.assertEqual(len(state["records"]),phase+1)

    def test_single_axis_intervals_never_authorize_both_axes(self):
        self.assertTrue(probe.allowed_grid((91,20),(91,14),(91,15)))
        self.assertTrue(probe.allowed_grid((91,14),(91,20),(91,20)))
        for value in ((90,15),(91,9),(91,21),(0,15),(True,15)):
            self.assertFalse(probe.allowed_grid((91,20),(91,10),value))
        self.assertFalse(probe.allowed_grid((91,20),(61,10),(76,15)))

    def test_initial_fixture_is_mandatory_later_loss_is_only_observation(self):
        terminal={"line_count":20,"lines":initial_lines()}
        for case in probe.CASES:
            probe.validate_document(case,0,terminal)
            for phase in range(1,len(probe.phases(case))):probe.validate_document(case,phase,{"line_count":1,"lines":[""]})
            with self.assertRaisesRegex(ValueError,"Initial fixture"):probe.validate_document(case,0,{"line_count":1,"lines":[""]})
            for lines in (["SECRET"],["HTOP","HTOP"],["\ufffd"],[" "*500],["uvw","uvw"]):
                with self.assertRaises(ValueError):probe.validate_document(case,1,{"line_count":len(lines),"lines":lines})

    def test_1049_stages_keep_independent_ack_and_same_grid_without_resize(self):
        state,_=receive()
        self.assertTrue(state["complete"])
        self.assertEqual(state["freeze_event_counts"],[1,2,2,3,4,4,5])
        self.assertEqual([r["phase"] for r in state["resize_boundaries"]],[1,3,4,6])
        for phase in (2,5):
            with self.subTest(phase=phase):
                stopped,_=receive(mode="unarmed-switch-"+str(phase))
                self.assertFalse(stopped.get("complete",False));self.assertTrue(stopped.get("error"))
                stopped,_=receive(mode="no-ack-"+str(phase))
                self.assertEqual(len(stopped["records"]),phase+1);self.assertTrue(stopped.get("error"))

    def test_empty_alternate_document_is_an_observation_not_a_policy_or_failure(self):
        case="hidden-primary-height"
        with tempfile.TemporaryDirectory() as temporary:
            directory=Path(temporary);state,result=recording(case)
            for phase in (2,3,4):
                result["observations"][phase]["terminal"].update(lines=[""],line_count=1,markers=[])
            save_mechanics(directory,state,result)
            audit=probe.audit_mechanics(directory,case)
            self.assertEqual(audit["stage_count"],7);self.assertFalse(audit["semantic_acceptance"])
            self.assertEqual(state["records"][2]["source_hex"],"1b5b3f3130343968")
            self.assertEqual(state["records"][5]["source_hex"],"1b5b3f313034396c")

    def test_mechanics_rechecks_variable_capacity_fixed_geometry_and_all_boundaries(self):
        for case in probe.CASES:
            with tempfile.TemporaryDirectory() as temporary:
                directory=Path(temporary);state,result=recording(case);save_mechanics(directory,state,result)
                self.assertEqual(probe.audit_mechanics(directory,case)["stage_count"],len(probe.phases(case)))
                bad=json.loads(json.dumps(result));bad["observations"][1]["terminal"]["capacity"]+=1
                save_mechanics(directory,state,bad)
                with self.assertRaises(ValueError):probe.audit_mechanics(directory,case)
                bad=json.loads(json.dumps(result));bad["geometry_checks"][1]["window_size"][1]+=1
                save_mechanics(directory,state,bad)
                with self.assertRaises(ValueError):probe.audit_mechanics(directory,case)
                bad=json.loads(json.dumps(state));before=bad["resize_boundaries"][0]["before_event_count"]
                fixed_axis = 1  # The first transition changes width only.
                bad["naws_history"][before][fixed_axis]+=1
                save_mechanics(directory,bad,result)
                with self.assertRaises(ValueError):probe.audit_mechanics(directory,case)
                save_mechanics(directory,state,result);(directory/"ack-0").unlink()
                with self.assertRaises((ValueError,FileNotFoundError)):probe.audit_mechanics(directory,case)

    def test_archived_dependencies_are_unchanged_and_case_namespaces_isolated(self):
        before=probe.baseline.phase_records()
        a,b=probe.peer_class("hidden-primary-height"),probe.peer_class("hidden-primary-height")
        self.assertEqual(len(a._serve.__globals__["phases"](20,91)),7)
        self.assertEqual(len(b._serve.__globals__["phases"](20,91)),7)
        self.assertEqual(probe.baseline.phase_records(),before)
        for case in probe.CASES:
            compile(probe.launcher_source(case),"<offline launcher>","exec")
            self.assertIn(json.dumps(probe.controller_contract(case)),probe.probe_fragment(case))


class HiddenHeightPlanTests(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory(prefix="resize-hidden-height-tests-");self.addCleanup(temporary.cleanup)
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
        return probe.prepare_plan(self.source,self.out,self.root/"rea.cmd",self.plan,"hidden-primary-height",authorized)

    def test_archive_plan_is_offline_and_exactly_ten_source_files(self):
        with patch.object(probe.common.subprocess,"check_output",side_effect=AssertionError("No archive Git")):
            plan=self.prepare()
        self.assertEqual(len(plan["source_files_sha256"]),10)
        self.assertEqual(plan["actual_original_runs"],0);self.assertFalse(self.out.exists())
        with self.assertRaisesRegex(ValueError,"not authorized"):probe.run_plan(self.plan)
        self.assertFalse(self.plan.with_suffix(".attempted").exists())

    def test_changed_case_geometry_or_source_is_rejected_before_attempt(self):
        plan=self.prepare(True)
        for key,value in (("case_id","width-then-height"),("phase_count",99),("required_grids",[]),("phases",[]),
                          ("source_identity",{"kind":"git-checkout"}),("launcher_source_sha256","0"*64)):
            self.plan.write_text(json.dumps({**plan,key:value}))
            with self.subTest(key=key),self.assertRaises(ValueError):probe.run_plan(self.plan)
            self.assertFalse(self.plan.with_suffix(".attempted").exists())

    def test_local_boundary_existing_plan_and_existing_attempt_remain_enforced(self):
        with self.assertRaisesRegex(ValueError,"local directory"):
            probe.prepare_plan(self.source,self.root/"elsewhere",self.root/"rea.cmd",self.plan,"hidden-primary-height")
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
