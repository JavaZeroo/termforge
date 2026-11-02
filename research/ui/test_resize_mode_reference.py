"""Fixed frame/pending sources, actual protected peer and plan guards; no sockets/target."""
import hashlib
import json
from pathlib import Path
import shutil
import socket
import tempfile
import unittest
from unittest.mock import patch

from research.ui import resize_mode_reference as probe


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
            grid = list(probe.required_grids(self.case)[armed])
            if self.mode == "wrong-shrink" and armed == probe.resize_phases(self.case)[0]: grid[0] = 62
            if self.mode == "height-change": grid[1] = 21
            if self.mode == "wrong-restore" and armed == probe.resize_phases(self.case)[1]: grid[0] = 90
            events = [[62,20],[60,20],grid] if self.mode == "batched-resize" and armed == probe.resize_phases(self.case)[0] else [grid]
            count = len(state["naws_history"])+(0 if self.mode == "stale-ready" else len(events))
            (directory/f"resize-ready-{armed}").write_text(json.dumps({"phase":armed,"grid":grid,"event_count":count}))
            return b"".join(naws(*event) for event in events)
        record = state["records"][-1]
        if self.mode == "unarmed-action" and record["phase"] == 2:
            return naws(62,20)+naws(61,20)
        if record["cursor_reply"] is not None:
            if self.mode == "no-ack-"+str(record["phase"]):
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
        self.fragment = b"8;501R" if self.mode == "dsr-overflow" else b"0;91R" if self.mode == "dsr-zero" else b"8;91R"
        return b"\x1b["


def receive(case="active-primary", mode="normal"):
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



def recording(case="active-primary"):
    state, _ = receive(case, "batched-resize")
    result={"status":"observed-fixed-resize-mode-states","case_id":case,"error":None,"observations":[],
            "window_rect_format":"qt-qrect-inclusive-ltrb","geometry_checks":[],"full_active_grid_verified":False,
            "local_shell_transport_verified":False,"presented_done":None}
    for record,window in zip(state["records"],probe.windows(case)):
        phase=record["phase"];lines=[""]*20
        if case.startswith("active-"):
            for row,word in ((1,"T0"),(2,"R2"),(3,"R3"),(4,"R4"),(5,"R5"),(6,"B6"),(20,"B0")):
                lines[row-1]=" "*5+word
            lines[2] += "  "+("MP" if case=="active-primary" else "AP")
            if phase>=2:lines[1]="UP"+lines[1][2:]
            if phase>=3:lines[4]="DN"+lines[4][2:]
        else:
            lines[1]="0123456789";lines[2]="ANCHOR";lines[7]=" "*90+"W"
            if phase in (1,2):lines[12]=" "*9+"AP"
            if phase>=4:lines[7]=" "*60+"X"
        rect=[113,-27,112+window[0],-28+window[1]]
        terminal={"connected":True,"protocol":"Telnet","capacity":20,"line_count":len(lines),"lines":lines,"markers":[],
                  "top":None,"scroll":0,"caret_api":{"caretLine":7,"caretColumn":90,"caretPosition":200}}
        result["observations"].append({**{k:record[k] for k in ("phase","label","frozen_grid","cursor_reply","cursor_reply_private","response_hex")},
                                      "terminal":terminal,"window_rect":rect,"window_size":list(window)})
        result["geometry_checks"].append({"phase":phase,"window_rect":rect,"window_size":list(window),"expected_size":list(window)})
    return state,result


