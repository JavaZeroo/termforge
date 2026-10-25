import copy
import json
import os
from pathlib import Path
import tempfile
import unittest

from experiments.build_paths import binary
from experiments.shell.run import execute
from experiments.shell.unicode_checks import validate_candidate_unicode
from experiments.shell.alternate_checks import STAGES, validate_candidate_alternate
from experiments.shell.scroll_checks import STAGES as SCROLL_STAGES, validate_candidate_scroll
from experiments.shell.workflow_checks import validate_console_window


class CandidateTuiTests(unittest.TestCase):
    def setUp(self):
        root = Path(os.environ.get("TERMFORGE_TEST_ARTIFACTS",
                                  Path(__file__).resolve().parents[2] / "build/test-artifacts"))
        base = root / "candidate-shell"
        base.mkdir(parents=True, exist_ok=True)
        self.out = Path(tempfile.mkdtemp(prefix="run-", dir=base))

    def test_escape_after_resize(self):
        for repeat in range(2):
            out = self.out / f"candidate-escape-{repeat}"
            execute(binary("termforge_shell"), "candidate-input-escape", out, timeout=20)
            self.assertEqual((out / "candidate-escape.bin").read_bytes(), b"\x1b")

    def test_real_fullscreen_editor_and_cursor_reply(self):
        executable = binary("termforge_shell")
        for repeat in range(2):
            out = self.out / f"candidate-tui-{repeat}"
            result = execute(executable, "candidate-tui", out, timeout=40)["result"]
            self.assertTrue(result["candidate_tui_start"])
            self.assertTrue(result["candidate_tui"])
            self.assertTrue(result["candidate_replies"])
            self.assertTrue(result["candidate_resize"])
            self.assertIn("TF_CANDIDATE_TUI", (out / "candidate-tui.txt").read_text(encoding="utf-8"))
            self.assertEqual((out / "candidate-vim.vim").read_text(encoding="utf-8"),
                             "set noswapfile\n"
                             "autocmd VimEnter * call writefile(['READY'], 'candidate-startup-log.txt')\n"
                             "autocmd InsertEnter * call writefile(['INSERT'], 'candidate-insert-log.txt', 'a')\n"
                             "autocmd InsertCharPre * call writefile([v:char], 'candidate-insert-log.txt', 'a')\n"
                             "autocmd InsertLeave * call writefile(['LEAVE'], 'candidate-insert-log.txt', 'a')\n")
            script = (out / "candidate-input.bin").read_bytes()
            self.assertTrue(script.startswith(b"iTF_CANDIDATE_TUI"))
            self.assertTrue(script.endswith(b":wq\r"))
            output = (out / "candidate-output.bin").read_bytes()
            self.assertTrue(output)
            self.assertEqual((out / "candidate-startup-log.txt").read_bytes(), b"READY\n")
            self.assertEqual((out / "candidate-resize-log.txt").read_bytes(), b"60x12")
            self.assertEqual((out / "candidate-insert-log.txt").read_text(encoding="utf-8"),
                             "INSERT\n" + "\n".join("TF_CANDIDATE_TUI") + "\nLEAVE\nINSERT\nLEAVE\n")
            replies = (out / "candidate-replies.bin").read_bytes()
            self.assertRegex(replies, rb"^\x1b\[[0-9]+;[0-9]+R$", replies)
            self.assertIsNone(result["presented_done"])

    def test_real_alternate_screen_restoration(self):
        for repeat in range(2):
            out = self.out / f"candidate-alt-{repeat}"
            record = execute(binary("termforge_shell"), "candidate-alt", out, timeout=20)
            summary = record["alternate_output"]
            self.assertTrue(summary["history_isolated"])
            self.assertFalse(summary["scroll_region_verified"])
            self.assertFalse(summary["full_r2_contract_verified"])
            self.assertIsNone(summary["presented_done"])
            data = json.loads((out / "candidate-alt.json").read_text(encoding="utf-8"))
            consoles = {stage: validate_console_window(out, expected_role=role, expected_generation=1, prefix=prefix)
                        for stage, (role, prefix) in STAGES.items()}
            source = (out / "candidate-alt.source.bin").read_bytes()
            transport = (out / "candidate-alt.transport.bin").read_bytes()
            for corruption in ("history", "style", "cursor", "source", "frame", "missing-stage"):
                changed = copy.deepcopy(data)
                changed_consoles = consoles
                if corruption == "history":
                    changed["document_lines"].append("ALT_ONE")
                elif corruption == "style":
                    next(cell for cell in changed["styled_cells"] if cell["fg"] == 2)["fg"] = 1
                elif corruption == "cursor":
                    changed["cursor_row"] = (changed["cursor_row"] + 1) % changed["rows"]
                elif corruption == "frame":
                    changed["frame_matches_full"] = False
                elif corruption == "missing-stage":
                    changed_consoles = dict(consoles)
                    del changed_consoles["restored1"]
                with self.subTest(repeat=repeat, corruption=corruption):
                    with self.assertRaises(ValueError):
                        validate_candidate_alternate(changed, changed_consoles,
                                                    b"" if corruption == "source" else source, transport)

    def test_real_scroll_region(self):
        for repeat in range(2):
            out = self.out / f"candidate-scroll-{repeat}"
            record = execute(binary("termforge_shell"), "candidate-scroll", out, timeout=20)
            summary = record["scroll_output"]
            self.assertTrue(summary["scroll_region_verified"])
            self.assertTrue(summary["outside_preserved"])
            self.assertFalse(summary["origin_and_wrap_modes_verified"])
            self.assertFalse(summary["resize_cross_verified"])
            self.assertFalse(summary["full_r2_contract_verified"])
            self.assertIsNone(summary["presented_done"])
            data = json.loads((out / "candidate-scroll.json").read_text(encoding="utf-8"))
            consoles = {stage: validate_console_window(out, expected_role=role, expected_generation=1, prefix=prefix)
                        for stage, (role, prefix) in SCROLL_STAGES.items()}
            source = (out / "candidate-scroll.source.bin").read_bytes()
            transport = (out / "candidate-scroll.transport.bin").read_bytes()
            for corruption in ("outside", "region", "history", "source", "transport", "cursor", "frame", "missing-stage"):
                changed = copy.deepcopy(data)
                changed_consoles = consoles
                if corruption == "outside":
                    changed["visible_lines"][0] = "REGION_A"
                elif corruption == "region":
                    changed["visible_lines"][1], changed["visible_lines"][2] = \
                        changed["visible_lines"][2], changed["visible_lines"][1]
                elif corruption == "history":
                    changed["document_lines"].append("REGION_A")
                elif corruption == "cursor":
                    changed["cursor_row"] = (changed["cursor_row"] + 1) % changed["rows"]
                elif corruption == "frame":
                    changed["frame_matches_full"] = False
                elif corruption == "missing-stage":
                    changed_consoles = dict(consoles)
                    del changed_consoles["region"]
                with self.subTest(repeat=repeat, corruption=corruption):
                    with self.assertRaises(ValueError):
                        validate_candidate_scroll(changed, changed_consoles,
                                                 b"" if corruption == "source" else source,
                                                 transport.replace(b"REGION_C", b"REGION_X") if corruption == "transport" else transport)

    def test_real_unicode_scalar_preservation_and_glyphs(self):
        for repeat in range(2):
            out = self.out / f"candidate-unicode-{repeat}"
            record = execute(binary("termforge_shell"), "candidate-unicode", out, timeout=20)
            summary = record["unicode_output"]
            self.assertEqual([sample["label"] for sample in summary["samples"]],
                             ["TF_U_DECOMPOSE", "TF_U_EMOJI", "TF_U_ZWJ", "TF_U_KEYCAP"])
            self.assertFalse(summary["complete_unicode_ime_verified"])
            self.assertFalse(summary["whole_cluster_width_policy_verified"])
            self.assertIsNone(summary["presented_done"])
            data = json.loads((out / "candidate-unicode.json").read_text(encoding="utf-8"))
            console = validate_console_window(out, expected_role="unicode", expected_generation=1,
                                              prefix="candidate-unicode")
            source = (out / "candidate-unicode.source.bin").read_bytes()
            transport = (out / "candidate-unicode.transport.bin").read_bytes()
            validate_candidate_unicode(data, console, source, transport)
            for corruption in ("missing", "width", "cluster", "source", "frame"):
                changed = copy.deepcopy(data)
                if corruption == "missing":
                    next(cell for line in changed["lines"] for cell in line["cells"]
                         if cell["text"] in ("👩\u200d", "👩\u200d💻"))["missing"] = 1
                elif corruption == "width":
                    next(cell for line in changed["lines"] for cell in line["cells"]
                         if cell["text"] in ("👩\u200d", "👩\u200d💻"))["width"] = 3
                elif corruption == "cluster":
                    next(cell for line in changed["lines"] for cell in line["cells"]
                         if cell["text"] in ("👩\u200d", "👩\u200d💻"))["text"] = "👩"
                elif corruption == "frame":
                    changed["frame_matches_full"] = False
                with self.subTest(repeat=repeat, corruption=corruption):
                    with self.assertRaises(ValueError):
                        validate_candidate_unicode(changed, console, b"" if corruption == "source" else source,
                                                   transport)


if __name__ == "__main__":
    unittest.main()
