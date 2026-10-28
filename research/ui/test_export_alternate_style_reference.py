"""The public partial-observation exporter must not turn unknown painting green."""
from __future__ import annotations
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from research.ui.alternate_style_reference import phases


def fixture(root):
    directory=root/"raw"
    directory.mkdir()
    script=b"// Synthetic unit fixture; no original process is launched.\n"
    (directory/"probe.js").write_bytes(script)
    (directory/"stderr.txt").write_bytes(b"")
    (directory/"fixture-ready").write_text("stable-NAWS-capacity")
    observations=[]
    records=[]
    for index,(label,source) in enumerate(phases(25,91)):
        observations.append({"phase":index,"label":label,"cursor_reply":[1,1],"paint_requested":True,"matching_marker_draws":0,
                             "terminal":{"protocol":"Telnet","connected":True,"capacity":25,"line_count":1,"lines":["TFM0"],"markers":[],"scroll":0,
                                         "is_alt":{"callable":True,"value":False,"error":None},"is_main":{"callable":True,"value":True,"error":None}}})
        records.append({"phase":index,"label":label,"source_hex":source.hex(),"source_sha256":hashlib.sha256(source).hexdigest(),"cursor_reply":[1,1]})
        (directory/f"ack-{index}").write_text("observed")
    captured={"status":"observed-all-style-phases","error":None,"observations":observations,"draws":[],"fills":[],"paint_calls_seen":0}
    names=("research/ui/alternate_style_reference.py","research/ui/alternate_style_reference.js","research/ui/alternate_reference.py","research/ui/r1_launch.py","research/ui/qt_r1_text.js")
    plan={"issue":123,"maximum_original_runs":1,"source_base_commit":"d91eb589f04be6b54a29d237b87c5b05ad243908",
          "rea_capture_seconds":45,"cli_wait_timeout_seconds":60,"whole_lifecycle_bound_seconds":None,"phase_count":11,
          "probe_files_sha256":{name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in names},
          "reference_exe_sha256":"7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0",
          "reference_files_manifest_sha256":"e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203"}
    data={"run.json":{"sha256":plan["reference_exe_sha256"],"files_sha256":plan["reference_files_manifest_sha256"],"script_sha256":hashlib.sha256(script).hexdigest()},
          "peer.json":{"complete":True,"frozen_grid":[91,25],"columns":91,"rows":25,"naws_history":[[91,24],[91,25]],"received_hex":(b"\x1b[1;1R"*11).hex(),"records":records},
          "style-probe-result.json":captured,"cleanup.json":{"active":0},"profile-check.json":{"only_synthetic_session":True,"session_count":1},
          "evidence.json":{"ok":True,"data":{"cleanup_error":None,"evidence":{"evidence_id":"synthetic-test-only","provider":{"name":"synthetic"},"confidence":"test-fixture","subject":None,"environment":None,"limitations":["Not runtime evidence."],"normalized_result":{"source_sha256":hashlib.sha256(script).hexdigest(),"messages_truncated":False,"cleanup_error":None}}}}}
    write_records(directory,data)
    plan_path=root/"plan.json"
    plan_path.write_text(json.dumps(plan),encoding="utf-8")
    return directory,plan_path,data


def write_records(directory,data):
    captured=data["style-probe-result.json"]
    messages=[{"type":"send","payload":{"kind":"style_phase",**copy.deepcopy(observation)}} for observation in captured["observations"]]
    messages.append({"type":"send","payload":{"kind":"style_reference_result",**copy.deepcopy(captured)}})
    data["evidence.json"]["data"]["evidence"]["normalized_result"]["messages"]=messages
    for name,value in data.items():
        (directory/name).write_text(json.dumps(value),encoding="utf-8")


class PartialStyleExportTests(unittest.TestCase):
    def check_cli(self,change=None):
        for optimized in (False,True):
            with self.subTest(optimized=optimized),tempfile.TemporaryDirectory() as temporary:
                root=Path(temporary)
                directory,plan,data=fixture(root)
                if change:
                    change(data)
                    write_records(directory,data)
                destination=root/"public/validation.json"
                result=subprocess.run([sys.executable,*(["-O"] if optimized else []),str(ROOT/"research/ui/export_alternate_style_reference.py"),"--directory",str(directory),"--plan",str(plan),"--destination",str(destination)],cwd=ROOT,capture_output=True,text=True,timeout=10)
                if change:
                    self.assertNotEqual(result.returncode,0,result.stdout)
                    self.assertFalse(destination.exists())
                    self.assertFalse(destination.parent.exists())
                else:
                    self.assertEqual(result.returncode,0,result.stderr)
                    summary=json.loads(destination.read_text(encoding="utf-8"))
                    self.assertEqual(summary["status"],"partial-mode-and-wrap-observation")
                    self.assertFalse(summary["terminal_style_contract_verified"])
                    self.assertIsNone(summary["paint_observation_attempt"]["entry_style_inheritance"])
                    self.assertIsNone(summary["paint_observation_attempt"]["exit_style_restoration"])
                    self.assertEqual(summary["paint_observation_attempt"]["result"],"unmatched")

    def test_unmatched_painting_exports_only_as_partial_unknown(self):
        self.check_cli()

    def test_nonfixture_text_is_rejected(self):
        self.check_cli(lambda data:data["style-probe-result.json"]["observations"][0]["terminal"]["lines"].__setitem__(0,"PRIVATE_REVIEW_MARKER/host/path"))

    def test_uncalled_mode_getter_cannot_be_published_as_observed(self):
        self.check_cli(lambda data:data["style-probe-result.json"]["observations"][0]["terminal"]["is_alt"].update(callable=False))

    def test_changed_original_identity_is_rejected(self):
        self.check_cli(lambda data:data["run.json"].update(sha256="0"*64))


if __name__=="__main__":
    unittest.main()
