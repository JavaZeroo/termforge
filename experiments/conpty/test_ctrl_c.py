"""Independent raw-output oracle and owned, unattended cmd interrupt gate."""
import copy
import json
import os
from pathlib import Path
import tempfile
import unittest

from experiments.build_paths import ROOT, binary
from experiments.conpty.ctrl_c_run import COMPLETE_INPUT, execute, validate_sample, validate_trace


class CtrlCOracleTests(unittest.TestCase):
    def test_command_echo_does_not_establish_marker_execution(self):
        for raw in (b"TF>echo TF_AFTER_IDLE\r\nTF>echo TF_AFTER_FLOOD\r\n",
                    b"TF_AFTER_IDLE\r\nTF_BUSY_42\r\necho TF_AFTER_FLOOD\r\n",
                    b"TF_AFTER_IDLE\r\nTF_BUSY_42\r\nTF_AFTER_FLOOD"):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                validate_trace(raw, COMPLETE_INPUT)

    def test_marker_order_and_natural_completion_are_rejected(self):
        for raw in (b"TF_AFTER_IDLE\nTF_BUSY_1000000\nTF_AFTER_FLOOD\n",
                    b"TF_AFTER_IDLE\nTF_AFTER_FLOOD\nTF_BUSY_3\n",
                    b"TF_BUSY_3\nTF_AFTER_IDLE\nTF_AFTER_FLOOD\n",
                    b"TF_AFTER_IDLE\nTF_BUSY_3\nTF_AFTER_FLOOD\nTF_AFTER_FLOOD\n",
                    b"TF_AFTER_IDLE\nTF_AFTER_FLOOD\n"):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                validate_trace(raw, COMPLETE_INPUT)

    def test_full_output_lines_and_exact_input_are_required(self):
        raw = (b"TF>echo TF_AFTER_IDLE\r\n\x1b[0mTF_AFTER_IDLE\r\nTF_BUSY_123\r\n"
               b"echo TF_AFTER_FLOOD\r\n\x1b[?25hTF_AFTER_FLOOD\r\n")
        result = validate_trace(raw, COMPLETE_INPUT)
        self.assertEqual(result, dict(idle_marker_lines=1, after_marker_lines=1,
                                      highest_busy=123, busy_lines=1))
        for wrong in (COMPLETE_INPUT.replace(b"1000000", b"1000"),
                      COMPLETE_INPUT.replace(b"\x03echo TF_AFTER_FLOOD", b"\x03\x03echo TF_AFTER_FLOOD"),
                      COMPLETE_INPUT[:-1]):
            with self.assertRaises(ValueError):
                validate_trace(raw, wrong)


@unittest.skipUnless(os.name == "nt", "raw ConPTY requires Windows")
class CtrlCIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe = binary("conpty_ctrl_c_probe")
        parent = ROOT / "local" / "ctrl-c-tests"
        parent.mkdir(parents=True, exist_ok=True)
        cls.out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))

    def test_original_cmd_flood_interrupt_and_cleanup_three_fresh(self):
        for repeat in range(3):
            row = execute(self.exe, self.out / f"cmd-{repeat}")
            self.assertEqual(row["status"], "passed")
            self.assertFalse(row["handler_delivery_verified"])
            self.assertEqual(row["job_after_cleanup"]["active"], 0)
            for key, value in (("after_marker", False), ("highest_busy", 1000000),
                               ("busy_hold_ms", 999), ("flood_input_bytes", 20),
                               ("exit_code", 0), ("cleanup_complete", False),
                               ("forced_reader_cancel", True)):
                bad = copy.deepcopy(row)
                bad["native"][key] = value
                with self.subTest(field=key), self.assertRaises(ValueError):
                    validate_sample(bad)
            for key, value in (("image_query_error", True), ("owned_images", {"cmd.exe": True}),
                               ("job_after", {"active": 1}), ("timed_out", True)):
                bad = copy.deepcopy(row)
                bad[key] = value
                with self.subTest(field=key), self.assertRaises(ValueError):
                    validate_sample(bad)

    def test_eof_and_wrong_gate_cannot_start_conpty(self):
        for number, gate in enumerate((b"", b"NOT_GO\n", b"G" * 64 + b"\n")):
            out = self.out / f"guard-{number}"
            row = execute(self.exe, out, gate=gate)
            self.assertEqual(row["returncode"], 2)
            # CREATE_NO_WINDOW can create the console host's own headless
            # conhost. A PTY client or an additional ConPTY is not permitted.
            self.assertIn(row["job_after"]["total"], (1, 2))
            self.assertEqual(row["job_after_cleanup"]["active"], 0)
            self.assertFalse(row["owned_images"]["cmd.exe"])
            self.assertFalse((out / "_private/input.bin").exists())
            self.assertFalse((out / "_private/output.bin").exists())
            native = json.loads((out / "_private/stdout.json").read_bytes())
            self.assertEqual(native["status"], "guard_rejected")

    def test_withheld_gate_timeout_recovers_only_owned_host(self):
        row = execute(self.exe, self.out / "withheld-go", gate=None, timeout=0.3)
        self.assertTrue(row["timed_out"])
        self.assertIn(row["job_at_timeout"]["total"], (1, 2))
        self.assertEqual(row["job_at_timeout"]["active"], row["job_at_timeout"]["total"])
        self.assertEqual(row["job_at_timeout"]["terminated"], 0)
        self.assertFalse(row["owned_images"]["cmd.exe"])
        self.assertEqual(row["job_after_cleanup"]["active"], 0)
        self.assertFalse((self.out / "withheld-go/_private/input.bin").exists())


if __name__ == "__main__":
    unittest.main()
