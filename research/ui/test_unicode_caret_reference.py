"""Fixed UTF-8 sources, actual protected peer and plan guards; no sockets/target."""
import hashlib
import json
from pathlib import Path
import shutil
import socket
import tempfile
import unittest
from unittest.mock import patch

from research.ui import unicode_caret_reference as probe


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
            if self.mode == "wrong-restore" and armed == probe.resize_phases(self.case)[0]: grid[0] = 90
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
        self.fragment = b"8;501R" if self.mode == "dsr-overflow" else b"0;91R" if self.mode == "dsr-zero" else b"8;63R" if record["phase"] == 0 else b"8;61R"
        return b"\x1b["


def receive(case="dsr-only-q", mode="normal"):
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



def recording(case="dsr-only-q"):
    state, _ = receive(case, "batched-resize")
    result={"status":"observed-fixed-unicode-caret-states","case_id":case,"error":None,"observations":[],
            "window_rect_format":"qt-qrect-inclusive-ltrb","geometry_checks":[],"full_active_grid_verified":False,
            "local_shell_transport_verified":False,"presented_done":None}
    for record,window in zip(state["records"],probe.windows(case)):
        phase=record["phase"];lines=probe.INITIAL_LINES.copy()
        if phase>=2:lines[7]=" "*60+"\u0302e\u0301"
        if phase>=3:lines[7]=" "*60+"\u0302Q"
        rect=[113,-27,112+window[0],-28+window[1]]
        terminal={"connected":True,"protocol":"Telnet","capacity":20,"line_count":len(lines),"lines":lines,"markers":[],
                  "top":None,"scroll":0,"caret_api":{"caretLine":7,"caretColumn":90,"caretPosition":200}}
        result["observations"].append({**{k:record[k] for k in ("phase","label","frozen_grid","cursor_reply","cursor_reply_private","response_hex")},
                                      "terminal":terminal,"window_rect":rect,"window_size":list(window)})
        result["geometry_checks"].append({"phase":phase,"window_rect":rect,"window_size":list(window),"expected_size":list(window)})
    return state,result


