"""Fixed addressing sources, actual protected peer and plan guards; no sockets/target."""
import hashlib
import json
from pathlib import Path
import shutil
import socket
import tempfile
import unittest
from unittest.mock import patch

from research.ui import resize_address_reference as probe


def naws(columns, rows):
    return bytes([255,250,31])+columns.to_bytes(2,"big")+rows.to_bytes(2,"big")+bytes([255,240])


class Connection:
    def __init__(self, peer, mode):
        self.peer, self.mode = peer, mode
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
            if self.mode == "wrong-restore" and armed == 3: grid[0] = 90
            events = [[62,20],[60,20],grid] if self.mode == "batched-resize" and armed == 1 else [grid]
            count = len(state["naws_history"])+(0 if self.mode == "stale-ready" else len(events))
            (directory/f"resize-ready-{armed}").write_text(json.dumps({"phase":armed,"grid":grid,"event_count":count}))
            return b"".join(naws(*event) for event in events)
        record = state["records"][-1]
        if self.mode == "unarmed-action" and record["phase"] == 2:
            return naws(62,20)+naws(61,20)
        if record["cursor_reply"] is not None:
            if self.mode == "no-pre-action-ack" and record["phase"] == 1:
                raise RuntimeError("Synthetic withheld pre-action acknowledgment")
            (directory/f'ack-{record["phase"]}').write_text("observed")
            target = record["phase"]+1
            if target in (1,3):
                window = list(probe.WINDOWS[target])
                if self.mode == "wrong-request": window[0] += 1
                (directory/f"resize-request-{target}").write_text(json.dumps({"phase":target,"window":window}))
            raise socket.timeout()
        if self.mode == "duplicate-reply": return b"\x1b[11;2R\x1b[11;2R"
        if self.mode == "foreign-input": return b"SECRET"
        if self.fragment is not None:
            result, self.fragment = self.fragment, None
            return result
        self.fragment = b"11;2R"
        return b"\x1b["


def receive(case="next-char", mode="normal"):
    with tempfile.TemporaryDirectory() as temporary:
        peer_type = probe.peer_class(case); peer = peer_type.__new__(peer_type)
        peer.directory = Path(temporary).resolve()
        (peer.directory/"fixture-ready").write_text(json.dumps({"grid":[91,20],"event_count":1}))
        peer.state = {"connected":False,"columns":None,"rows":None,"phase":None,"records":[]}
        peer.stop = type("NeverStopped",(),{"is_set":lambda _:False})()
        connection = Connection(peer,mode)
        peer.listener = type("Listener",(),{"accept":lambda _:(connection,("127.0.0.1",12345))})()
        peer._serve()
        return peer.state, connection.sent


def recording(case="next-char"):
    state, _ = receive(case, "batched-resize")
    result = {"status":"observed-four-resize-address-states","case_id":case,"error":None,"observations":[],
              "window_rect_format":"qt-qrect-inclusive-ltrb","geometry_checks":[],"full_active_grid_verified":False,
              "local_shell_transport_verified":False,"presented_done":None}
    for record, window in zip(state["records"],probe.WINDOWS):
        rect = [113,-27,112+window[0],-28+window[1]]
        lines = ["","0123456789","ANCHOR",""," "*90+"v",""," "*90+"wx"]
        if record["phase"] >= 2 and case != "erase-line-start": lines.append(" Q")
        terminal = {"connected":True,"protocol":"Telnet","capacity":20,"line_count":len(lines),"lines":lines,"markers":[],
                    "top":None,"scroll":0,"caret_api":{"caretLine":10,"caretColumn":1,"caretPosition":200}}
        result["observations"].append({**{k:record[k] for k in ("phase","label","frozen_grid","cursor_reply","cursor_reply_private","response_hex")},
                                      "terminal":terminal,"window_rect":rect,"window_size":list(window)})
        result["geometry_checks"].append({"phase":record["phase"],"window_rect":rect,"window_size":list(window),"expected_size":list(window)})
    return state,result


