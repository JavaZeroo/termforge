"""Fixed frame/pending sources, actual protected peer and plan guards; no sockets/target."""
import hashlib
import json
from pathlib import Path
import shutil
import socket
import tempfile
import unittest
from unittest.mock import patch

from research.ui import resize_frame_reference as probe


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


def receive(case="pending-on-shrink", mode="normal"):
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



def recording(case="pending-on-shrink"):
    state, _ = receive(case, "batched-resize")
    result={"status":"observed-fixed-resize-frame-states","case_id":case,"error":None,"observations":[],
            "window_rect_format":"qt-qrect-inclusive-ltrb","geometry_checks":[],"full_active_grid_verified":False,
            "local_shell_transport_verified":False,"presented_done":None}
    for record,window in zip(state["records"],probe.windows(case)):
        phase=record["phase"];lines=[""]*20;lines[1]="0123456789";lines[2]="ANCHOR"
        if case in ("alternate-active","hidden-primary"):
            lines[7]=" "*90+"v";lines[9]=" "*90+"wx"
        if case=="hidden-primary":
            lines[2]="ANCHOR   MP"
            if phase in (1,2):lines=[""]*20;lines[1]=" "*9+"AP"
            if phase>=4:lines[1]="PS23456789";lines[4]="BS"
            if phase>=5:lines[1]="PI23456789";lines[4]="BI"
            if phase>=6:lines[0]="PO";lines[19]="BO"
        elif case.endswith("-shrink"):
            lines[7]=" "*90+"W"
            if phase>=2:lines[7]=" "*90+("WX" if case.startswith("pending-") else "X")
        elif case.endswith("-grow"):
            if phase>=2:lines[7]=" "*60+"W"
            if phase>=4:lines[7]=" "*60+("WX" if case.startswith("pending-") else "X")
        rect=[113,-27,112+window[0],-28+window[1]]
        terminal={"connected":True,"protocol":"Telnet","capacity":20,"line_count":len(lines),"lines":lines,"markers":[],
                  "top":None,"scroll":0,"caret_api":{"caretLine":7,"caretColumn":90,"caretPosition":200}}
        result["observations"].append({**{k:record[k] for k in ("phase","label","frozen_grid","cursor_reply","cursor_reply_private","response_hex")},
                                      "terminal":terminal,"window_rect":rect,"window_size":list(window)})
        result["geometry_checks"].append({"phase":phase,"window_rect":rect,"window_size":list(window),"expected_size":list(window)})
    return state,result