class ModeReferenceTests(unittest.TestCase):
    def test_three_literal_sources_have_16_stages_and_no_LF(self):
        reset=b"\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"
        region=reset+b"\x1b[1;6HT0\x1b[2;6HR2\x1b[3;6HR3\x1b[4;6HR4\x1b[5;6HR5\x1b[6;6HB6\x1b[20;6HB0"
        expected={
            "active-primary":[region+b"\x1b[2;5r\x1b[?6h\x1b[2;10HMP",b"",b"\x1b[1;1HUP",b"\x1b[99;1HDN",b""],
            "active-alternate":[b"\x1b[?1049h"+region+b"\x1b[2;5r\x1b[?6h\x1b[2;10HAP",b"",b"\x1b[1;1HUP",b"\x1b[99;1HDN",b""],
            "saved-primary-edge":[reset+b"\x1b[2;1H0123456789\x1b[3;1HANCHOR\x1b[8;91HW",b"\x1b[?1049h\x1b[2;10HAP",b"",b"\x1b[?1049l",b"X",b""],
        }
        self.assertEqual(set(probe.CASES),set(expected))
        for case,sources in expected.items():
            self.assertEqual([source for _,source in probe.phases(case)],sources)
            self.assertTrue(all(b"\n" not in source and b"\r" not in source for source in sources))
        self.assertEqual(sum(len(probe.phases(c)) for c in probe.CASES),16)

    def test_case_specific_boundaries_exact_fragmented_replies_and_NAWS(self):
        expected={"active-primary":([91,61,61,61,91],[1,4],[1,4,4,4,5]),
                  "active-alternate":([91,61,61,61,91],[1,4],[1,4,4,4,5]),
                  "saved-primary-edge":([91,91,61,61,61,91],[2,5],[1,1,4,4,4,5])}
        for case,(widths,boundaries,freezes) in expected.items():
            with self.subTest(case=case):
                state,sent=receive(case,"batched-resize")
                self.assertTrue(state["complete"]);self.assertNotIn("error",state)
                self.assertEqual(state["frozen_grids"],[[w,20] for w in widths])
                self.assertEqual([b["phase"] for b in state["resize_boundaries"]],boundaries)
                self.assertEqual(state["freeze_event_counts"],freezes)
                self.assertEqual(sent[1:],[source+b"\x1b[6n" for _,source in probe.phases(case)])
                self.assertEqual(state["received_hex"],(b"\x1b[8;91R"*len(widths)).hex())

    def test_shrink_upper_lower_and_return_observations_must_each_be_acknowledged(self):
        for case,index in (("active-primary",1),("active-primary",2),("active-primary",3),
                           ("active-alternate",1),("active-alternate",2),("saved-primary-edge",2),("saved-primary-edge",3)):
            with self.subTest(case=case,index=index):
                state,sent=receive(case,"no-ack-"+str(index))
                self.assertEqual(len(state["records"]),index+1)
                self.assertEqual(sent[1:],[source+b"\x1b[6n" for _,source in probe.phases(case)[:index+1]])
                self.assertFalse(state.get("complete",False))

    def test_dsr_is_bounded_independently_of_protocol_grid(self):
        for raw,wanted in ((b"\x1b[8;91R",[8,91]),(b"\x1b[?200;500R",[200,500])):
            self.assertEqual(probe.reply_observation(raw)["cursor_reply"],wanted)
        self.assertTrue(probe.reply_observation(b"\x1b[?8;91R")["cursor_reply_private"])
        self.assertIsNone(probe.reply_observation(b"\x1b[8;9"))
        for raw in (b"\x1b[0;91R",b"\x1b[201;1R",b"\x1b[8;501R",b"\x1b[1;0R",b"\x1b[8;91R\x1b[8;91R"):
            with self.subTest(raw=raw),self.assertRaises(ValueError):probe.reply_observation(raw)

    def test_wrong_geometry_stale_events_extra_bytes_and_invalid_replies_stop(self):
        for mode in ("stale-initial","stale-ready","wrong-shrink","wrong-restore","height-change","wrong-request",
                     "unarmed-action","duplicate-reply","foreign-input","dsr-overflow","dsr-zero"):
            with self.subTest(mode=mode):
                state,_=receive(mode=mode)
                self.assertIn("error",state);self.assertFalse(state.get("complete",False));self.assertTrue(state["closed"])

    def test_finite_body_allows_source_variants_without_predicting_region_or_saved_state(self):
        for case in ("active-primary","active-alternate"):
            middle="MP" if case=="active-primary" else "AP"
            for text in ("UP","UP   T0","UP   R3  "+middle,"DN   B0", "DN".ljust(61)):
                self.assertIn(text,probe.synthetic_lines(case,3))
            self.assertNotIn("DN",probe.synthetic_lines(case,2))
            self.assertNotIn("UP",probe.synthetic_lines(case,1))
        for text in ("X"," "*60+"X"," "*90+"WX"," "*29+"WX"," "*9+"AP"):
            self.assertIn(text,probe.synthetic_lines("saved-primary-edge",4))
        for case in probe.CASES:
            for phase in range(len(probe.phases(case))):
                probe.validate_document(case,phase,{"line_count":1,"lines":[""]})
                self.assertLessEqual(max(map(len,probe.synthetic_lines(case,phase))),92)

    def test_private_text_duplicates_future_markers_and_unbounded_padding_fail(self):
        for case in probe.CASES:
            for phase in range(len(probe.phases(case))):
                for lines in (["PRIVATE"],[" "*93+"W"],["UP","UP"],[" "*5+"R2"," "*5+"R2"]):
                    with self.subTest(case=case,phase=phase,lines=lines),self.assertRaises(ValueError):
                        probe.validate_document(case,phase,{"line_count":len(lines),"lines":lines})
        for case,phase,line in (("active-primary",1,"UP"),("active-alternate",2,"DN"),("saved-primary-edge",3,"X"),
                                ("saved-primary-edge",0," "*9+"AP"),("active-primary",3," "*9+"AP")):
            with self.assertRaises(ValueError):probe.validate_document(case,phase,{"line_count":1,"lines":[line]})

    def test_complete_mechanics_accepts_outside_grid_DSR_and_rejects_missing_sampling(self):
        for case in probe.CASES:
            with self.subTest(case=case),tempfile.TemporaryDirectory() as temp:
                directory=Path(temp);state,result=recording(case)
                values={"peer.json":state,probe.RESULT_FILE:result,"cleanup.json":{"active":0,"total":3,"terminated":0},
                        "profile-check.json":{"only_synthetic_session":True,"session_count":1}}
                for name,value in values.items():(directory/name).write_text(json.dumps(value))
                (directory/"stderr.txt").write_bytes(b"")
                for index in range(len(probe.phases(case))):(directory/f"ack-{index}").write_text("observed")
                actual=probe.audit_mechanics(directory,case)
                self.assertTrue(actual["stateful_observations_preserved"])
                self.assertEqual(actual["stage_count"],len(probe.phases(case)))
                result["observations"].pop(1);(directory/probe.RESULT_FILE).write_text(json.dumps(result))
                with self.assertRaisesRegex(ValueError,"phases incomplete"):probe.audit_mechanics(directory,case)

    def test_case_bindings_are_isolated_and_frozen_dependencies_unchanged(self):
        original=probe.baseline.phase_records()
        one,two=probe.peer_class("active-primary"),probe.peer_class("saved-primary-edge")
        self.assertEqual(len(one._serve.__globals__["phases"](20,91)),5)
        self.assertEqual(len(two._serve.__globals__["phases"](20,91)),6)
        self.assertEqual(probe.baseline.phase_records(),original)
        for case in probe.CASES:
            compile(probe.launcher_source(case),"<offline launcher>","exec")
            self.assertIn(json.dumps(probe.controller_contract(case)),probe.probe_fragment(case))