class CaretReferenceTests(unittest.TestCase):
    def test_three_exact_sources_and_twelve_stages(self):
        initial=bytes.fromhex('1b5b3f366c1b5b3f37681b5b721b5b306d1b5b324a1b5b323b3148303132333435363738391b5b333b3148414e43484f521b5b383b36314865cc815a')
        self.assertEqual(len(initial),60)
        self.assertEqual(hashlib.sha256(initial).hexdigest(),'c6d08514fb8897ee31059833506de1354f04b69b9721aa1153f34f750e444d49')
        for case in probe.CASES:
            middle=b'\x1b[8;61H' if case=='cup-position-q' else b''
            self.assertEqual([x for _,x in probe.phases(case)],[initial,middle,b'\xcc\x82',b'Q'])
        self.assertEqual(sum(len(probe.phases(c)) for c in probe.CASES),12)

    def test_zero_or_one_resize_and_fragmented_replies(self):
        for case in probe.CASES:
            state,sent=receive(case,'batched-resize')
            self.assertTrue(state['complete']);self.assertNotIn('error',state)
            widths=[91,61,61,61] if case=='resize-mark-q' else [91]*4
            self.assertEqual(state['frozen_grids'],[[w,20] for w in widths])
            self.assertEqual([b['phase'] for b in state['resize_boundaries']],[1] if case=='resize-mark-q' else [])
            self.assertEqual(state['freeze_event_counts'],[1,4,4,4] if case=='resize-mark-q' else [1]*4)
            self.assertEqual(sent[1:],[source+b'\x1b[6n' for _,source in probe.phases(case)])
            self.assertEqual(state['received_hex'],(b'\x1b[8;63R'+b'\x1b[8;61R'*3).hex())

    def test_every_pre_action_observation_is_required(self):
        for case in probe.CASES:
            for index in (0,1,2):
                state,sent=receive(case,'no-ack-'+str(index))
                self.assertEqual(len(state['records']),index+1)
                self.assertEqual(sent[1:],[source+b'\x1b[6n' for _,source in probe.phases(case)[:index+1]])
                self.assertFalse(state.get('complete',False))

    def test_DSR_independent_bounds_and_split_reply(self):
        self.assertEqual(probe.reply_observation(b'\x1b[?200;500R')['cursor_reply'],[200,500])
        self.assertIsNone(probe.reply_observation(b'\x1b[8;6'))
        for raw in (b'\x1b[0;1R',b'\x1b[201;1R',b'\x1b[1;501R',b'\x1b[8;61R\x1b[8;61R'):
            with self.assertRaises(ValueError):probe.reply_observation(raw)

    def test_wrong_geometry_events_extra_bytes_and_invalid_replies_stop(self):
        for mode in ('stale-initial','stale-ready','wrong-shrink','wrong-restore','height-change','wrong-request','unarmed-action','duplicate-reply','foreign-input','dsr-overflow','dsr-zero'):
            state,_=receive('resize-mark-q',mode)
            self.assertIn('error',state);self.assertFalse(state.get('complete',False));self.assertTrue(state['closed'])
        for case in ('dsr-only-q','cup-position-q'):
            state,_=receive(case,'unarmed-action');self.assertIn('error',state)

    def test_finite_Q_retention_overwrite_and_insertion_candidates(self):
        for case in probe.CASES:
            for text in (' '*60+'\u0302Q',' '*60+'Q','\u0302Qe\u0301','e\u0301Z\u0302Q','QếZ'):
                self.assertIn(text,probe.synthetic_lines(case,3))
            for phase in range(4):
                self.assertLess(len(probe.synthetic_lines(case,phase)),6000)
                self.assertLessEqual(max(map(len,probe.synthetic_lines(case,phase))),91)
                probe.validate_document(case,phase,{'line_count':1,'lines':['']})

    def test_private_unicode_future_actions_duplicates_and_padding_rejected(self):
        for case in probe.CASES:
            for phase in range(4):
                for lines in (['PRIVATE'],['Z','Z'],['é','e'],['\ufffd'],['😀'],[' '*92]):
                    with self.assertRaises(ValueError):probe.validate_document(case,phase,{'line_count':len(lines),'lines':lines})
            for phase,word in ((0,'\u0302'),(1,'\u0302'),(2,'Q')):
                with self.assertRaises(ValueError):probe.validate_document(case,phase,{'line_count':1,'lines':[word]})

    def test_mechanics_preserves_initial_gate_and_each_before_after_phase(self):
        for case in probe.CASES:
            with tempfile.TemporaryDirectory() as temporary:
                directory=Path(temporary);state,result=recording(case)
                values={'peer.json':state,probe.RESULT_FILE:result,'cleanup.json':{'active':0,'total':3,'terminated':0},'profile-check.json':{'only_synthetic_session':True,'session_count':1}}
                for name,value in values.items():(directory/name).write_text(json.dumps(value))
                (directory/'stderr.txt').write_bytes(b'')
                for phase in range(4):(directory/f'ack-{phase}').write_text('observed')
                self.assertEqual(probe.audit_mechanics(directory,case)['stage_count'],4)
                result['observations'][0]['terminal']['lines'][7]=' '*60+'éZ'
                (directory/probe.RESULT_FILE).write_text(json.dumps(result))
                with self.assertRaisesRegex(ValueError,'initial UTF-8 body/DSR'):probe.audit_mechanics(directory,case)

    def test_fresh_case_namespace_and_exact_controller_identity(self):
        original=probe.baseline.phase_records()
        for case in probe.CASES:
            cls=probe.peer_class(case)
            self.assertEqual(cls._serve.__globals__['phases'](20,91),probe.phases(case))
            compile(probe.launcher_source(case),'<offline launcher>','exec')
            self.assertIn(json.dumps(probe.controller_contract(case)),probe.probe_fragment(case))
        self.assertEqual(probe.baseline.phase_records(),original)


class CaretPlanTests(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory(prefix="unicode-caret-tests-");self.addCleanup(temporary.cleanup)
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
        return probe.prepare_plan(self.source,self.out,self.root/"rea.cmd",self.plan,"dsr-only-q",authorized)

    def test_archive_plan_is_offline_and_exactly_ten_source_files(self):
        with patch.object(probe.common.subprocess,"check_output",side_effect=AssertionError("No archive Git")):
            plan=self.prepare()
        self.assertEqual(len(plan["source_files_sha256"]),10)
        self.assertEqual(plan["actual_original_runs"],0);self.assertFalse(self.out.exists())
        with self.assertRaisesRegex(ValueError,"not authorized"):probe.run_plan(self.plan)
        self.assertFalse(self.plan.with_suffix(".attempted").exists())

    def test_changed_case_geometry_or_source_is_rejected_before_attempt(self):
        plan=self.prepare(True)
        for key,value in (("case_id","resize-mark-q"),("phase_count",3),("required_grids",[]),("phases",[]),
                          ("source_identity",{"kind":"git-checkout"}),("launcher_source_sha256","0"*64)):
            self.plan.write_text(json.dumps({**plan,key:value}))
            with self.subTest(key=key),self.assertRaises(ValueError):probe.run_plan(self.plan)
            self.assertFalse(self.plan.with_suffix(".attempted").exists())

    def test_local_boundary_existing_plan_and_existing_attempt_remain_enforced(self):
        with self.assertRaisesRegex(ValueError,"local directory"):
            probe.prepare_plan(self.source,self.root/"elsewhere",self.root/"rea.cmd",self.plan,"dsr-only-q")
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
