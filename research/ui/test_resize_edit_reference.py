"""Fixed editing sources, actual protected peer and plan guards; no sockets/target."""
import hashlib
import json
from pathlib import Path
import shutil
import socket
import tempfile
import unittest
from unittest.mock import patch

from research.ui import resize_edit_reference as probe


def naws(columns, rows):
    return bytes([255,250,31])+columns.to_bytes(2,"big")+rows.to_bytes(2,"big")+bytes([255,240])


class Connection:
    def __init__(self, peer, mode):
        self.peer, self.mode = peer, mode
        self.case = peer.case_id
        self.initial, self.fragment, self.sent, self.resize_sent, self.calls = True, None, [], set(), 0
    def settimeout(self, _): pass
    def close(self): pass
    def sendall(self, data): self.sent.append(data)
    def recv(self, _):
        self.calls += 1
        if self.calls > 100:
            raise RuntimeError("Synthetic receive bound")
        if self.initial:
            self.initial = False
            return naws(92 if self.mode == "stale-initial" else 91, 20)
        state, directory = self.peer.state, self.peer.directory
        armed = state.get("resize_armed")
        if armed and armed not in self.resize_sent:
            self.resize_sent.add(armed)
            grid = [61,20] if armed == 1 else [91,20]
            if self.mode == "wrong-shrink" and armed == 1: grid[0] = 62
            if self.mode == "height-change": grid[1] = 21
            if self.mode == "wrong-restore" and armed == len(probe.phases(self.case))-1: grid[0] = 90
            events = [[62,20],[60,20],grid] if self.mode == "batched-resize" and armed == 1 else [grid]
            count = len(state["naws_history"])+(0 if self.mode == "stale-ready" else len(events))
            (directory/f"resize-ready-{armed}").write_text(json.dumps({"phase":armed,"grid":grid,"event_count":count}))
            return b"".join(naws(*event) for event in events)
        record = state["records"][-1]
        if self.mode == "unarmed-action" and record["phase"] == 2:
            return naws(62,20)+naws(61,20)
        if record["cursor_reply"] is not None:
            if self.mode == "no-pre-action-ack" and record["phase"] == 2:
                raise RuntimeError("Synthetic withheld pre-action acknowledgment")
            (directory/f'ack-{record["phase"]}').write_text("observed")
            target = record["phase"]+1
            if target in probe.resize_phases(self.case):
                window = list(probe.windows(self.case)[target])
                if self.mode == "wrong-request": window[0] += 1
                (directory/f"resize-request-{target}").write_text(json.dumps({"phase":target,"window":window}))
            raise socket.timeout()
        if self.mode == "duplicate-reply": return b"\x1b[11;2R\x1b[11;2R"
        if self.mode == "foreign-input": return b"SECRET"
        if self.fragment is not None:
            result, self.fragment = self.fragment, None
            return result
        self.fragment = b"8;61R" if record["phase"] == 2 else b"11;2R"
        return b"\x1b["


def receive(case="right-edge-wrap", mode="normal"):
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



def recording(case="right-edge-wrap"):
    state, _ = receive(case, "batched-resize")
    result = {"status":"observed-fixed-resize-edit-states","case_id":case,"error":None,"observations":[],
              "window_rect_format":"qt-qrect-inclusive-ltrb","geometry_checks":[],"full_active_grid_verified":False,
              "local_shell_transport_verified":False,"presented_done":None}
    for record, window in zip(state["records"],probe.windows(case)):
        rect = [113,-27,112+window[0],-28+window[1]]
        lines = ["","0123456789","ANCHOR","","","",""," "*90+"v",""," "*90+"wx","",""]
        phase = record["phase"]
        if phase >= 2 and case.startswith("erase-"): lines[7] = "L"+" "*89+"v"
        if phase >= 3:
            if case == "right-edge-wrap": lines[7] = " "*60+"Q"+" "*29+"v" if phase == 3 else " "*60+"QR"+" "*28+"v"
            elif case == "erase-right": lines[7] = "L"+" "*90
            elif case == "erase-whole-line": lines[7] = " "*91
            elif case == "insert-char": lines[7] = " "*91+"v"
            elif case == "delete-char": lines[7] = " "*89+"v "
        terminal = {"connected":True,"protocol":"Telnet","capacity":20,"line_count":len(lines),"lines":lines,"markers":[],
                    "top":None,"scroll":0,"caret_api":{"caretLine":7,"caretColumn":60,"caretPosition":200}}
        result["observations"].append({**{k:record[k] for k in ("phase","label","frozen_grid","cursor_reply","cursor_reply_private","response_hex")},
                                      "terminal":terminal,"window_rect":rect,"window_size":list(window)})
        result["geometry_checks"].append({"phase":phase,"window_rect":rect,"window_size":list(window),"expected_size":list(window)})
    return state,result


