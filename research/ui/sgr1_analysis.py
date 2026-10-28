"""Scalar-only same-frame SGR1 classification with independent repeated controls."""
from __future__ import annotations
import types
from research.ui import raster_raw_analysis as raw
from research.ui.sgr1_reference import TARGETS

require = raw.require
MARKERS = [[(row["label"], row["offset"], row["group"], row["sample"]) for row in target["markers"]] for target in TARGETS]
# Each clone keeps the reviewed ROI/lifetime checks and an isolated case table.
_namespace = {**vars(raw), "MARKERS": MARKERS,
              "GROUPS": [[row["group"] for row in target["markers"]] for target in TARGETS]}
_namespace["marker_bindings"] = types.FunctionType(raw.marker_bindings.__code__, _namespace)
_audit_capture = types.FunctionType(raw.audit_capture.__code__, _namespace)


def classify_frame(rois):
    grouped = {name: [] for name in ("sample", "control-normal", "control-sgr1")}
    for roi in rois:
        require(roi["group"] in grouped and type(roi["sample"]) is int and roi["sample"] in (0, 1), "Unexpected SGR1 ROI identity")
        grouped[roi["group"]].append(roi)
    for name, values in grouped.items():
        require((not values and name == "sample") or [row["sample"] for row in values] == [0, 1], "Incomplete or duplicate SGR1 controls/sample")
    result = {"unique_control": None, "reason": None, "matching_controls": [],
              "signature": "Exact ordered H/space QRgb SHA256 with shape and count; two independent repeats per group",
              "internal_bold_verified": False, "font_weight_mechanism": None,
              "unmatched_does_not_imply_reset": True}
    def relation(key):
        values = [row[key] for row in grouped["control-normal"] + grouped["control-sgr1"]]
        return "unknown" if any(value is None for value in values) else "same" if all(value == values[0] for value in values) else "different"
    result["control_color_relation"] = {"background": relation("background_rgba"), "dominant_ink": relation("dominant_ink_rgba")}
    def unknown(reason):
        return {**result, "reason": reason}
    shapes = [(row["h_rect"][2:4], row["space_rect"][2:4]) for row in rois]
    if any(shape != shapes[0] for shape in shapes):
        return unknown("shapes-differ")
    signatures = {name: [raw.raw_signature(row) for row in values] for name, values in grouped.items()}
    if any(values[0] != values[1] for name, values in signatures.items() if name != "sample"):
        return unknown("control-repeat-differs")
    if signatures["control-normal"][0] == signatures["control-sgr1"][0]:
        return unknown("controls-indistinguishable")
    if not grouped["sample"]:
        return {**result, "reason": "controls-distinct-no-unknown-sample"}
    if signatures["sample"][0] != signatures["sample"][1]:
        return unknown("sample-repeat-differs")
    result["matching_controls"] = [label for group, label in (("control-normal", "SGR0-control-output"), ("control-sgr1", "SGR1-control-output"))
                                   if signatures["sample"][0] == signatures[group][0]]
    if len(result["matching_controls"]) == 1:
        result["unique_control"] = result["matching_controls"][0]
        result["reason"] = "unique-same-frame-output-match"
        return result
    return unknown("sample-unmatched")


def audit_capture(capture):
    public = _audit_capture(capture)
    if capture["capture_valid"]:
        # All SGR1 controls occupy one actual row, including baseline/known-state stages.
        markers = capture["terminal"]["markers"]
        require(len({row["document_line"] for row in markers}) == 1, "SGR1 controls are not on one document row")
        require(len({(row["h_rect"]["y"], row["h_rect"]["height"]) for row in markers}) == 1, "SGR1 controls do not share a logical row")
        require(all(type(row["position"]) is int and 0 <= row["position"] <= 9007199254740991 for row in markers), "Invalid SGR1 text position")
        require(len({row["position"] for row in markers}) == len(markers), "Aliased SGR1 text positions")
        require(len({row["position"] - row["column"] for row in markers}) == 1, "SGR1 positions do not bind one text row")
        require(all(left["h_rect"]["x"] < right["h_rect"]["x"] for left, right in zip(markers, markers[1:])), "Aliased or reordered SGR1 glyph rectangles")
        public["sgr1_signature_comparison"] = classify_frame(public["rois"])
    else:
        public["sgr1_signature_comparison"] = None
    return public


def frame_comparisons(captures):
    return [{"phase": row["phase"], **row["sgr1_signature_comparison"]} for row in captures if row["capture_valid"]]
