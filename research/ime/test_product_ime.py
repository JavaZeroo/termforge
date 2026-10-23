"""Collector guards and owned benign children; never inject input on this host."""
from __future__ import annotations

import copy
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

from research.ime import product_runner as runner
from research.ime.product_checks import validate_product_case, validate_product_manifest


class ProductImeSyntheticSchemaTests(unittest.TestCase):
    """Hand-authored schema examples are unit data, never real-input evidence."""
    @staticmethod
    def fixture(mode):
        checkpoints = {
            "ui-ime-basic": [("bottom-preedit", "bottom", False), ("bottom-moved", "bottom", False),
                             ("bottom-commit", "bottom", True), ("bottom-cancel", "bottom", True),
                             ("main-preedit", "main", False), ("main-commit", "main", True), ("main-cancel", "main", True)],
            "ui-ime-geometry": [(name, "main", committed) for name, committed in
                                (("before-output", False), ("after-output", False), ("after-resize", False),
                                 ("after-zoom", False), ("after-dpi", False), ("committed", True), ("cancelled", True))],
            "ui-ime-focus": [("bottom-preedit", "bottom", False), ("bottom-hidden", "bottom", False),
                             ("main-commit", "main", True), ("bottom-resumed", "bottom", True), ("hidden-preedit", "hidden", False)],
            "ui-ime-close": [("before-close", "bottom", False), ("survivor-commit", "main", True), ("survivor-cancel", "main", True)],
        }[mode]
        names = ["bottom", "main", "hidden"] if mode == "ui-ime-focus" else ["bottom", "main"]
        captures = {"bottom": "你好".encode(), "main": "你好".encode()}
        if mode == "ui-ime-focus": captures["hidden"] = b""
        if mode in ("ui-ime-geometry", "ui-ime-close"): captures["bottom"] = b""
        data = {"schema": "termforge-product-ime-v1", "mode": mode,
                "input_desktop": {"station": "WinSta0", "input_name": "Default", "thread_name": "Default", "query_only": True},
                "provider": {"clsid": "{81D4E9C9-1D3B-41BC-9E6C-4B40BF79E35E}",
                             "profile": "{FA550B04-5AD7-411F-A5AC-CA038EC515D7}", "langid": 2052,
                             "language_hresult": 0, "activation_hresult": 0},
                "key_batches": {"ui-ime-basic": 18, "ui-ime-geometry": 9, "ui-ime-focus": 25, "ui-ime-close": 14}[mode],
                "checkpoints": [], "receivers": [], "after_close_candidates": [], "terminal_output": {name: "" for name in names},
                "output_screen_lines": [],
                "rename": {"preedit": "", "text": "", "candidates": []},
                "actions": {name: True for name in {
                    "ui-ime-basic": ("candidate_followed_workspace", "bottom-cancel", "main-cancel"),
                    "ui-ime-geometry": ("twenty_output_rows", "resize", "zoom_125", "controlled_dpi_120", "cancelled"),
                    "ui-ime-focus": ("hidden_cancelled", "rename_cancelled_old", "rename_text_verified"),
                    "ui-ime-close": ("closed_target_destroyed", "closed_receiver_reaped", "closed_candidate_gone", "survivor-cancel"),
                }[mode]}}
        data["guarded_key_batches"] = data["key_batches"]
        counts = dict.fromkeys(names, 0)
        for index, (name, session, committed) in enumerate(checkpoints):
            counts[session] += 1
            hidden = name == "bottom-hidden"
            composing = not committed and not hidden
            generation = names.index(session) + 1
            x, y = 20 + index * 10, 30 + index * 20
            dpi, zoom, cells = (120, 125, [19, 41]) if mode == "ui-ime-geometry" and index >= 4 else (96, 125, [15, 33]) if mode == "ui-ime-geometry" and index == 3 else (96, 100, [12, 26])
            data["checkpoints"].append({"name": name, "session": session, "generation": generation,
                "composing": composing, "preedit": "ni'hao" if composing else "", "composition_events": counts[session],
                "result_events": int(committed), "character_units": [], "committed": "你好" if committed else "", "ime_error": "",
                "display_dpi": dpi, "zoom_percent": zoom, "cell_pixels": cells,
                "caret_client": [x, y, x + cells[0], y + cells[1]], "caret_screen": [x + 100, y + 200, x + 100 + cells[0], y + 200 + cells[1]],
                "caret_positioned": True, "caret_updates": index + 1,
                "monitor_work": [0, 0, 1920, 1080], "os_dpi": 96,
                "candidates": [] if not composing else [{"instance": generation, "class": "MSCandUIWindow_Candidate",
                                                          "rect": [x + 100, y + 208 + cells[1], x + 300, y + 288 + cells[1]]}],
                "received_hex": "e4bda0e5a5bd" if committed else ""})
        for generation, name in enumerate(names, 1):
            close = mode == "ui-ime-close" and name == "bottom"
            data["receivers"].append({"name": name, "generation": generation, "capture": name + ".bin",
                "bytes": len(captures[name]), "hex": captures[name].hex(), "stopped_by_close": close,
                "stopped_event": not close, "process_signaled": True, "exit_code": 99 if close else 0})
        if mode == "ui-ime-focus":
            data["rename"] = {"preedit": "ni'hao", "text": "你好", "candidates": [
                {"instance": 4, "class": "MSCandUIWindow_Candidate", "rect": [300, 300, 500, 380]}]}
        if mode == "ui-ime-geometry":
            data["output_screen_lines"] = ["TF_IME_OUTPUT " + str(index) for index in range(20)] + ["TF_IME_OUTPUT_DONE"]
            data["terminal_output"]["main"] = (b"".join(b"TF_IME_OUTPUT " + str(index).encode("ascii") + b"\r\n" for index in range(20)) +
                                                  b"TF_IME_OUTPUT_DONE\r\n").hex()
        return data, captures

    def test_four_literal_synthetic_schema_examples_are_independent_of_real_input(self):
        for mode in ("ui-ime-basic", "ui-ime-geometry", "ui-ime-focus", "ui-ime-close"):
            data, captures = self.fixture(mode)
            with self.subTest(mode=mode):
                self.assertTrue(validate_product_manifest(data, mode, captures)["raw_receiver_bytes_verified"])

    def test_schema_rejects_preedit_duplicates_stale_generation_and_forged_geometry(self):
        data, captures = self.fixture("ui-ime-basic")
        mutations = (("generation", 2), ("received_hex", "6e69"), ("ime_error", "native failure"),
                     ("caret_positioned", False), ("character_units", [0x4f60]), ("result_events", 1))
        for field, wrong in mutations:
            bad = copy.deepcopy(data); bad["checkpoints"][0][field] = wrong
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_product_manifest(bad, "ui-ime-basic", captures)
        for wrong in (b"nihao", "你好你好".encode(), "你好".encode() + b"\x1b", b""):
            bad_captures = dict(captures, bottom=wrong)
            with self.subTest(raw=wrong), self.assertRaises(ValueError):
                validate_product_manifest(data, "ui-ime-basic", bad_captures)
        bad = copy.deepcopy(data); bad["checkpoints"][1]["candidates"][0]["rect"][0] += 100
        with self.assertRaises(ValueError): validate_product_manifest(bad, "ui-ime-basic", captures)
        bad = copy.deepcopy(data); bad["checkpoints"][3]["composition_events"] = bad["checkpoints"][2]["composition_events"]
        with self.assertRaises(ValueError): validate_product_manifest(bad, "ui-ime-basic", captures)
        bad = copy.deepcopy(data); bad["guarded_key_batches"] -= 1
        with self.assertRaises(ValueError): validate_product_manifest(bad, "ui-ime-basic", captures)
        bad = copy.deepcopy(data); bad["provider"]["activation_hresult"] = False
        with self.assertRaises(ValueError): validate_product_manifest(bad, "ui-ime-basic", captures)

    def test_candidate_absolute_anchor_rejects_constant_offset_and_allows_screen_edges(self):
        data, captures = self.fixture("ui-ime-basic")
        for offset in ((500, 0), (0, 500)):
            bad=copy.deepcopy(data)
            for row in bad["checkpoints"]:
                for candidate in row["candidates"]:
                    candidate["rect"]=[n+offset[i%2] for i,n in enumerate(candidate["rect"])]
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, "anchored"):
                validate_product_manifest(bad,"ui-ime-basic",captures)
        for placement in ("above", "right-edge"):
            good=copy.deepcopy(data)
            for row in good["checkpoints"]:
                for candidate in row["candidates"]:
                    rect=candidate["rect"]; width,height=rect[2]-rect[0],rect[3]-rect[1]
                    if placement=="above":
                        rect[1],rect[3]=row["caret_screen"][1]-height-4,row["caret_screen"][1]-4
                    else:
                        row["monitor_work"][2]=row["caret_screen"][0]+width-20
                        rect[0],rect[2]=row["monitor_work"][2]-width,row["monitor_work"][2]
            validate_product_manifest(good,"ui-ime-basic",captures)

    def test_geometry_focus_rename_and_close_require_actual_observations(self):
        data, captures = self.fixture("ui-ime-geometry")
        for field in ("caret_screen", "caret_updates"):
            bad = copy.deepcopy(data); bad["checkpoints"][2][field] = copy.deepcopy(bad["checkpoints"][1][field])
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_product_manifest(bad, "ui-ime-geometry", captures)
        for phase, field, wrong in ((3, "zoom_percent", 100), (4, "display_dpi", 96), (3, "cell_pixels", [12, 26])):
            bad = copy.deepcopy(data); bad["checkpoints"][phase][field] = wrong
            with self.subTest(metrics=field), self.assertRaises(ValueError): validate_product_manifest(bad, "ui-ime-geometry", captures)
        original = data["output_screen_lines"]
        for wrong in (original[:8] + original[9:], original + [original[8]],
                      original[:8] + [original[9], original[8]] + original[10:]):
            bad = copy.deepcopy(data); bad["output_screen_lines"] = wrong
            with self.subTest(output=wrong[-3:]), self.assertRaises(ValueError): validate_product_manifest(bad, "ui-ime-geometry", captures)
        # The console server may replace producer CRLF with cursor VT and
        # repeat rendered markers during resize; the retained rows stay exact.
        raw = bytes.fromhex(data["terminal_output"]["main"])
        data["terminal_output"]["main"] = (raw.replace(b"\r\n", b"\x1b[H") + b"TF_IME_OUTPUT 8").hex()
        validate_product_manifest(data, "ui-ime-geometry", captures)
        data, captures = self.fixture("ui-ime-focus")
        for field, wrong in (("text", "你好你好"), ("preedit", ""), ("candidates", [])):
            bad = copy.deepcopy(data); bad["rename"][field] = wrong
            with self.subTest(rename=field), self.assertRaises(ValueError): validate_product_manifest(bad, "ui-ime-focus", captures)
        bad = copy.deepcopy(data); bad["checkpoints"][1]["candidates"] = copy.deepcopy(bad["checkpoints"][0]["candidates"])
        with self.assertRaises(ValueError): validate_product_manifest(bad, "ui-ime-focus", captures)
        with self.assertRaises(ValueError): validate_product_manifest(data, "ui-ime-focus", dict(captures, hidden="你好".encode()))
        data, captures = self.fixture("ui-ime-close")
        for field, wrong in (("exit_code", 259), ("process_signaled", False), ("stopped_by_close", False)):
            bad = copy.deepcopy(data); bad["receivers"][0][field] = wrong
            with self.subTest(closed_receiver=field), self.assertRaises(ValueError): validate_product_manifest(bad, "ui-ime-close", captures)
        bad = copy.deepcopy(data); bad["after_close_candidates"] = copy.deepcopy(bad["checkpoints"][0]["candidates"])
        with self.assertRaises(ValueError): validate_product_manifest(bad, "ui-ime-close", captures)

    def test_real_artifact_gate_rejects_corruption_and_false_inner_or_outer_job_zero(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary); mode = "ui-ime-basic"; data, captures = self.fixture(mode)
            (directory / "product-ime.json").write_text(json.dumps(data), encoding="utf-8")
            for name, value in captures.items(): (directory / (name + ".bin")).write_bytes(value)
            bitmap = bytearray(58)
            struct.pack_into("<2sIHHI", bitmap, 0, b"BM", 58, 0, 0, 54)
            struct.pack_into("<IiiHHIIiiII", bitmap, 14, 40, 1, -1, 1, 32, 0, 4, 0, 0, 0, 0)
            (directory / "frame.bmp").write_bytes(bitmap)
            shell = {"valid": True, "mode": mode, "sessions_closed": 2, "active_children_after": 0, "presented_done": None}
            launcher = {"schema": "termforge-hosted-ime-launcher-v1", "mode": mode, "child_exit_code": 0,
                        "child_signaled": True, "timed_out": False, "inner_job_after": {"active": 0, "total": 1, "terminated": 0}}
            row = {"mode": mode, "status": "collected", "assigned": True, "gate_released": True, "process_reaped": True,
                   "timed_out": False, "returncode": 0, "forced_job_termination": False, "cleanup_errors": [],
                   "evidence_errors": [], "error": None, "outer_job_before_cleanup": {"active": 0, "total": 1, "terminated": 0},
                   "outer_job_after": {"active": 0, "total": 1, "terminated": 0},
                   "lifecycle": [{"stage": stage} for stage in ("starting", "awaiting-go", "job-assigned", "releasing-go", "finished")]}
            def streams(first=shell, last=launcher):
                (directory / "stdout.jsonl").write_text(json.dumps(first) + "\n" + json.dumps(last) + "\n", encoding="utf-8")
                (directory / "stderr.txt").write_bytes(b"")
                row["streams"] = {name: runner.file_identity(directory / name) for name in ("stdout.jsonl", "stderr.txt")}
            streams()
            self.assertTrue(validate_product_case(directory, mode, row)["inner_and_outer_job_zero_verified"])
            for field, wrong in (("timed_out", True), ("forced_job_termination", True), ("desktop_gone", True),
                                 ("outer_job_after", {"active": 1, "total": 1, "terminated": 0}),
                                 ("cleanup_errors", [{"kind": "unknown-job-state"}])):
                bad = copy.deepcopy(row); bad[field] = wrong
                with self.subTest(field=field), self.assertRaises(ValueError): validate_product_case(directory, mode, bad)
            bad = copy.deepcopy(launcher); bad["inner_job_after"]["active"] = 1; streams(last=bad)
            with self.assertRaises(ValueError): validate_product_case(directory, mode, row)
            streams(last=dict(launcher, timed_out=True))
            with self.assertRaises(ValueError): validate_product_case(directory, mode, row)
            bad = dict(shell, valid=1); streams(first=bad)
            with self.assertRaises(ValueError): validate_product_case(directory, mode, row)
            streams(); (directory / "stderr.txt").write_bytes(b"changed raw bytes")
            with self.assertRaises(ValueError): validate_product_case(directory, mode, row)
            streams(); (directory / "bottom.bin").write_bytes("你好".encode() + b"\x1b")
            with self.assertRaises(ValueError): validate_product_case(directory, mode, row)