class EditReferenceTests(unittest.TestCase):
    def test_literal_78_byte_baseline_and_five_independent_edit_sequences(self):
        actions={"right-edge-wrap":[b"Q",b"R"],"erase-right":[b"\x1b[0K"],"erase-whole-line":[b"\x1b[2K"],
                 "insert-char":[b"\x1b[1@"],"delete-char":[b"\x1b[1P"]}
        total=0
        for case,expected in actions.items():
            with self.subTest(case=case):
                rows=probe.phases(case);total+=len(rows)
                self.assertEqual(len(rows[0][1]),78)
                self.assertEqual(rows[0][1],probe.baseline.phases(20,91)[0][1])
                self.assertEqual(rows[1][1],b"");self.assertEqual(rows[-1][1],b"")
                self.assertEqual(rows[2][1],(b"\x1b[8;1HL" if case.startswith("erase-") else b"")+b"\x1b[8;61H")
                self.assertEqual([source for _,source in rows[3:-1]],expected)
                self.assertEqual(probe.fixed_contract(case)["pre_action_phases"],[0,1,2])
        self.assertEqual(total,26)

    def test_actual_peer_exact_input_fragmented_replies_and_only_two_resize_boundaries(self):
        for case in probe.CASES:
            with self.subTest(case=case):
                state,sent=receive(case,"batched-resize");count=len(probe.phases(case))
                self.assertTrue(state["complete"]);self.assertNotIn("error",state)
                self.assertEqual(sent[1:],[source+b"\x1b[6n" for _,source in probe.phases(case)])
                self.assertEqual(state["frozen_grids"],[list(v) for v in probe.required_grids(case)])
                self.assertEqual(state["freeze_event_counts"],[1]+[4]*(count-2)+[5])
                self.assertEqual([v["phase"] for v in state["resize_boundaries"]],[1,count-1])
                self.assertEqual([v["cursor_reply"] for v in state["records"]],[[11,2],[11,2],[8,61]]+[[11,2]]*(count-3))
                self.assertEqual(state["received_hex"],(b"\x1b[11;2R"*2+b"\x1b[8;61R"+b"\x1b[11;2R"*(count-3)).hex())

    def test_wrong_geometry_extra_reply_stale_events_and_unarmed_change_stop(self):
        for mode in ("stale-initial","stale-ready","wrong-shrink","wrong-restore","height-change","wrong-request",
                     "unarmed-action","duplicate-reply","foreign-input"):
            with self.subTest(mode=mode):
                state,_=receive(mode=mode)
                self.assertIn("error",state);self.assertFalse(state.get("complete",False));self.assertTrue(state["closed"])

    def test_preparation_ack_is_required_before_any_edit(self):
        state,sent=receive(mode="no-pre-action-ack")
        self.assertEqual(len(state["records"]),3)
        self.assertEqual(sent[1:],[source+b"\x1b[6n" for _,source in probe.phases("right-edge-wrap")[:3]])
        self.assertNotIn(b"Q\x1b[6n",sent)

    def test_preparation_retains_the_tail_and_only_the_declared_L_prefix(self):
        for case in probe.CASES:
            _,result=recording(case);before=result["observations"][1]["terminal"];prepared=result["observations"][2]["terminal"]
            probe.validate_preparation(case,before,prepared,[8,61])
            with self.assertRaisesRegex(ValueError,"DSR"):probe.validate_preparation(case,before,prepared,[8,60])
            prepared["lines"][7]=""
            with self.assertRaisesRegex(ValueError,"source content"):probe.validate_preparation(case,before,prepared,[8,61])

    def test_finite_intervals_preserve_soft_fragment_erase_and_trailing_blanks(self):
        for case in ("erase-right","erase-whole-line"):
            allowed=probe.synthetic_lines(case,3)
            self.assertIn(" "*90+"w ",allowed)
            self.assertIn(" "*90+" x",allowed)
            self.assertIn("L"+" "*90,allowed)
            self.assertIn("L",allowed)
            self.assertIn("AN  OR",allowed)
        self.assertIn(" "*91+"v",probe.synthetic_lines("insert-char",3))
        self.assertIn(" "*89+"v ",probe.synthetic_lines("delete-char",3))
        self.assertNotIn(" "*92+"v",probe.synthetic_lines("insert-char",3))
        self.assertNotIn(" "*88+"v",probe.synthetic_lines("delete-char",3))

    def test_unknown_text_repeated_markers_and_early_QR_are_rejected(self):
        for case in probe.CASES:
            for phase in range(len(probe.phases(case))):
                for lines in (["SECRET"],[" "*94+"v"],["vv"],["0123456789","0123456789"],["ANCHOR","ANCHOR"]):
                    with self.subTest(case=case,phase=phase,lines=lines),self.assertRaises(ValueError):
                        probe.validate_document(case,phase,{"line_count":len(lines),"lines":lines})
        with self.assertRaises(ValueError):probe.validate_document("right-edge-wrap",3,{"line_count":2,"lines":["ANCHOR","R"]})
        probe.validate_document("right-edge-wrap",4,{"line_count":2,"lines":["ANCHOR","R"]})
        with self.assertRaises(ValueError):probe.validate_document("right-edge-wrap",4,{"line_count":3,"lines":["ANCHOR","R","R"]})

    def test_mechanics_requires_all_pre_edit_observations_exact_action_and_precondition(self):
        for case in probe.CASES:
            with self.subTest(case=case),tempfile.TemporaryDirectory() as temp:
                directory=Path(temp);state,result=recording(case)
                values={"peer.json":state,probe.RESULT_FILE:result,"cleanup.json":{"active":0,"total":3,"terminated":0},
                        "profile-check.json":{"only_synthetic_session":True,"session_count":1}}
                for name,value in values.items():(directory/name).write_text(json.dumps(value))
                (directory/"stderr.txt").write_bytes(b"")
                for index in range(len(probe.phases(case))):(directory/f"ack-{index}").write_text("observed")
                self.assertEqual(probe.audit_mechanics(directory,case)["pre_action_stage_count"],3)
                state["records"][3]["source_hex"]="51" if case!="right-edge-wrap" else "52"
                (directory/"peer.json").write_text(json.dumps(state))
                with self.assertRaisesRegex(ValueError,"Exact action source"):probe.audit_mechanics(directory,case)

    def test_case_namespaces_preserve_frozen_source_and_other_cases(self):
        baseline=probe.baseline.phase_records()
        one,two=probe.peer_class("right-edge-wrap"),probe.peer_class("erase-right")
        self.assertEqual(one._serve.__globals__["phases"](20,91)[3][1],b"Q")
        self.assertEqual(two._serve.__globals__["phases"](20,91)[3][1],b"\x1b[0K")
        self.assertEqual(probe.baseline.phase_records(),baseline)
        for case in probe.CASES:compile(probe.launcher_source(case),"<offline launcher>","exec")