class ModePlanTests(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory(prefix="resize-mode-tests-");self.addCleanup(temporary.cleanup)
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
        return probe.prepare_plan(self.source,self.out,self.root/"rea.cmd",self.plan,"active-primary",authorized)

    def test_archive_plan_is_offline_and_exactly_ten_source_files(self):
        with patch.object(probe.common.subprocess,"check_output",side_effect=AssertionError("No archive Git")):
            plan=self.prepare()
        self.assertEqual(len(plan["source_files_sha256"]),10)
        self.assertEqual(plan["actual_original_runs"],0);self.assertFalse(self.out.exists())
        with self.assertRaisesRegex(ValueError,"not authorized"):probe.run_plan(self.plan)
        self.assertFalse(self.plan.with_suffix(".attempted").exists())

    def test_changed_case_geometry_or_source_is_rejected_before_attempt(self):
        plan=self.prepare(True)
        for key,value in (("case_id","active-alternate"),("phase_count",3),("required_grids",[]),("phases",[]),
                          ("source_identity",{"kind":"git-checkout"}),("launcher_source_sha256","0"*64)):
            self.plan.write_text(json.dumps({**plan,key:value}))
            with self.subTest(key=key),self.assertRaises(ValueError):probe.run_plan(self.plan)
            self.assertFalse(self.plan.with_suffix(".attempted").exists())

    def test_local_boundary_existing_plan_and_existing_attempt_remain_enforced(self):
        with self.assertRaisesRegex(ValueError,"local directory"):
            probe.prepare_plan(self.source,self.root/"elsewhere",self.root/"rea.cmd",self.plan,"active-primary")
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