class FrameReferenceTests(unittest.TestCase):
    def test_six_literal_protocols_have_29_stages_and_initial_group_has_19(self):
        reset=b"\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"
        prefix=reset+b"\x1b[2;1H0123456789\x1b[3;1HANCHOR"
        body=prefix+b"\x1b[?7l\x1b[8;91Huv\x1b[?7h\x1b[10;91Hwx"
        expected={
            "alternate-active":[b"\x1b[?1049h"+body,b"",b""],
            "hidden-primary":[body+b"\x1b[2;5r\x1b[?6h\x1b[2;10HMP",b"\x1b[?1049h\x1b[3;7r\x1b[?6l\x1b[2;10HAP",b"",b"\x1b[?1049l",
                              b"\x1b[1;1HPS\x1b[99;1HBS",b"\x1b[?6h\x1b[1;1HPI\x1b[99;1HBI",b"\x1b[?6l\x1b[1;1HPO\x1b[99;1HBO",b""],
            "pending-on-shrink":[prefix+b"\x1b[8;91HW",b"",b"X",b""],
            "wrap-off-shrink":[prefix+b"\x1b[?7l\x1b[8;91HW",b"",b"X",b""],
            "pending-on-grow":[prefix,b"",b"\x1b[8;61HW",b"",b"X"],
            "wrap-off-grow":[prefix+b"\x1b[?7l",b"",b"\x1b[8;61HW",b"",b"X"],
        }
        for case,sources in expected.items():
            self.assertEqual([source for _,source in probe.phases(case)],sources)
        self.assertEqual(len(body),78)
        self.assertEqual(sum(len(probe.phases(c)) for c in probe.CASES),29)
        self.assertEqual(sum(len(probe.phases(c)) for c in probe.INITIAL_CASES),19)

    def test_variable_resize_indices_and_exact_fragmented_replies_at_every_stateful_stage(self):
        expected={"alternate-active":([91,61,91],[1,2],[1,4,5]),
                  "hidden-primary":([91,91,61,61,61,61,61,91],[2,7],[1,1,4,4,4,4,4,5]),
                  "pending-on-shrink":([91,61,61,91],[1,3],[1,4,4,5]),
                  "wrap-off-shrink":([91,61,61,91],[1,3],[1,4,4,5]),
                  "pending-on-grow":([91,61,61,91,91],[1,3],[1,4,4,5,5]),
                  "wrap-off-grow":([91,61,61,91,91],[1,3],[1,4,4,5,5])}
        for case,(widths,boundaries,freezes) in expected.items():
            with self.subTest(case=case):
                state,sent=receive(case,"batched-resize")
                self.assertTrue(state["complete"]);self.assertNotIn("error",state)
                self.assertEqual(state["frozen_grids"],[[w,20] for w in widths])
                self.assertEqual([b["phase"] for b in state["resize_boundaries"]],boundaries)
                self.assertEqual(state["freeze_event_counts"],freezes)
                self.assertEqual(sent[1:],[source+b"\x1b[6n" for _,source in probe.phases(case)])
                self.assertEqual(state["received_hex"],(b"\x1b[8;91R"*len(widths)).hex())
                self.assertEqual([r["cursor_reply"] for r in state["records"]],[[8,91]]*len(widths))

    def test_unmodified_return_shrink_baseline_and_grown_baseline_must_be_acknowledged(self):
        for case,index in (("hidden-primary",3),("pending-on-shrink",1),("wrap-off-shrink",1),("pending-on-grow",3),("wrap-off-grow",3)):
            with self.subTest(case=case):
                state,sent=receive(case,"no-ack-"+str(index))
                self.assertEqual(len(state["records"]),index+1)
                self.assertEqual(sent[1:],[source+b"\x1b[6n" for _,source in probe.phases(case)[:index+1]])
                self.assertFalse(state.get("complete",False))

    def test_dsr_bounds_are_independent_of_current_grid_and_preserve_private_prefix(self):
        for raw,wanted in ((b"\x1b[8;91R",[8,91]),(b"\x1b[?200;500R",[200,500])):
            self.assertEqual(probe.reply_observation(raw)["cursor_reply"],wanted)
        self.assertTrue(probe.reply_observation(b"\x1b[?8;91R")["cursor_reply_private"])
        self.assertIsNone(probe.reply_observation(b"\x1b[8;9"))
        for raw in (b"\x1b[0;91R",b"\x1b[201;1R",b"\x1b[8;501R",b"\x1b[1;0R",b"\x1b[8;91R\x1b[8;91R"):
            with self.subTest(raw=raw),self.assertRaises(ValueError):probe.reply_observation(raw)

    def test_identity_geometry_stale_naws_extra_bytes_and_out_of_bound_dsr_stop(self):
        for mode in ("stale-initial","stale-ready","wrong-shrink","wrong-restore","height-change","wrong-request",
                     "unarmed-action","duplicate-reply","foreign-input","dsr-overflow","dsr-zero"):
            with self.subTest(mode=mode):
                state,_=receive(mode=mode)
                self.assertIn("error",state);self.assertFalse(state.get("complete",False));self.assertTrue(state["closed"])

    def test_finite_source_words_preserve_distinct_controls_and_never_predict_frame_restore(self):
        for phase,words in ((4,("PS","BS")),(5,("PI","BI")),(6,("PO","BO"))):
            allowed=probe.synthetic_lines("hidden-primary",phase)
            for word in words:
                self.assertIn(word,allowed);self.assertIn(word+"23456789",allowed)
                self.assertIn(word+"CHOR   MP",allowed);self.assertIn(word.ljust(61),allowed)
            self.assertIn(" "*9+"AP",allowed)
        self.assertNotIn("PO",probe.synthetic_lines("hidden-primary",5))
        self.assertNotIn("PS",probe.synthetic_lines("hidden-primary",3))
        for case in probe.CASES:
            for phase in range(len(probe.phases(case))):
                probe.validate_document(case,phase,{"line_count":1,"lines":[""]})
        self.assertIn("X"+" "*60,probe.synthetic_lines("pending-on-shrink",2))
        self.assertIn(" "*90+"X",probe.synthetic_lines("wrap-off-shrink",2))
        self.assertIn(" "*60+"WX",probe.synthetic_lines("pending-on-grow",4))

    def test_private_text_repeat_markers_and_premature_WX_or_controls_are_rejected(self):
        for case in probe.CASES:
            for phase in range(len(probe.phases(case))):
                for lines in (["PRIVATE"],[" "*93+"W"],["ANCHOR","ANCHOR"],["0123456789","0123456789"]):
                    with self.subTest(case=case,phase=phase),self.assertRaises(ValueError):
                        probe.validate_document(case,phase,{"line_count":len(lines),"lines":lines})
        for case,phase,line in (("hidden-primary",3,"PS"),("pending-on-grow",1," "*60+"W"),("wrap-off-shrink",1,"X")):
            with self.assertRaises(ValueError):probe.validate_document(case,phase,{"line_count":1,"lines":[line]})

    def test_complete_mechanics_accepts_dsr91_on_grid61_and_rejects_missing_sampling(self):
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

    def test_each_case_binds_its_own_stage_contract_without_changing_frozen_dependencies(self):
        original=probe.baseline.phase_records()
        one,two=probe.peer_class("hidden-primary"),probe.peer_class("pending-on-grow")
        self.assertEqual(len(one._serve.__globals__["phases"](20,91)),8)
        self.assertEqual(len(two._serve.__globals__["phases"](20,91)),5)
        self.assertEqual(probe.baseline.phase_records(),original)
        for case in probe.CASES:
            compile(probe.launcher_source(case),"<offline launcher>","exec")
            self.assertIn(json.dumps(probe.controller_contract(case)),probe.probe_fragment(case))