class EditPlanTests(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory(prefix="resize-edit-tests-");self.addCleanup(temporary.cleanup)
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
        return probe.prepare_plan(self.source,self.out,self.root/"rea.cmd",self.plan,"right-edge-wrap",authorized)

    def test_archive_plan_is_offline_and_exactly_ten_source_files(self):
        with patch.object(probe.common.subprocess,"check_output",side_effect=AssertionError("No archive Git")):
            plan=self.prepare()
        self.assertEqual(len(plan["source_files_sha256"]),10)
        self.assertEqual(plan["actual_original_runs"],0);self.assertFalse(self.out.exists())
        with self.assertRaisesRegex(ValueError,"not authorized"):probe.run_plan(self.plan)
        self.assertFalse(self.plan.with_suffix(".attempted").exists())

    def test_changed_case_geometry_or_source_is_rejected_before_attempt(self):
        plan=self.prepare(True)
        for key,value in (("case_id","delete-char"),("phase_count",3),("required_grids",[]),("phases",[]),
                          ("source_identity",{"kind":"git-checkout"}),("launcher_source_sha256","0"*64)):
            self.plan.write_text(json.dumps({**plan,key:value}))
            with self.subTest(key=key),self.assertRaises(ValueError):probe.run_plan(self.plan)
            self.assertFalse(self.plan.with_suffix(".attempted").exists())

    def test_local_boundary_existing_plan_and_existing_attempt_remain_enforced(self):
        with self.assertRaisesRegex(ValueError,"local directory"):
            probe.prepare_plan(self.source,self.root/"elsewhere",self.root/"rea.cmd",self.plan,"right-edge-wrap")
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