class ProductImeGuardTests(unittest.TestCase):
    def test_workflow_keeps_product_activation_input_explicit_and_uploads_only_named_evidence(self):
        workflow = (runner.ROOT / ".github/workflows/r1-ime.yml").read_text(encoding="utf-8")
        self.assertIn("default: inventory", workflow)
        def step(name):
            match = re.search(r"(?m)^      - name: " + re.escape(name) + r"\n(.*?)(?=^      - |\Z)", workflow, re.S | re.M)
            self.assertIsNotNone(match, name)
            return match.group(1)
        for name in ("Configure probe", "Build native IME probe and product receiver",
                     "Activate existing fixed Microsoft Pinyin in disposable hosted VM",
                     "Require real composition, candidate position, commit and cancel",
                     "Require four real product IME workflows and owned cleanup"):
            with self.subTest(step=name): self.assertIn("if: inputs.mode == 'verify'", step(name))
        build = step("Build native IME probe and product receiver")
        self.assertIn("shell_test_launcher shell_ime_client", build)
        product = step("Require four real product IME workflows and owned cleanup")
        self.assertIn("timeout-minutes: 5", product)
        self.assertIn("python -m research.ime.product_runner", product)
        self.assertIn("--timeout 60 --out local/hosted-ime/product", product)
        self.assertNotIn("product_runner", workflow.split("  workflow_dispatch:", 1)[0])
        upload = step("Preserve bounded IME evidence on success or failure")
        self.assertIn("if: always()", upload)
        paths = {line.strip() for line in upload.splitlines() if line.strip().startswith("local/hosted-ime/product/")}
        self.assertEqual(paths, {"local/hosted-ime/product/manifest.json"} | {
            "local/hosted-ime/product/*/" + name for name in ("result.json", "product-ime.json", "progress.jsonl", "stdout.jsonl",
                                                           "stderr.txt", "frame.bmp", "bottom.bin", "main.bin", "hidden.bin",
                                                           "bottom.bin.receiver.json", "main.bin.receiver.json", "hidden.bin.receiver.json")})
        self.assertNotIn(".identity", upload)

    def test_public_guard_precedes_every_directory_and_process_operation(self):
        with patch.dict(os.environ, {}, clear=True), patch.object(Path, "mkdir") as mkdir, \
                patch.object(runner.subprocess, "Popen") as popen, patch.object(Path, "resolve") as resolve:
            with self.assertRaises(ValueError):
                runner.execute_hosted_ime(Path("missing-launcher.exe"), Path("must-not-create"))
            mkdir.assert_not_called(); popen.assert_not_called(); resolve.assert_not_called()

    def test_designated_environment_metadata_guard_is_pure(self):
        environment = {"GITHUB_ACTIONS": "true", "RUNNER_ENVIRONMENT": "github-hosted", "RUNNER_OS": "Windows",
                       "GITHUB_REPOSITORY": "JavaZeroo/termforge", "ImageOS": "win25", "ImageVersion": "20261021.278.1"}
        runner.require_hosted(environment)
        for name in environment:
            for wrong in ("", "invalid"):
                changed = dict(environment, **{name: wrong})
                with self.subTest(name=name, wrong=wrong), self.assertRaises(ValueError):
                    runner.require_hosted(changed)

    def test_real_cli_refuses_nonhosted_without_creating_output(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "refused"
            environment = os.environ.copy()
            for key in runner.ENVIRONMENT_KEYS:
                environment.pop(key, None)
            result = subprocess.run([sys.executable, "-m", "research.ime.product_runner", "--launcher", "missing.exe",
                                     "--out", str(out)], capture_output=True, env=environment, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(out.exists())
            self.assertIn(b"designated disposable GitHub-hosted", result.stderr)

    def test_existing_case_directory_and_old_failure_are_never_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "case"; out.mkdir()
            (out / "result.json").write_bytes(b"old failure\xff")
            with patch.object(runner, "OwnedJob") as job:
                with self.assertRaises(FileExistsError):
                    runner._run_gated_command("synthetic", out, ["unused"], 1)
                job.assert_not_called()
            self.assertEqual((out / "result.json").read_bytes(), b"old failure\xff")


@unittest.skipUnless(os.name == "nt", "owned Job tests require Windows")
class ProductImeBenignJobTests(unittest.TestCase):
    def run_child(self, directory, source, timeout=2):
        path = Path(directory) / "benign.py"
        path.write_text(source, encoding="utf-8")
        return runner._run_gated_command("synthetic-no-input", Path(directory) / "case",
                                         [sys.executable, str(path)], timeout)

    def test_assignment_failure_never_releases_go_and_reaps_the_real_launcher(self):
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / "go-received"
            source = "import pathlib,sys\nif sys.stdin.readline() == 'GO\\n': pathlib.Path(sys.argv[0]).with_name('go-received').touch()\n"
            with patch.object(runner.OwnedJob, "assign", side_effect=OSError("injected assignment failure")):
                row = self.run_child(directory, source)
            self.assertEqual(row["status"], "failed")
            self.assertFalse(row["assigned"]); self.assertFalse(row["gate_released"])
            self.assertTrue(row["process_reaped"]); self.assertFalse(marker.exists())
            self.assertEqual(row["outer_job_after"]["active"], 0)
            self.assertEqual(row["cleanup_errors"], [])

    def test_real_raw_stream_bytes_and_job_zero_remain_separate_from_ime_verification(self):
        with tempfile.TemporaryDirectory() as directory:
            row = self.run_child(directory, "import sys\nassert sys.stdin.readline() == 'GO\\n'\nsys.stdout.buffer.write(b'\\xff\\n');sys.stdout.flush()\nsys.stderr.buffer.write(b'\\x80\\r\\n');sys.stderr.flush()\n")
            self.assertEqual(row["status"], "collected")
            self.assertTrue(row["assigned"] and row["gate_released"] and row["process_reaped"])
            self.assertFalse(row["forced_job_termination"] or row["real_ime_product_verified"])
            self.assertEqual(row["outer_job_before_cleanup"]["active"], 0)
            self.assertEqual(row["outer_job_after"]["active"], 0)
            for name, expected in (("stdout.jsonl", b"\xff\n"), ("stderr.txt", b"\x80\r\n")):
                self.assertEqual((Path(directory) / "case" / name).read_bytes(), expected)
                self.assertEqual(row["streams"][name], {"bytes": len(expected), "sha256": hashlib.sha256(expected).hexdigest()})

    def test_timeout_reclaims_real_descendant_and_preserves_partial_raw_output(self):
        with tempfile.TemporaryDirectory() as directory:
            source = ("import subprocess,sys,time\nassert sys.stdin.readline() == 'GO\\n'\n"
                      "subprocess.Popen([sys.executable,'-c','import time;time.sleep(30)'])\n"
                      "sys.stdout.buffer.write(b'partial\\xff');sys.stdout.flush()\ntime.sleep(30)\n")
            row = self.run_child(directory, source, timeout=0.4)
            self.assertEqual(row["status"], "timed-out")
            self.assertTrue(row["timed_out"] and row["forced_job_termination"] and row["process_reaped"])
            self.assertGreaterEqual(row["outer_job_before_cleanup"]["active"], 2)
            self.assertEqual(row["outer_job_after"]["active"], 0)
            self.assertEqual((Path(directory) / "case/stdout.jsonl").read_bytes(), b"partial\xff")
            self.assertFalse(row["real_ime_product_verified"])

    def test_checkpoint_failure_after_assignment_reaps_without_sending_go(self):
        with tempfile.TemporaryDirectory() as directory:
            write = runner.write_json
            def checkpoint(path, data):
                if data["lifecycle"][-1]["stage"] == "job-assigned":
                    raise OSError("injected checkpoint write failure")
                return write(path, data)
            with patch.object(runner, "write_json", side_effect=checkpoint):
                row = self.run_child(directory, "import pathlib,sys\nif sys.stdin.readline() == 'GO\\n': pathlib.Path(sys.argv[0]).with_name('must-not-run').touch()\n")
            self.assertEqual(row["status"], "failed")
            self.assertTrue(row["assigned"] and row["process_reaped"])
            self.assertFalse(row["gate_released"])
            self.assertEqual(row["outer_job_after"]["active"], 0)
            self.assertFalse((Path(directory) / "must-not-run").exists())

    def test_unknown_job_accounting_never_becomes_a_zero_or_verified_result(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(runner.OwnedJob, "drain", side_effect=OSError("injected Job query failure")):
                row = self.run_child(directory, "import sys\nassert sys.stdin.readline() == 'GO\\n'\n")
            self.assertEqual(row["status"], "cleanup-failed")
            self.assertTrue(row["process_reaped"] and row["forced_job_termination"])
            self.assertIsNone(row["outer_job_after"])
            self.assertTrue(row["cleanup_errors"])
            self.assertFalse(row["real_ime_product_verified"])

    def test_job_termination_failure_preserves_original_timeout(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(runner.OwnedJob, "terminate", side_effect=OSError("injected Job termination failure")):
                row = self.run_child(directory, "import sys,time\nassert sys.stdin.readline() == 'GO\\n'\ntime.sleep(30)\n", timeout=0.15)
            self.assertEqual(row["status"], "cleanup-failed")
            self.assertEqual(row["error"]["kind"], "deadline-exceeded")
            self.assertTrue(row["timed_out"] and row["process_reaped"])
            self.assertEqual(row["outer_job_after"]["active"], 0)
            self.assertTrue(row["cleanup_errors"])
            self.assertFalse(row["real_ime_product_verified"])


if __name__ == "__main__":
    unittest.main()
