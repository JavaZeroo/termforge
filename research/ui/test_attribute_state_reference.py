"""Synthetic attribute inputs, output classifications and non-execution gates."""
from __future__ import annotations
import copy
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock

from research.ui import attribute_state_reference as reference
from research.ui import mode_state_reference
from research.ui.raster_raw_analysis import classify_pixels

ROOT = Path(__file__).resolve().parents[2]
BG, FG = 0xff101010, 0xffeeeeee
NORMAL, DECORATED = [BG, FG, BG, FG], [FG, FG, BG, FG]


def roi(group, sample, pixels, shape=(2, 2)):
    return {"group": group, "sample": sample, "h_rect": [0, 0, *shape], "space_rect": [0, 0, *shape],
            **classify_pixels(pixels, [BG] * len(pixels))}


def frame(sample=DECORATED, control=DECORATED):
    return ([roi("sample", n, sample) for n in (0, 1)] +
            [roi("control-normal", n, NORMAL) for n in (0, 1)] +
            [roi("control-attribute", n, control) for n in (0, 1)])


class AttributeClassificationTests(unittest.TestCase):
    def test_each_fixed_attribute_uses_exact_same_frame_signatures(self):
        for attribute, code in (("underline", 4), ("reverse", 7), ("strikethrough", 9),
                                ("hidden", 8), ("overline", 53), ("dim", 2)):
            with self.subTest(attribute=attribute):
                result = reference.classify_frame(attribute, frame())
                self.assertEqual(result["unique_control"], f"SGR{code}-control-output")
                self.assertFalse(result["internal_attribute_verified"])
                self.assertFalse(result["full_style_verified"])
                self.assertEqual(reference.classify_frame(attribute, frame(NORMAL))["unique_control"], "SGR0-control-output")

    def test_equal_controls_and_equal_histogram_different_order_stay_unknown(self):
        self.assertEqual(reference.classify_frame("dim", frame(NORMAL, NORMAL))["reason"], "controls-indistinguishable")
        values = frame()
        values[3] = roi("control-normal", 1, [FG, BG, FG, BG])
        self.assertEqual(values[2]["h"]["histogram"], values[3]["h"]["histogram"])
        self.assertEqual(reference.classify_frame("underline", values)["reason"], "control-repeat-differs")

    def test_hidden_output_can_match_a_distinct_control_without_ink(self):
        result = reference.classify_frame("hidden", frame([BG] * 4, [BG] * 4))
        self.assertEqual(result["unique_control"], "SGR8-control-output")
        self.assertFalse(result["ink_required_for_signature_match"])
        self.assertFalse(result["internal_attribute_verified"])

    def test_inconsistent_or_unmatched_sample_and_shape_remain_unknown(self):
        values = frame(); values[1] = roi("sample", 1, NORMAL)
        self.assertEqual(reference.classify_frame("reverse", values)["reason"], "sample-repeat-differs")
        self.assertEqual(reference.classify_frame("reverse", frame([FG, BG, BG, FG]))["reason"], "sample-unmatched")
        values = frame(); values[-1] = roi("control-attribute", 1, DECORATED, (1, 4))
        self.assertEqual(reference.classify_frame("overline", values)["reason"], "shapes-differ")

    def test_missing_duplicate_or_unknown_case_is_rejected(self):
        for values in (frame()[:-1], frame() + [frame()[-1]], [*frame()[:-1], frame()[-2]]):
            with self.subTest(values=values), self.assertRaises(ValueError):
                reference.classify_frame("underline", values)
        with self.assertRaises(ValueError):
            reference.classify_frame("bold", frame())

    def test_literal_underlined_sources_and_unchanged_capture_body(self):
        expected = [
            b"\x1b[0m\x1b[2J\x1b[H\x1b[2;3HN0 H H \x1b[4mS0 H H",
            b"\x1b[?1049hE0 H H \x1b[0mEN H H \x1b[4mES H H",
            b"\x1b[4;3H\x1b[0mA0 H H AN H H \x1b[4mAS H H",
            b"\x1b[?1049hR0 H H \x1b[0mRN H H \x1b[4mRS H H\x1b[0m",
            b"\x1b[?1049l M1 H H \x1b[0mMN H H \x1b[4mMS H H",
        ]
        self.assertEqual([value for _, value in reference.phases("underline")], expected)
        source_path = ROOT / "research/ui/raster_raw_reference.js"
        before = source_path.read_bytes()
        for attribute in reference.ATTRIBUTES:
            script = reference.build_probe_fragment(attribute)
            self.assertEqual(len(reference.phase_records(attribute)), 5)
            self.assertEqual([len(row["markers"]) for row in reference.targets(attribute)], [4, 6, 6, 6, 6])
            self.assertIn("must(!grabs.includes(index),'extra-grab-for-phase')", script)
            self.assertIn("const pixmap=Memory.alloc(32),image=Memory.alloc(32)", script)
            # Native object ABI/capture/ROI handling remains the exact old body.
            native_start, native_end = "function modulePathMatches", "function finish("
            old_body = before.decode("utf-8").split(native_start, 1)[1].split(native_end, 1)[0]
            new_body = script.split(native_start, 1)[1].split(native_end, 1)[0]
            self.assertEqual(new_body.replace("attribute_state_raster_phase", "raw_raster_phase"), old_body)
        self.assertEqual(source_path.read_bytes(), before)

    def test_peer_case_namespaces_do_not_overwrite_each_other(self):
        from research.ui import sgr1_reference
        old = sgr1_reference.phases(25, 91)
        underline = reference.make_peer("underline")
        dim = reference.make_peer("dim")
        first = underline._serve.__globals__["phases"](25, 91)[0][1]
        second = dim._serve.__globals__["phases"](25, 91)[0][1]
        self.assertTrue(first.endswith(b"\x1b[4mS0 H H"))
        self.assertTrue(second.endswith(b"\x1b[2mS0 H H"))
        self.assertEqual(sgr1_reference.phases(25, 91), old)

    def test_complete_synthetic_capture_uses_bound_markers_and_scalar_only_output(self):
        from research.ui.test_sgr1_reference import capture_fixture
        with tempfile.TemporaryDirectory() as temporary:
            _, _, data = capture_fixture(Path(temporary))
            captures = data["sgr1-raster-result.json"]["captures"]
            for attribute in reference.ATTRIBUTES:
                descriptors = reference.targets(attribute)
                for index, original in enumerate(captures):
                    capture = copy.deepcopy(original)
                    capture["label"] = descriptors[index]["label"]
                    labels = list(dict.fromkeys(row["label"] for row in descriptors[index]["markers"]))
                    capture["terminal"]["lines"] = [" ".join(labels)]
                    for ordinal, expected in enumerate(descriptors[index]["markers"]):
                        marker = capture["terminal"]["markers"][ordinal]
                        marker.update(label=expected["label"], group=expected["group"])
                        capture["rois"][ordinal]["group"] = expected["group"]
                    public = reference.audit_capture(attribute, capture)
                    result = public["attribute_signature_comparison"]
                    self.assertFalse(result["internal_attribute_verified"])
                    self.assertNotIn("h_pixels", json.dumps(public))
                    self.assertNotIn("space_pixels", json.dumps(public))
                    if index == 4:
                        self.assertEqual(result["unique_control"], f"SGR{reference.ATTRIBUTES[attribute]}-control-output")
                    invalid = copy.deepcopy(capture)
                    invalid["terminal"]["markers"][1]["position"] = invalid["terminal"]["markers"][0]["position"]
                    with self.assertRaises(ValueError):
                        reference.audit_capture(attribute, invalid)


class AttributePlanTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="attribute-tests-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for name in reference.SOURCE_FILES:
            destination = self.root / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, destination)
        hashes = {name: hashlib.sha256((self.root / name).read_bytes()).hexdigest() for name in reference.SOURCE_FILES}
        (self.root / "reference-source.json").write_text(json.dumps({
            "schema": "termforge-mode-reference-source-v1",
            "source_base_commit": "f72c5c4f68dd7eb579ad1e523762333b135aa074",
            "source_files_sha256": hashes}), encoding="utf-8")
        self.plan_path = self.root / "local/plan.json"
        self.out = self.root / "local/run"
        for patcher in (
            mock.patch.object(reference, "ROOT", self.root),
            mock.patch.object(reference, "check_full_hashes", return_value=reference.MODULE_SHA256),
            mock.patch.object(reference, "rea_identity", return_value={"version": "6.7.0", "shim_sha256": "a" * 64,
                              "package_json_sha256": "b" * 64, "entry_sha256": "c" * 64}),
            mock.patch.object(mode_state_reference.subprocess, "check_output", side_effect=AssertionError("archive must not call Git")),
        ):
            patcher.start(); self.addCleanup(patcher.stop)
        self.launch = mock.patch.object(reference, "_launch").start()
        self.addCleanup(mock.patch.stopall)

    def prepare(self, attribute="underline", authorized=False):
        return reference.prepare_plan(self.root / "original", self.out, self.root / "rea.cmd",
                                      self.plan_path, attribute, authorized)

    def test_archive_plan_binds_one_case_and_preparation_never_launches(self):
        plan = self.prepare("hidden")
        self.assertEqual(plan["source_identity"]["kind"], "source-archive")
        self.assertEqual(plan["requested_sgr"], 8)
        self.assertEqual(plan["maximum_original_runs"], 1)
        self.assertEqual((plan["phase_count"], plan["maximum_grabs"]), (5, 5))
        self.launch.assert_not_called()
        self.assertFalse(self.out.exists())
        with self.assertRaises(ValueError):
            reference.run_plan(self.plan_path)
        self.assertFalse(self.plan_path.with_suffix(".attempted").exists())

    def test_authorized_plan_consumes_exactly_one_attempt(self):
        self.prepare("reverse", True)
        reference.run_plan(self.plan_path)
        self.launch.assert_called_once_with(self.root / "original", self.out, self.root / "rea.cmd", "reverse")
        self.assertTrue(self.plan_path.with_suffix(".attempted").exists())
        with self.assertRaises(FileExistsError):
            reference.run_plan(self.plan_path)
        self.assertEqual(self.launch.call_count, 1)

    def test_changed_case_source_or_bounds_cannot_launch(self):
        plan = self.prepare(authorized=True)
        mutations = (("attribute", "dim"), ("requested_sgr", 7), ("maximum_grabs", 6),
                     ("generated_capture_sha256", "0" * 64), ("source_identity", {"kind": "git-checkout"}),
                     ("source_files_sha256", {}), ("phases", []))
        for key, value in mutations:
            changed = {**plan, key: value}
            self.plan_path.write_text(json.dumps(changed), encoding="utf-8")
            with self.subTest(key=key), self.assertRaises(ValueError):
                reference.run_plan(self.plan_path)
            self.launch.assert_not_called()
            self.assertFalse(self.plan_path.with_suffix(".attempted").exists())

    def test_archive_byte_change_is_rejected_before_attempt(self):
        self.prepare(authorized=True)
        with (self.root / "research/ui/raster_raw_reference.js").open("a", encoding="utf-8") as stream:
            stream.write("\n// changed after preparation\n")
        with self.assertRaises(ValueError):
            reference.run_plan(self.plan_path)
        self.launch.assert_not_called()
        self.assertFalse(self.plan_path.with_suffix(".attempted").exists())

    def test_prepare_never_overwrites_an_existing_plan(self):
        self.prepare()
        saved = self.plan_path.read_bytes()
        with self.assertRaises(ValueError):
            self.prepare("dim")
        self.assertEqual(self.plan_path.read_bytes(), saved)


if __name__ == "__main__":
    unittest.main()
