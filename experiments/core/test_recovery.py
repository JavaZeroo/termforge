"""Compiled-parser recovery checks with independent content and error expectations."""
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess
import tempfile
import unittest

from experiments.build_paths import ROOT, binary


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "input.bin"
        self.last_run = None

    def tearDown(self):
        result = self._outcome.result
        if self.last_run and any(test is self for test, _ in result.failures + result.errors):
            self.preserve(*self.last_run)

    def run_input(self, data, chunks, *, mode="recovery", rows=2, cols=80, finish=None):
        self.path.write_bytes(data)
        command = [str(binary("recovery_probe")), str(self.path), str(rows), str(cols), "2",
                   ",".join(map(str, chunks)), mode]
        if finish is not None:
            command.append(str(finish))
        self.last_run = (data, command, "", "")
        try:
            result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", timeout=15)
        except subprocess.TimeoutExpired as error:
            def decoded(value):
                return value.decode("utf-8", errors="replace") if isinstance(value, bytes) else value or ""
            self.last_run = (data, command, decoded(error.stdout), decoded(error.stderr) + "\nTIMEOUT")
            self.preserve(*self.last_run)
            raise
        self.last_run = (data, command, result.stdout, result.stderr)
        if result.returncode and mode == "recovery":
            self.preserve(data, command, result.stdout, result.stderr)
        if mode == "recovery":
            self.assertEqual(result.returncode, 0, result.stderr)
            try:
                json.loads(result.stdout)
            except ValueError:
                self.preserve(*self.last_run)
                raise
        return result

    def preserve(self, data, command, stdout, stderr):
        folder = Path(os.environ.get("TERMFORGE_TEST_ARTIFACTS", ROOT / "build/test-artifacts")) / "recovery"
        folder.mkdir(parents=True, exist_ok=True)
        key = hashlib.sha256(data + json.dumps(command[2:]).encode()).hexdigest()[:20]
        (folder / f"{key}.bin").write_bytes(data)
        (folder / f"{key}.json").write_text(json.dumps(
            {"arguments_after_input": command[2:], "stdout": stdout, "stderr": stderr},
            indent=2, ensure_ascii=False), encoding="utf-8")

    def expect(self, data, text, *, stats=None, cols=80, finish=None, long=False):
        # Every fixed chunk size for short cases plus deterministic nonuniform
        # patterns; expected text and counters never come from the parser.
        rng = random.Random(20261023)
        patterns = [[1], [511], [4096]] if long else [[n] for n in range(1, len(data) + 2)]
        patterns += [[rng.randint(1, 13) for _ in range(17)] for _ in range(3)]
        for chunks in patterns:
            with self.subTest(chunks=chunks):
                result = self.run_input(data, chunks, cols=cols, finish=finish)
                value = json.loads(result.stdout)
                try:
                    screen = value["screen"]
                    self.assertEqual(screen["lines"], [text + " " * (cols - len(text)), " " * cols])
                    self.assertEqual(screen["history"], [])
                    self.assertEqual(screen["row"], 0)
                    self.assertEqual(screen["column"], min(len(text), cols - 1))
                    self.assertEqual(screen["wrap_pending"], len(text) == cols)
                    self.assertEqual(value["buffered_input_bytes_at_end"], 0)
                    d = value["diagnostics"]
                    self.assertLessEqual(d["max_parameters"], 16)
                    self.assertLessEqual(d["max_utf8_bytes"], 4)
                    self.assertLessEqual(d["max_sequence_bytes"], 4097)
                    self.assertLessEqual(value["max_buffered_input_bytes_after_feed"], 16 * 4 + 4)
                    for key, expected in (stats or {}).items():
                        self.assertEqual(d[key], expected, key)
                except AssertionError:
                    self.preserve(*self.last_run)
                    raise

    def test_unknown_escape_and_intermediates(self):
        self.expect(b"A\x1b7\x1b(B\x1b#8Z", "AZ", stats={"ignored_sequences": 3})

    def test_ech_default_zero_count_clamp_and_cursor_are_independently_expected(self):
        for sequence, expected in ((b"\x1b[X", "a cd"), (b"\x1b[0X", "a cd"),
                                   (b"\x1b[2X", "a  d"), (b"\x1b[79X", "a   ")):
            data = b"abcd\r\nWXYZ\x1b[1;2H" + sequence
            for mode in ("strict", "recovery"):
                for chunks in ([1], [2, 3, 7], [4096]):
                    with self.subTest(sequence=sequence, mode=mode, chunks=chunks):
                        result = self.run_input(data, chunks, mode=mode, cols=4)
                        self.assertEqual(result.returncode, 0, result.stderr)
                        value = json.loads(result.stdout)
                        self.assertEqual(value["screen"]["lines"], [expected, "WXYZ"])
                        self.assertEqual((value["screen"]["row"], value["screen"]["column"]), (0, 1))
                        self.assertFalse(value["screen"]["wrap_pending"])

    def test_ech_resets_pending_wrap_and_preserves_existing_wide_glyph_policy(self):
        cases = [(b"ABCD\x1b[X", ["ABC ", "    "], 3, False),
                 (b"ABCD\x1b[XZ", ["ABCZ", "    "], 3, True),
                 ("中A\x1b[1G\x1b[X".encode(), ["  A ", "    "], 0, False),
                 ("中A\x1b[2G\x1b[X".encode(), ["  A ", "    "], 1, False)]
        for data, expected, column, pending in cases:
            for chunks in ([1], [3, 1, 7], [4096]):
                with self.subTest(data=data, chunks=chunks):
                    screen = json.loads(self.run_input(data, chunks, cols=4).stdout)["screen"]
                    self.assertEqual(screen["lines"], expected)
                    self.assertEqual((screen["row"], screen["column"], screen["wrap_pending"]), (0, column, pending))

    def test_both_real_more_captures_keep_their_complete_bytes_and_expected_ech_result(self):
        fixtures = ROOT / "experiments/core/fixtures"
        identities = (("360", "d035d53385788d2b68dcc0d866b5a2fe68897cdcb28a761bd4d36efcc6df6cf9", ") --        ", 5),
                      ("366", "bb9dbe77353245a2aad7ef07e12da9a8371cc7ac56d6b47ebc0bfecee35b9958", "            ", 0))
        for count, digest, last, column in identities:
            data = (fixtures / f"more-pager-{count}.bin").read_bytes()
            self.assertEqual(len(data), int(count))
            self.assertEqual(hashlib.sha256(data).hexdigest(), digest)
            if count == "366":
                self.assertTrue(data.endswith(b"\r\x1b[79X"))
            for chunks in ([1], [2, 7, 31], [4096]):
                with self.subTest(bytes=count, chunks=chunks):
                    value = json.loads(self.run_input(data, chunks, rows=3, cols=12).stdout)
                    self.assertEqual(value["screen"]["lines"], ["TF_PAGE_23  ", "-- More (56%", last])
                    self.assertEqual((value["screen"]["row"], value["screen"]["column"]), (2, column))
                    self.assertFalse(value["screen"]["wrap_pending"])

    def test_unknown_csi_consumes_private_and_intermediate(self):
        # CSI 6n is a handled cursor report now, so only the private and the
        # intermediate forms count as ignored here.
        self.expect(b"A\x1b[?9999h\x1b[1 q\x1b[6nZ", "AZ", stats={"ignored_sequences": 2})

    def test_cursor_report_is_handled_not_ignored(self):
        self.expect(b"A\x1b[6nZ", "AZ", stats={"ignored_sequences": 0})

    def test_known_alternate_mode_is_not_counted_as_ignored(self):
        self.expect(b"A\x1b[?1049hALT\x1b[?1049hNEW\x1b[?1049lZ", "AZ",
                    stats={"ignored_sequences": 0})

    def test_invalid_parameter_does_not_apply_prefix(self):
        # ED3 (CSI 3J) is implemented now, so only the bad CUP and CSI 3K count.
        self.expect(b"A\x1b[2;3;4H\x1b[3J\x1b[3KZ", "AZ", stats={"ignored_sequences": 2})

    def test_invalid_sgr_is_atomic(self):
        for data in (b"\x1b[1;999mX", b"\x1b[31m\x1b[0;999mX"):
            for chunks in ([1], [2, 7, 3], [4096]):
                value = json.loads(self.run_input(data, chunks).stdout)
                self.assertEqual(value["diagnostics"]["ignored_sequences"], 1)
                if b"31m" in data:
                    self.assertEqual(value["screen"]["current_style"], {"fg": 1, "bg": -1, "bold": False})
                else:
                    self.assertNotIn("current_style", value["screen"])
                    self.assertNotIn("cell_details", value["screen"])

    def test_osc_bel_and_st(self):
        self.expect(b"A\x1b]0;hidden\x07B\x1b]52;c;clipboard\x1b\\Z", "ABZ",
                    stats={"ignored_sequences": 2})

    def test_dcs_and_other_strings_do_not_use_bel(self):
        for introducer in (b"P", b"X", b"^", b"_"):
            self.expect(b"A\x1b" + introducer + b"hidden\x07tail\x1b\\Z", "AZ",
                        stats={"ignored_sequences": 1})

    def test_embedded_escape_cannot_execute_from_string(self):
        self.expect(b"A\x1bP\x1b[2J\x1b]52;copy\x00\xff\r\n\x1b\x1b\\Z", "AZ",
                    stats={"ignored_sequences": 1, "malformed_utf8": 0})

    def test_cancel_strings_and_sequences(self):
        for cancel in (b"\x18", b"\x1a"):
            for prefix in (b"\x1b[123", b"\x1b(", b"\x1bPbody", b"\x1b]body"):
                self.expect(b"A" + prefix + cancel + b"Z", "AZ", stats={"cancelled_sequences": 1})

    def test_escape_resynchronizes_csi(self):
        self.expect(b"A\x1b[999;\x1b[2CZ", "A  Z", stats={"resynchronized_sequences": 1})

    def test_c0_is_ignored_inside_control_sequence(self):
        self.expect(b"A\x1b\x00[2\r\nCZ", "A  Z", stats={"ignored_controls": 3})

    def test_ground_controls_are_ignored_without_replies(self):
        self.expect(b"A\x00\x05\x07\x0b\x0c\x18\x1a\x7fZ", "AZ", stats={"ignored_controls": 8})

    def test_csi_numeric_overflow_discards_tail(self):
        self.expect(b"A\x1b[" + b"9" * 1000000 + b";1HZ", "AZ", long=True,
                    stats={"ignored_sequences": 1, "oversized_sequences": 1, "max_sequence_bytes": 33})

    def test_csi_parameter_limit(self):
        self.expect(b"A\x1b[" + b";" * 16 + b"mZ", "AZ", stats={"ignored_sequences": 1, "max_parameters": 16})

    def test_csi_length_limit(self):
        self.expect(b"A\x1b[" + b"0" * 33 + b"mZ", "AZ",
                    stats={"ignored_sequences": 1, "oversized_sequences": 1})

    def test_long_escape_intermediate(self):
        self.expect(b"A\x1b" + b"(" * 1000000 + b"BZ", "AZ", long=True,
                    stats={"ignored_sequences": 1, "oversized_sequences": 1, "max_sequence_bytes": 33})

    def test_long_string_has_no_body_buffer(self):
        self.expect(b"A\x1b]52;" + b"x" * 1000000 + b"\x1b\\Z", "AZ", long=True,
                    stats={"ignored_sequences": 1, "oversized_sequences": 1, "max_sequence_bytes": 4097,
                           "max_parameters": 0, "max_utf8_bytes": 0})

    def test_invalid_utf8_maximal_subparts(self):
        # Python's decoder is an independent oracle for the documented Unicode
        # maximal-subpart policy, restricted here to invalid bytes and ASCII.
        for data in (b"A\xc2Z", b"A\xe1\x80Z", b"A\xf1\x80\x80Z", b"A\xc0\xafZ",
                     b"A\xe0\x80\xbfZ", b"A\xed\xa0\x80Z", b"A\xf4\x90\x80\x80Z",
                     b"A\x80\xbf\xffZ"):
            text = data.decode("utf-8", errors="replace")
            self.expect(data, text, stats={"malformed_utf8": text.count("\ufffd")})

    def test_bad_utf8_does_not_swallow_escape(self):
        self.expect(b"A\xe1\x80\x1b[2CZ", "A\ufffd  Z", stats={"malformed_utf8": 1})

    def test_raw_c1_and_encoded_c1_are_not_reinterpreted(self):
        self.expect(b"A\x9b\xc2\x9bZ", "A\ufffd\ufffdZ",
                    stats={"malformed_utf8": 1, "replaced_scalars": 1})

    def test_unsupported_valid_scalar_replacement(self):
        self.expect("A\u200d\ufe0f\ue000Z".encode(), "A\ufffd\ufffd\ufffdZ", stats={"replaced_scalars": 3})

    def test_combining_without_base_replacement(self):
        self.expect("\u0301Z".encode(), "\ufffdZ", stats={"replaced_scalars": 1})

    def test_combining_limit_remains_bounded(self):
        value = json.loads(self.run_input(("e" + "\u0301" * 17 + "Z").encode(), [1]).stdout)
        self.assertEqual(value["diagnostics"]["replaced_scalars"], 1)
        self.assertEqual(value["screen"]["cell_details"][0]["text"], "e" + "\u0301" * 16)
        self.assertEqual(value["screen"]["lines"][0], "e" + "\u0301" * 16 + "\ufffdZ" + " " * 77)

    def test_wide_scalar_on_single_column(self):
        self.expect("中".encode(), "\ufffd", cols=1, stats={"replaced_scalars": 1})

    def test_truncated_control_is_dropped_at_finish(self):
        for prefix in (b"\x1b", b"\x1b[31", b"\x1b]hidden", b"\x1bPbody\x1b"):
            self.expect(b"A" + prefix + b"Z", "AZ", finish=1 + len(prefix),
                        stats={"truncated_sequences": 1})

    def test_truncated_utf8_replaced_once_at_finish(self):
        for prefix in (b"\xc2", b"\xe1\x80", b"\xf1\x80\x80"):
            self.expect(b"A" + prefix + b"Z", "A\ufffdZ", finish=1 + len(prefix),
                        stats={"truncated_sequences": 1, "malformed_utf8": 1})

    def test_unterminated_string_consumes_all_until_eof(self):
        self.expect(b"A\x1b]hidden\r\nZ", "A", stats={"truncated_sequences": 1})

    def test_strict_still_rejects_invalid_input(self):
        # ?25 is a supported mode now, so the unsupported-private sample uses ?12.
        for data in (b"\x1b7", b"\x1b]title\x07", b"\xff", b"\xe1\x80", b"\x1b[?12h", b"\x1b[31"):
            result = self.run_input(data, [1], mode="strict")
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, "")

    def test_common_subset_retains_strict_snapshot(self):
        data = "A中e\u0301\x1b[31mB\x1b[0m\r\nlast\x1b[2DXY".encode()
        for chunks in ([1], [2, 3, 7, 1], [4096]):
            strict = self.run_input(data, chunks, mode="strict")
            self.assertEqual(strict.returncode, 0, strict.stderr)
            recovery = self.run_input(data, chunks)
            self.assertEqual(json.loads(strict.stdout)["screen"], json.loads(recovery.stdout)["screen"])

    def test_deterministic_mixed_invalid_streams(self):
        rng = random.Random(20261023)
        tokens = [(b"A", "A"), (b"\x1b[?25h", ""), (b"\x1b]52;hidden\x07", ""),
                  (b"\xff", "\ufffd"), (b"\xe1\x80Q", "\ufffdQ"), (b"\x1b(0", ""),
                  (b"\x1bPbody\x1b[2J\x1b\\", ""), (b"\x1b[999999999m", "")]
        for _ in range(20):
            selected = [rng.choice(tokens) for _ in range(10)]
            self.expect(b"".join(x[0] for x in selected), "".join(x[1] for x in selected), long=True)

    def test_arbitrary_bytes_resume_at_explicit_cancel(self):
        rng = random.Random(20261023)
        for _ in range(10):
            data = rng.randbytes(4096) + b"\x18\x1b[0m\x1b[2J\x1b[HOK"
            reference = None
            for chunks in ([1], [4096], [rng.randint(1, 31) for _ in range(19)]):
                value = json.loads(self.run_input(data, chunks).stdout)
                self.assertEqual(value["screen"]["lines"], ["OK" + " " * 78, " " * 80])
                self.assertEqual(value["screen"]["column"], 2)
                self.assertLessEqual(len(value["screen"]["history"]), 2)
                self.assertEqual(value["buffered_input_bytes_at_end"], 0)
                comparable = (value["screen"], value["diagnostics"])
                if reference is not None:
                    self.assertEqual(comparable, reference)
                reference = comparable


if __name__ == "__main__":
    unittest.main()
