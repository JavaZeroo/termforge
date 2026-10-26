import copy
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from experiments.build_paths import binary
from experiments.conpty.margin_run import (
    PACKAGE, SOURCE, SOURCE_SHA256, backend_path, margin_result, sha256, validate_lifecycle,
)
from experiments.conpty.run import OwnedJob


class MarginDiagnosticTests(unittest.TestCase):
    def test_original_workload_identity(self):
        import hashlib
        self.assertEqual(len(SOURCE), 175)
        self.assertEqual(hashlib.sha256(SOURCE).hexdigest(), SOURCE_SHA256)
        self.assertEqual(SOURCE.count(b"\x1b[2;3r"), 1)
        self.assertEqual(SOURCE.count(b"\x1b[4;5r"), 1)

    def test_exact_margin_result_and_wrapped_marker(self):
        for columns, rows in ((80, 24), (12, 7)):
            lines = ["TOP_FIXED", "REGION_C", "REGION_D", "BOTTOM_FIXED", ""]
            marker = "TF_MARGIN_DONE"
            lines += [marker[at:at+columns] for at in range(0, len(marker), columns)]
            lines += [""] * (rows - len(lines))
            self.assertTrue(margin_result(lines, columns, rows))
            wrong = lines.copy()
            wrong[3] = "REGION_CIXED"
            self.assertFalse(margin_result(wrong, columns, rows))
            wrong = lines.copy()
            wrong[5] = "TF_MARGIN"
            self.assertFalse(margin_result(wrong, columns, rows))
            self.assertFalse(margin_result(lines + [""], columns, rows))

    def test_lifecycle_requires_exact_observed_types_and_values(self):
        sample = dict(waited=True, exit_code=37, read_error=109, capture_overflow=False,
                      forced_reader_cancel=False, forced_process_cancel=False,
                      cleanup_complete=True, active_children_after=0, side_by_side=False, creation_flags=0)
        validate_lifecycle(sample)
        for key, value in sample.items():
            for replacement in (None, not value if type(value) is bool else value + 1,
                                int(value) if type(value) is bool else False):
                wrong = copy.deepcopy(sample)
                wrong[key] = replacement
                with self.subTest(key=key, replacement=replacement), self.assertRaises(ValueError):
                    validate_lifecycle(wrong)
            wrong = copy.deepcopy(sample)
            del wrong[key]
            with self.subTest(missing=key), self.assertRaises(ValueError):
                validate_lifecycle(wrong)

    def test_unreviewed_backend_pair_is_rejected(self):
        self.assertEqual(backend_path("system"), "system")
        with tempfile.TemporaryDirectory() as root:
            dll = Path(root) / "conpty.dll"
            dll.write_bytes(b"unreviewed")
            (dll.parent / "OpenConsole.exe").write_bytes(b"unreviewed")
            self.assertNotEqual(sha256(dll), PACKAGE["files"]["conpty.dll"])
            with self.assertRaisesRegex(ValueError, "identity"):
                backend_path(dll)

    @unittest.skipUnless(os.name == "nt", "Windows native diagnostic")
    def test_native_eof_and_invalid_gate_never_start_probe(self):
        executable = binary("conpty_margin_probe")
        with tempfile.TemporaryDirectory() as root:
            for index, gate in enumerate((b"", b"NO\n", b"GOXX")):
                out = Path(root) / str(index)
                result = subprocess.run([str(executable), "system", "vt-on", str(out), "80", "24"],
                                        input=gate, capture_output=True, timeout=5,
                                        creationflags=subprocess.CREATE_NO_WINDOW)
                self.assertEqual(result.returncode, 2)
                self.assertIn(b"GO required", result.stderr)
                self.assertFalse(out.exists())

    @unittest.skipUnless(os.name == "nt", "Windows native diagnostic")
    def test_gate_wait_is_bounded_by_owned_job(self):
        with tempfile.TemporaryDirectory() as root:
            out = Path(root) / "absent"
            job = OwnedJob()
            process = subprocess.Popen([str(binary("conpty_margin_probe")), "system", "vt-on", str(out), "80", "24"],
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                job.assign(process.pid)
                with self.assertRaises(subprocess.TimeoutExpired):
                    process.wait(timeout=0.05)
                self.assertFalse(out.exists())
                job.terminate()
                process.communicate(timeout=5)
                self.assertEqual(job.drain()["active"], 0)
                self.assertFalse(out.exists())
            finally:
                if process.poll() is None:
                    job.terminate()
                    process.communicate(timeout=5)
                job.close()


if __name__ == "__main__":
    unittest.main()
