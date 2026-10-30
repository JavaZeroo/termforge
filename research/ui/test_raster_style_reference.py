"""Offline synthetic boundaries for single-run raster analysis and publication."""
from __future__ import annotations
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from research.ui.raster_style_reference import MODULE_SHA256,MANIFEST_SHA256,SOURCE_FILES,check_full_hashes,phases
from research.ui.raster_style_analysis import analyze_pixels,audit_capture
from research.ui.export_selection_counterexample import counterexample_rows


def fixture(root,partial=False,selection_stop=False):
    directory=root/"raw";directory.mkdir();script=b"// synthetic offline unit fixture only\n"
    (directory/"probe.js").write_bytes(script);(directory/"stderr.txt").write_bytes(b"")
    colors=[(0xffaa0000,0xff0000aa),(0xffaa0000,0xff0000aa),(0xff00aa00,0xffaa00aa),(0xff00aa00,0xffaa00aa),(0xffaa0000,0xff0000aa)]
    captures=[];records=[]
    for index,(source_label,content) in enumerate(phases(25,91)[:1 if partial else 5]):
        label=["M0 H H","A0 H","A1 H","A2 H","M1 H"][index];fg,bg=colors[index];markers=[];rois=[]
        for ordinal in range(2 if index==0 else 1):
            h=[ordinal*4,0,2,2];space=[ordinal*4+2,0,2,2]
            markers.append({"ordinal":ordinal,"character":"H","preceding_character":" ","h_rect":dict(zip(("x","y","width","height"),h)),"space_rect":dict(zip(("x","y","width","height"),space))})
            hp=[fg,bg,fg,bg];sp=[bg]*4
            rois.append({"ordinal":ordinal,"h_rect":h,"space_rect":space,"h_pixels":hp,"space_pixels":sp,"colors":analyze_pixels(hp,sp)})
        capture={"phase":index,"label":label,"cursor_reply":[1,1],"grab_attempted":True,"module_guards":[{"name":name,"path_guard_passed":True} for name in MODULE_SHA256],
                 "terminal":{"protocol":"Telnet","connected":True,"capacity":25,"line_count":1,"lines":[label],"is_alt":index in (1,2,3),"is_main":index not in (1,2,3),"markers":markers,"selection":"","caret_rect":{"x":8,"y":0,"width":1,"height":1}},
                 "image":{"viewport_width":8,"viewport_height":2,"grab_rect":[0,0,7,1],"pixmap_dpr":1,"image_dpr":1,"width":8,"height":2,"is_null":False},"rois":rois,
                 "constructed":["pixmap","image"],"destroyed":["image","pixmap"],"calibration":True,"reason":None}
        if partial:
            capture.update(calibration=False,reason="empty-image",rois=[])
            capture["image"].update(width=0,height=0,is_null=True)
            if selection_stop:
                capture.update(reason="selection-overlaps-observation",grab_attempted=False,image=None,constructed=[],destroyed=[])
                capture["terminal"]["selection"]="  M0 H H\r\n"
        else:
            (directory/f"ack-{index}").write_text("observed")
        captures.append(capture);records.append({"phase":index,"label":source_label,"source_hex":content.hex(),"source_sha256":hashlib.sha256(content).hexdigest(),"cursor_reply":[1,1]})
    result={"status":"partial-raster-observation" if partial else "observed-five-raster-phases","reason":"selection-overlaps-observation" if selection_stop else "empty-image" if partial else None,"captures":captures,"grab_phases":[] if selection_stop else list(range(len(captures))),"module_guards":captures[0]["module_guards"],"presented_done":None}
    plan={"issue":126,"maximum_original_runs":1,"phase_count":5,"maximum_grabs_per_phase":1,"maximum_grabs":5,"source_base_commit":"0"*40,
          "rea_capture_seconds":45,"cli_wait_timeout_seconds":60,"whole_lifecycle_bound_seconds":None,"module_sha256":MODULE_SHA256,"files_manifest_sha256":MANIFEST_SHA256,
          "source_files_sha256":{name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in SOURCE_FILES}}
    data={"run.json":{"sha256":MODULE_SHA256["WindTerm.exe"],"files_sha256":MANIFEST_SHA256,"script_sha256":hashlib.sha256(script).hexdigest()},
          "raster-result.json":result,"peer.json":{"frozen_grid":[91,25],"columns":91,"rows":25,"freeze_event_count":2,"naws_history":[[91,24],[91,25]],"records":records,"received_hex":(b"\x1b[1;1R"*len(records)).hex(),"complete":not partial},
          "cleanup.json":{"active":0},"profile-check.json":{"only_synthetic_session":True,"session_count":1},
          "evidence.json":{"ok":True,"data":{"cleanup_error":None,"evidence":{"evidence_id":"ev_"+"0"*64,"provider":{"id":"frida","name":"Frida","version":None},"confidence":"observed","subject":None,"environment":None,"limitations":[],"normalized_result":{"source_sha256":hashlib.sha256(script).hexdigest(),"cleanup_error":None,"messages_truncated":False}}}}}
    write_records(directory,data);plan_path=root/"plan.json";plan_path.write_text(json.dumps(plan),encoding="utf-8")
    return directory,plan_path,data