class AddressReferenceTests(unittest.TestCase):
    def test_five_literal_actions_and_four_stages_preserve_pre_action_baseline(self):
        expected={"next-char":b"Q","carriage-return":b"\rQ","line-feed":b"\nQ","explicit-cup":b"\x1b[11;2HQ","erase-line-start":b"\x1b[1K"}
        for case,action in expected.items():
            with self.subTest(case=case):
                values=probe.phases(case)
                self.assertEqual(values[1][1],b"");self.assertEqual(values[2][1],action);self.assertEqual(values[3][1],b"")
                self.assertTrue(values[0][1].endswith(b"\x1b[10;91Hwx"))
                self.assertEqual(probe.fixed_contract(case)["pre_action_phases"],[0,1])
                self.assertEqual(probe.fixed_contract(case)["resize_phases"],[1,3])
        with self.assertRaises(ValueError):probe.phases("next-char",20,90)
        with self.assertRaises(ValueError):probe.phases("unknown")

    def test_actual_peer_emits_only_the_bound_action_after_ack_and_keeps_two_resize_boundaries(self):
        for case in probe.CASES:
            state,sent=receive(case,"batched-resize")
            self.assertTrue(state["complete"]);self.assertNotIn("error",state)
            self.assertEqual(sent[1:],[source+b"\x1b[6n" for _,source in probe.phases(case)])
            self.assertEqual(state["frozen_grids"],[[91,20],[61,20],[61,20],[91,20]])
            self.assertEqual(state["freeze_event_counts"],[1,4,4,5])
            self.assertEqual([v["phase"] for v in state["resize_boundaries"]],[1,3])
            self.assertEqual(state["received_hex"], (b"\x1b[11;2R"*4).hex())
            self.assertEqual([v["cursor_reply"] for v in state["records"]], [[11,2]]*4)

    def test_wrong_geometry_stale_snapshot_extra_input_and_unarmed_change_stop(self):
        for mode in ("stale-initial","stale-ready","wrong-shrink","wrong-restore","height-change","wrong-request",
                     "unarmed-action","duplicate-reply","foreign-input"):
            with self.subTest(mode=mode):
                state,_=receive(mode=mode)
                self.assertIn("error",state);self.assertFalse(state.get("complete",False));self.assertTrue(state["closed"])

    def test_missing_pre_action_ack_never_releases_action_or_restore(self):
        state,sent=receive(mode="no-pre-action-ack")
        self.assertEqual(len(state["records"]),2)
        self.assertEqual(sent[1:],[probe.phases("next-char")[0][1]+b"\x1b[6n",b"\x1b[6n"])
        self.assertNotIn(b"Q\x1b[6n",sent)

    def test_peer_and_launcher_namespaces_do_not_change_the_frozen_baseline(self):
        original=probe.baseline.phase_records()
        one,two=probe.peer_class("next-char"),probe.peer_class("line-feed")
        self.assertEqual(one._serve.__globals__["phases"](20,91)[2][1],b"Q")
        self.assertEqual(two._serve.__globals__["phases"](20,91)[2][1],b"\nQ")
        self.assertEqual(probe.baseline.phase_records(),original)
        for case in probe.CASES:
            source=probe.launcher_source(case);compile(source,"<offline launcher>","exec")
            self.assertIn("resize_address_reference",source);self.assertIn(repr(case),source)
            self.assertNotIn("__RESIZE_ADDRESS_CASE_JSON__",probe.probe_fragment(case))

    def test_finite_text_allowlist_rejects_private_text_duplicate_action_and_premature_action(self):
        for case in probe.CASES:
            for phase in range(4):
                for line in ("PRIVATE", " "*93+"Q", " "*90+"wxx", "QQ", "ANCHORSECRET"):
                    with self.subTest(case=case,phase=phase,line=line),self.assertRaises(ValueError):
                        probe.validate_document(case,phase,{"line_count":1,"lines":[line]})
        for phase in (0,1):
            with self.assertRaises(ValueError):probe.validate_document("next-char",phase,{"line_count":1,"lines":["Q"]})
        with self.assertRaises(ValueError):probe.validate_document("next-char",2,{"line_count":2,"lines":["Q"," Q"]})
        probe.validate_document("next-char",2,{"line_count":1,"lines":[" "*90+"wxQ"]})
        probe.validate_document("erase-line-start",2,{"line_count":1,"lines":[" "*91+"x"]})

    def test_mechanics_requires_all_four_observations_and_exact_action(self):
        with tempfile.TemporaryDirectory() as temp:
            directory=Path(temp);state,result=recording()
            values={"peer.json":state,probe.RESULT_FILE:result,"cleanup.json":{"active":0,"total":3,"terminated":0},
                    "profile-check.json":{"only_synthetic_session":True,"session_count":1}}
            for name,value in values.items():(directory/name).write_text(json.dumps(value))
            (directory/"stderr.txt").write_bytes(b"")
            for index in range(4):(directory/f"ack-{index}").write_text("observed")
            self.assertEqual(probe.audit_mechanics(directory,"next-char")["pre_action_stage_count"],2)
            state["records"][2]["source_hex"]="0d51";(directory/"peer.json").write_text(json.dumps(state))
            with self.assertRaisesRegex(ValueError,"Exact action source"):probe.audit_mechanics(directory,"next-char")


class AddressPlanTests(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory(prefix="resize-address-tests-");self.addCleanup(temporary.cleanup)
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
        return probe.prepare_plan(self.source,self.out,self.root/"rea.cmd",self.plan,"next-char",authorized)

    def test_archive_plan_is_offline_and_exactly_ten_source_files(self):
        with patch.object(probe.common.subprocess,"check_output",side_effect=AssertionError("No archive Git")):
            plan=self.prepare()
        self.assertEqual(len(plan["source_files_sha256"]),10)
        self.assertEqual(plan["actual_original_runs"],0);self.assertFalse(self.out.exists())
        with self.assertRaisesRegex(ValueError,"not authorized"):probe.run_plan(self.plan)
        self.assertFalse(self.plan.with_suffix(".attempted").exists())

    def test_changed_case_geometry_or_source_is_rejected_before_attempt(self):
        plan=self.prepare(True)
        for key,value in (("case_id","line-feed"),("phase_count",3),("required_grids",[]),("phases",[]),
                          ("source_identity",{"kind":"git-checkout"}),("launcher_source_sha256","0"*64)):
            self.plan.write_text(json.dumps({**plan,key:value}))
            with self.subTest(key=key),self.assertRaises(ValueError):probe.run_plan(self.plan)
            self.assertFalse(self.plan.with_suffix(".attempted").exists())

    def test_local_boundary_existing_plan_and_existing_attempt_remain_enforced(self):
        with self.assertRaisesRegex(ValueError,"local directory"):
            probe.prepare_plan(self.source,self.root/"elsewhere",self.root/"rea.cmd",self.plan,"next-char")
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
