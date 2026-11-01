"""One attribute per bounded five-stage 1049 output-signature observation.

The immutable raw Qt capture body is reused; only its fixed case descriptors
and output names change. Preparation and classification never launch a target.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys
import types

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from research.ui.alternate_style_reference import peer_class, require
from research.ui.raster_raw_reference import MODULE_SHA256, MANIFEST_SHA256, check_full_hashes
from research.ui.sgr1_reference import rea_identity
from research.ui.mode_state_reference import source_identity
from research.ui import raster_raw_analysis as raw

# These are requested SGR inputs, not claims that the original implements them.
ATTRIBUTES = {"underline": 4, "reverse": 7, "strikethrough": 9,
              "hidden": 8, "overline": 53, "dim": 2}
SOURCE_FILES = ("research/ui/attribute_state_reference.py", "research/ui/sgr1_reference.py",
                "research/ui/mode_state_reference.py", "research/ui/raster_raw_analysis.py",
                "research/ui/raster_selection_analysis.py",
                "research/ui/raster_raw_reference.py", "research/ui/raster_raw_reference.js",
                "research/ui/alternate_style_reference.py", "research/ui/alternate_reference.py",
                "research/ui/r1_launch.py", "research/ui/launch.py", "research/ui/qt_r1_text.js",
                "research/ui/r1_vt_fixture.py", "experiments/conpty/run.py")
AUDIT_FILES = ("research/ui/attribute_state_reference.py", "research/ui/raster_raw_analysis.py",
               "research/ui/raster_selection_analysis.py")
RESULT_FILE = "attribute-state-raster-result.json"
COMPLETE = "captured-five-attribute-state-raster-phases"
PARTIAL = "partial-attribute-state-raster-capture"


def attribute_code(attribute):
    require(type(attribute) is str and attribute in ATTRIBUTES, "Unknown fixed attribute case")
    return ATTRIBUTES[attribute]


def case_id(attribute):
    return f"sgr{attribute_code(attribute)}-{attribute}-1049-state-v1"


def pair(label, group):
    return [{"label": label, "offset": len(label) - 3 + 2 * sample,
             "group": group, "sample": sample} for sample in (0, 1)]


def targets(attribute):
    attribute_code(attribute)
    result = [{"label": f"{attribute} main controls", "markers":
               pair("N0 H H", "control-normal") + pair("S0 H H", "control-attribute")}]
    for title, sample, normal, active in (("entry", "E0 H H", "EN H H", "ES H H"),
                                          ("known alternate", "A0 H H", "AN H H", "AS H H"),
                                          ("repeat", "R0 H H", "RN H H", "RS H H"),
                                          ("return", "M1 H H", "MN H H", "MS H H")):
        result.append({"label": f"{attribute} {title} output", "markers":
                       pair(sample, "sample") + pair(normal, "control-normal") + pair(active, "control-attribute")})
    return result


def phases(attribute, rows=25, columns=91):
    code = attribute_code(attribute)
    require(10 <= rows <= 60 and 40 <= columns <= 160, "Controlled grid out of bounds")
    active = f"\x1b[{code}m".encode("ascii")
    return [
        ("main-controls-end-attribute", b"\x1b[0m\x1b[2J\x1b[H\x1b[2;3HN0 H H " + active + b"S0 H H"),
        ("1049-entry-with-controls", b"\x1b[?1049hE0 H H \x1b[0mEN H H " + active + b"ES H H"),
        ("alternate-end-attribute", b"\x1b[4;3H\x1b[0mA0 H H AN H H " + active + b"AS H H"),
        ("1049-repeat-with-controls-end-sgr0", b"\x1b[?1049hR0 H H \x1b[0mRN H H " + active + b"RS H H\x1b[0m"),
        ("1049-return-with-controls", b"\x1b[?1049l M1 H H \x1b[0mMN H H " + active + b"MS H H"),
    ]


def phase_records(attribute, rows=25, columns=91):
    return [{"phase": index, "label": label, "source_hex": content.hex(), "source_bytes": len(content),
             "source_sha256": hashlib.sha256(content).hexdigest(), "dsr_suffix_hex": "1b5b366e"}
            for index, (label, content) in enumerate(phases(attribute, rows, columns))]


def build_probe_fragment(attribute):
    source = (ROOT / "research/ui/raster_raw_reference.js").read_text(encoding="utf-8")
    start, end = "const started=Date.now(),captures=[],grabs=[],targets=[", ";\nfunction modulePathMatches"
    require(source.count(start) == source.count(end) == 1, "Protected capture descriptor boundary changed")
    left, right = source.index(start), source.index(end)
    source = source[:left] + "const started=Date.now(),captures=[],grabs=[],targets=" + json.dumps(targets(attribute), separators=(",", ":")) + source[right:]
    replacements = (("raster-raw-result.json", RESULT_FILE, 1),
                    ("raw_raster_result", "attribute_state_raster_result", 1),
                    ("raw_raster_phase", "attribute_state_raster_phase", 1),
                    ("five-stage-raw-raster-capture-v1", case_id(attribute), 1),
                    ("captured-five-raw-raster-phases", COMPLETE, 1),
                    ("partial-raw-raster-capture", PARTIAL, 3))
    for old, new, count in replacements:
        require(source.count(old) == count, "Protected capture case names changed")
        source = source.replace(old, new)
    return source


def make_peer(attribute):
    attribute_code(attribute)
    base = peer_class()
    # peer_class creates an isolated namespace; this never changes another case.
    base._serve.__globals__["phases"] = lambda rows, columns: phases(attribute, rows, columns)

    class AttributePeer(base):
        def _record(self):
            if self.state.get("frozen_grid") and "freeze_event_count" not in self.state:
                self.state["freeze_event_count"] = len(self.state.get("naws_history", []))
            super()._record()
    return AttributePeer


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_hashes(names):
    return {name: sha(ROOT / name) for name in names}


def classification_contract(attribute):
    return {"attribute": attribute, "requested_sgr": attribute_code(attribute),
            "controls": ["SGR0-control-output", f"SGR{attribute_code(attribute)}-control-output"],
            "signature": "Exact ordered H/space QRgb SHA256, shape and sample count",
            "repeats_per_group": 2, "same_frame_and_text_row_required": True,
            "ink_required_for_signature_match": False, "internal_attribute_verified": False,
            "indistinguishable_controls": "unknown", "unmatched_or_inconsistent": "unknown"}


def prepare_plan(source, out, rea, destination, attribute, execution_authorized=False):
    code = attribute_code(attribute)
    source, out, rea, destination = [Path(path).resolve() for path in (source, out, rea, destination)]
    require(type(execution_authorized) is bool, "Execution authorization must be boolean")
    require(not out.exists() and not destination.exists(), "Plan/output must be new")
    require(out.is_relative_to(ROOT / "local") and destination.is_relative_to(ROOT / "local"), "Plan/output must belong to this worktree local directory")
    plan = {"schema": "termforge-attribute-state-plan-v1", "issue": 27, "case_id": case_id(attribute),
            "attribute": attribute, "requested_sgr": code, "maximum_original_runs": 1,
            "actual_original_runs": 0, "execution_authorized": execution_authorized,
            **source_identity(root=ROOT, source_files=SOURCE_FILES),
            "source": str(source), "out": str(out), "rea": str(rea), "rea_prepared_identity": rea_identity(rea),
            "module_sha256": check_full_hashes(source), "files_manifest_sha256": MANIFEST_SHA256,
            "offline_audit_files_sha256": source_hashes(AUDIT_FILES),
            "generated_capture_sha256": hashlib.sha256(build_probe_fragment(attribute).encode("utf-8")).hexdigest(),
            "phases": phase_records(attribute), "phase_count": 5, "maximum_grabs_per_phase": 1, "maximum_grabs": 5,
            "rea_capture_seconds": 45, "cli_wait_timeout_seconds": 60, "whole_lifecycle_bound_seconds": None,
            "classification": classification_contract(attribute),
            "scope": "One requested attribute, same-frame output signatures only; no private structure, internal style, full grid, local-shell, presented or performance claim",
            "stop": "Structural/identity/empty-image/ROI/DPR/selection/caret failures stop. Equal or inconsistent controls and incomparable/unmatched samples stay unknown. No shifted ROI, additional grab or retry."}
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(plan, indent=2) + "\n")
    return plan


def _launch(source, out, rea, attribute):
    from research.ui import r1_launch
    launcher = Path(r1_launch.__file__)
    code = launcher.read_text(encoding="utf-8")
    replacements = {"from research.ui.r1_vt_fixture import Peer": "# Peer is bound to one fixed attribute case.",
                    'Path(__file__).with_name("r1_menu_wrap.js").read_text(encoding="utf-8")': "build_probe_fragment()"}
    for old, new in replacements.items():
        require(code.count(old) == 1, "Protected launcher source changed")
        code = code.replace(old, new)
    namespace = {"__file__": str(launcher), "__name__": "termforge_attribute_state_launcher",
                 "Peer": make_peer(attribute), "build_probe_fragment": lambda: build_probe_fragment(attribute)}
    exec(compile(code, str(launcher), "exec"), namespace)
    namespace["run"](source, out, rea, 45, False, "menu-wrap")


def run_plan(path):
    path = Path(path).resolve()
    plan = json.loads(path.read_text(encoding="utf-8"))
    attribute = plan.get("attribute")
    require(plan.get("schema") == "termforge-attribute-state-plan-v1" and plan.get("issue") == 27 and
            plan.get("case_id") == case_id(attribute) and plan.get("requested_sgr") == attribute_code(attribute) and
            plan.get("maximum_original_runs") == 1 and plan.get("actual_original_runs") == 0, "Unexpected attribute plan")
    require(plan.get("execution_authorized") is True, "Offline proposal only; target execution is not enabled")
    require(all(plan.get(key) == value for key, value in source_identity(root=ROOT, source_files=SOURCE_FILES).items()), "Source identity changed after plan")
    require(plan["module_sha256"] == MODULE_SHA256 and plan["files_manifest_sha256"] == MANIFEST_SHA256, "Plan module/package identity differs")
    require(plan["source_files_sha256"] == source_hashes(SOURCE_FILES) and plan["offline_audit_files_sha256"] == source_hashes(AUDIT_FILES), "Source inventory or bytes changed after plan")
    require(plan["generated_capture_sha256"] == hashlib.sha256(build_probe_fragment(attribute).encode("utf-8")).hexdigest(), "Generated capture differs")
    require(plan["phases"] == phase_records(attribute) and plan["classification"] == classification_contract(attribute), "Fixed source or classification differs")
    require((plan["phase_count"], plan["maximum_grabs_per_phase"], plan["maximum_grabs"], plan["rea_capture_seconds"], plan["cli_wait_timeout_seconds"], plan["whole_lifecycle_bound_seconds"]) == (5, 1, 5, 45, 60, None), "Plan capture bounds differ")
    source, out, rea = [Path(plan[key]).resolve() for key in ("source", "out", "rea")]
    require(path.is_relative_to(ROOT / "local") and out.is_relative_to(ROOT / "local") and not out.exists(), "This run plan/output is not new/local")
    require(plan["rea_prepared_identity"] == rea_identity(rea), "Prepared REA entry changed")
    check_full_hashes(source)
    with path.with_suffix(".attempted").open("x", encoding="utf-8") as marker:
        marker.write("One authorized original launch attempt; never retry this plan.\n")
    _launch(source, out, rea, attribute)
    require(all(plan.get(key) == value for key, value in source_identity(root=ROOT, source_files=SOURCE_FILES).items()), "Source identity changed during run")


def classify_frame(attribute, rois):
    contract = classification_contract(attribute)
    grouped = {name: [] for name in ("sample", "control-normal", "control-attribute")}
    for roi in rois:
        require(roi["group"] in grouped and type(roi["sample"]) is int and roi["sample"] in (0, 1), "Unexpected attribute ROI identity")
        grouped[roi["group"]].append(roi)
    for name, values in grouped.items():
        require((not values and name == "sample") or [row["sample"] for row in values] == [0, 1], "Incomplete or duplicate attribute controls/sample")
    result = {**contract, "unique_control": None, "matching_controls": [], "reason": None,
              "unmatched_does_not_imply_reset": True, "full_style_verified": False}
    def unknown(reason):
        return {**result, "reason": reason}
    shapes = [(row["h_rect"][2:4], row["space_rect"][2:4]) for row in rois]
    if any(shape != shapes[0] for shape in shapes):
        return unknown("shapes-differ")
    signatures = {name: [raw.raw_signature(row) for row in values] for name, values in grouped.items()}
    if any(values[0] != values[1] for name, values in signatures.items() if name != "sample"):
        return unknown("control-repeat-differs")
    if signatures["control-normal"][0] == signatures["control-attribute"][0]:
        return unknown("controls-indistinguishable")
    if not grouped["sample"]:
        return {**result, "reason": "controls-distinct-no-unknown-sample"}
    if signatures["sample"][0] != signatures["sample"][1]:
        return unknown("sample-repeat-differs")
    result["matching_controls"] = [label for group, label in zip(("control-normal", "control-attribute"), contract["controls"])
                                   if signatures["sample"][0] == signatures[group][0]]
    if len(result["matching_controls"]) == 1:
        return {**result, "unique_control": result["matching_controls"][0], "reason": "unique-same-frame-output-match"}
    return unknown("sample-unmatched")


def audit_capture(attribute, capture):
    descriptors = targets(attribute)
    namespace = {**vars(raw), "MARKERS": [[(row["label"], row["offset"], row["group"], row["sample"]) for row in item["markers"]] for item in descriptors],
                 "GROUPS": [[row["group"] for row in item["markers"]] for item in descriptors]}
    namespace["marker_bindings"] = types.FunctionType(raw.marker_bindings.__code__, namespace)
    public = types.FunctionType(raw.audit_capture.__code__, namespace)(capture)
    if capture["capture_valid"]:
        markers = capture["terminal"]["markers"]
        require(len({row["document_line"] for row in markers}) == 1, "Attribute controls are not on one document row")
        require(len({(row["h_rect"]["y"], row["h_rect"]["height"]) for row in markers}) == 1, "Attribute controls do not share a logical row")
        require(all(type(row["position"]) is int and 0 <= row["position"] <= 9007199254740991 for row in markers), "Invalid attribute text position")
        require(len({row["position"] for row in markers}) == len(markers) and
                len({row["position"] - row["column"] for row in markers}) == 1, "Aliased attribute positions or text rows")
        require(all(left["h_rect"]["x"] < right["h_rect"]["x"] for left, right in zip(markers, markers[1:])), "Aliased or reordered attribute glyph rectangles")
        public["attribute_signature_comparison"] = classify_frame(attribute, public["rois"])
    else:
        public["attribute_signature_comparison"] = None
    return public


def frame_comparisons(captures):
    return [{"phase": row["phase"], **row["attribute_signature_comparison"]} for row in captures if row["capture_valid"]]


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--prepare", action="store_true")
    parser.add_argument("--attribute", choices=list(ATTRIBUTES))
    parser.add_argument("--authorize-execution", action="store_true")
    for name in ("source", "out", "rea"):
        parser.add_argument("--" + name, type=Path)
    args = parser.parse_args()
    if args.prepare:
        require(args.source and args.out and args.rea and args.attribute, "Source/output/REA/attribute required")
        prepare_plan(args.source, args.out, args.rea, args.plan, args.attribute, args.authorize_execution)
        print(json.dumps({"prepared": True, "case_id": case_id(args.attribute), "phase_count": 5, "maximum_original_runs": 1}))
    else:
        require(args.attribute is None and not args.authorize_execution, "Run only the attribute and authorization frozen in the plan")
        run_plan(args.plan)