def write_records(directory,data):
    result=data["raster-result.json"]
    messages=[{"type":"send","payload":{"kind":"raster_phase",**copy.deepcopy(capture)}} for capture in result["captures"]]
    messages.append({"type":"send","payload":{"kind":"raster_result",**copy.deepcopy(result)}})
    data["evidence.json"]["data"]["evidence"]["normalized_result"]["messages"]=messages
    for name,value in data.items():(directory/name).write_text(json.dumps(value),encoding="utf-8")


class RasterBoundaryTests(unittest.TestCase):
    def test_historical_counterexample_projects_only_endpoints_and_length(self):
        def snapshot(phase,caret,anchor,text):return {"type":"send","payload":{"kind":"snapshot","phase":phase,"text_state":json.dumps({"caret":caret,"anchor":anchor,"selection":text})}}
        evidence={"normalized_result":{"messages":[snapshot("shell-ready",97,97,"private fixture value"),snapshot("styled-final",400,400,"different private value")]}}
        result=counterexample_rows(evidence)
        self.assertEqual(result,[{"caret":97,"anchor":97,"selection_length":21},{"caret":400,"anchor":400,"selection_length":23}])
        self.assertNotIn("private",json.dumps(result))
        evidence["normalized_result"]["messages"][0]=snapshot("shell-ready",97,98,"private fixture value")
        with self.assertRaises(ValueError):counterexample_rows(evidence)

    def test_full_file_hash_detects_tail_change(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);payload=b"fixed-header-and-body";file=root/"Qt5Gui.dll";file.write_bytes(payload)
            expected={file.name:hashlib.sha256(payload).hexdigest()};self.assertEqual(check_full_hashes(root,expected),expected)
            file.write_bytes(payload[:-1]+b"X")
            with self.assertRaisesRegex(ValueError,"full hash"):check_full_hashes(root,expected)

    @unittest.skipUnless(shutil.which("node"),"Node is required to exercise production JS native-boundary helpers")
    def test_production_js_native_contract(self):
        # Bounded diagnostic for the observed ten second stall (#144): a preload
        # checkpoint records whether Node ever started, without changing the limit.
        with tempfile.TemporaryDirectory() as diagnostic:
            checkpoint=Path(diagnostic)/"node-checkpoint.json"
            preload=Path(diagnostic)/"checkpoint.js"
            preload.write_text("const fs=require(\"fs\");fs.writeFileSync(process.env.TERMFORGE_NODE_CHECKPOINT,JSON.stringify({started:Date.now(),version:process.version}));",encoding="utf-8")
            environment=dict(os.environ,TERMFORGE_NODE_CHECKPOINT=str(checkpoint))
            started=time.monotonic()
            try:
                process=subprocess.Popen(["node","--require",str(preload),str(ROOT/"research/ui/test_raster_native_contract.js")],
                                         stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,env=environment)
            except OSError as error:
                self.fail(f"node fixture could not be created: {error}")
            spawn_seconds=round(time.monotonic()-started,3)
            try:
                stdout,stderr=process.communicate(timeout=10)
            except subprocess.TimeoutExpired as expired:
                # The child state at the deadline separates a process that never
                # started from one that started and then stalled (#144).
                state=checkpoint.read_text(encoding="utf-8") if checkpoint.exists() else "preload checkpoint never written"
                self.fail(f"node fixture exceeded its ten second limit; {state}; "
                          f"pid={process.pid} spawn_seconds={spawn_seconds} "
                          f"exit_at_timeout={process.poll()} "
                          f"partial_stdout_bytes={len(expired.output or '')}")
            result=subprocess.CompletedProcess(process.args,process.returncode,stdout,stderr)
        self.assertEqual(result.returncode,0,result.stderr)

    def test_pixel_analysis_rejects_empty_ambiguous_or_nonuint_samples(self):
        for h,space in (([],[1]),([1],[1]),([2,3],[1,1]),([2,2],[1,3]),([-1],[1])):
            with self.subTest(h=h,space=space),self.assertRaises(ValueError):analyze_pixels(h,space)

    def check_cli(self,change=None,partial=False,selection_stop=False):
        for optimized in (False,True):
            with self.subTest(optimized=optimized),tempfile.TemporaryDirectory() as temporary:
                root=Path(temporary);directory,plan,data=fixture(root,partial,selection_stop)
                if change:change(data);write_records(directory,data)
                output=root/"public/validation.json"
                completed=subprocess.run([sys.executable,*(["-O"] if optimized else []),str(ROOT/"research/ui/export_raster_style_reference.py"),"--directory",str(directory),"--plan",str(plan),"--destination",str(output)],cwd=ROOT,capture_output=True,text=True,timeout=10)
                if change:
                    self.assertNotEqual(completed.returncode,0,completed.stdout);self.assertFalse(output.exists());self.assertFalse(output.parent.exists())
                else:
                    self.assertEqual(completed.returncode,0,completed.stderr);summary=json.loads(output.read_text(encoding="utf-8"))
                    self.assertEqual(summary["sample_colors_verified"],not partial)
                    self.assertEqual(summary["observed_phase_count"],1 if partial else 5)
                    self.assertNotIn("h_pixels",output.read_text());self.assertNotIn("space_pixels",output.read_text())
                    if partial:self.assertIsNone(summary["sample_color_comparisons"])
                    else:self.assertTrue(summary["sample_color_comparisons"]["return_same_sample_colors_as_main"])
                    if selection_stop:
                        self.assertFalse(summary["stop_guard_observation"]["actual_selection_or_overlap_proven"])
                        self.assertEqual(summary["grab_phases"],[])
                        self.assertFalse(summary["controlled_raster_observed"])

    def test_valid_synthetic_five_phase_export(self):self.check_cli()
    def test_empty_image_remains_unknown_and_stops_after_one(self):self.check_cli(partial=True)
    def test_selection_guard_does_not_claim_actual_selection_or_pixels(self):self.check_cli(partial=True,selection_stop=True)
    def test_extra_grab_is_rejected(self):self.check_cli(lambda data:data["raster-result.json"]["grab_phases"].append(0))
    def test_module_path_guard_failure_is_rejected(self):self.check_cli(lambda data:data["raster-result.json"]["module_guards"][0].update(path_guard_passed=False))
    def test_nonfixture_text_is_rejected(self):self.check_cli(lambda data:data["raster-result.json"]["captures"][0]["terminal"]["lines"].__setitem__(0,"PRIVATE_MARKER/host/path"))
    def test_derived_colors_cannot_disagree_with_pixels(self):self.check_cli(lambda data:data["raster-result.json"]["captures"][0]["rois"][0]["colors"].update(background_rgba=[1,2,3,4]))


if __name__=="__main__":unittest.main()