class FramePlanTests(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory(prefix="resize-frame-tests-");self.addCleanup(temporary.cleanup)
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
        return probe.prepare_plan(self.source,self.out,self.root/"rea.cmd",self.plan,"pending-on-shrink",authorized)

    def test_archive_plan_is_offline_and_exactly_ten_source_files(self):
        with patch.object(probe.common.subprocess,"check_output",side_effect=AssertionError("No archive Git")):
            plan=self.prepare()
        self.assertEqual(len(plan["source_files_sha256"]),10)
        self.assertEqual(plan["actual_original_runs"],0);self.assertFalse(self.out.exists())
        with self.assertRaisesRegex(ValueError,"not authorized"):probe.run_plan(self.plan)
        self.assertFalse(self.plan.with_suffix(".attempted").exists())

    def test_changed_case_geometry_or_source_is_rejected_before_attempt(self):
        plan=self.prepare(True)
        for key,value in (("case_id","wrap-off-shrink"),("phase_count",3),("required_grids",[]),("phases",[]),
                          ("source_identity",{"kind":"git-checkout"}),("launcher_source_sha256","0"*64)):
            self.plan.write_text(json.dumps({**plan,key:value}))
            with self.subTest(key=key),self.assertRaises(ValueError):probe.run_plan(self.plan)
            self.assertFalse(self.plan.with_suffix(".attempted").exists())

    def test_local_boundary_existing_plan_and_existing_attempt_remain_enforced(self):
        with self.assertRaisesRegex(ValueError,"local directory"):
            probe.prepare_plan(self.source,self.root/"elsewhere",self.root/"rea.cmd",self.plan,"pending-on-shrink")
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
