"""Strict publication of finite synthetic Unicode text and independent API units."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path, PureWindowsPath
import re
import sys
from types import FunctionType, SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from research.ui import export_mode_state_reference as common
from research.ui import export_resize_state_reference as resize_export
from research.ui import resize_unicode_reference as probe

require, sha, digest, integer = common.require, common.sha, common.digest, common.integer
MARKERS = probe.MARKERS
EXPORT_SOURCES = ("research/ui/export_resize_unicode_reference.py", "research/ui/export_resize_state_reference.py",
                  "research/ui/export_mode_state_reference.py")


def raw_names(case):
    return ("evidence.json", "run.json", "probe.js", "peer.json", probe.RESULT_FILE, "files.json",
            "mechanics-check.json", "stderr.txt", "profile-check.json", "cleanup.json", "fixture-ready",
            *(f"ack-{n}" for n in range(len(probe.phases(case)))),
            *(f"resize-{kind}-{n}" for n in probe.resize_phases(case) for kind in ("request", "ready")))


def read(path):
    require(path.stat().st_size <= 4*1024*1024, "Recording exceeds byte bound")
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, "Duplicate recording field")
            result[key] = value
        return result
    def nonfinite(_):
        raise ValueError("Nonfinite JSON value")
    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique, parse_constant=nonfinite)


def adapt(source, changes):
    for old, new in changes.items():
        require(source.count(old) == 1, "Protected offline audit adapter changed: "+old)
        source = source.replace(old, new)
    return source


def extract(path, start, end):
    source = path.read_text(encoding="utf-8")
    require(source.count(start) == source.count(end) == 1, "Protected offline audit function boundary changed")
    return start+source.split(start, 1)[1].split(end, 1)[0]


def expected_probe(plan, run, source_root):
    integer(run["pid"], 2**32-1, 1); integer(run["tid"], 2**32-1, 1)
    require(type(run["window_station"]) is str and re.fullmatch(r"TermForgeR1-[0-9]+", run["window_station"]),
            "Owned station identity differs")
    template = (source_root / "research/ui/qt_r1_text.js").read_text(encoding="utf-8").split("// Scenario driver", 1)[0]
    driver = (source_root / "research/ui/resize_unicode_reference.js").read_text(encoding="utf-8")
    require(driver.count("__RESIZE_UNICODE_CASE_JSON__") == 1, "Case template anchor differs")
    template += driver.replace("__RESIZE_UNICODE_CASE_JSON__", json.dumps(probe.controller_contract(plan["case_id"])))
    require(hashlib.sha256(template.encode()).hexdigest() == run["template_sha256"], "Probe template differs")
    generated = template.replace("__UI_THREAD_ID__", str(run["tid"])).replace("__OWNED_STATION__", run["window_station"])
    generated = generated.replace("__RUN_DIRECTORY_JSON__", json.dumps(PureWindowsPath(plan["out"]).as_posix()))
    generated = generated.replace("__HOSTED_CI__", "false").replace("__SCENARIO_TIMEOUT_MS__", "43000")
    return generated.encode().replace(b"\n", b"\r\n")


# The immutable existing helpers are adapted in fresh namespaces. Each textual
# boundary is checked, and all three offline source files are identified below.
_source = extract(Path(common.__file__), "def provenance_facts(", "\n\ndef export(")
_source = adapt(_source, {
    "One original launch attempt; retain after failure and never retry this plan.": probe.ATTEMPT.rstrip("\n"),
    "probe.source_identity(root=source_root, source_files=probe.SOURCE_FILES)":
        "probe.common.source_identity(root=source_root, source_files=probe.SOURCE_FILES)",
})
_PROVENANCE = {**vars(common), "ROOT": ROOT, "probe": probe, "read": read}
exec(compile(_source, str(Path(common.__file__)), "exec"), _PROVENANCE)


def provenance_facts(directory, plan_path, plan, source_root, reference_source):
    function = FunctionType(_PROVENANCE["provenance_facts"].__code__, {**_PROVENANCE, "RAW_NAMES": raw_names(plan["case_id"])})
    return function(directory, plan_path, plan, source_root, reference_source)


_marker_projection = FunctionType(common.marker_projection.__code__, {**vars(common), "MARKERS": MARKERS})


def marker_projection(terminal):
    result = []
    for marker, raw in zip(_marker_projection(terminal), terminal["markers"]):
        line = terminal["lines"][marker["document_line"]]
        # The finite source alphabet is entirely BMP. Python scalar offsets and
        # JS UTF-16 offsets therefore coincide here, not for arbitrary Unicode.
        require(len(line.encode("utf-16-le"))//2 == len(line), "Unexpected non-BMP text")
        start = integer(raw["line_start_api"], 262144)
        require(marker["position"] == start+marker["column"], "Captured API-call arithmetic differs")
        rect = marker["rect"]
        if rect is not None:
            require(0 <= rect["width"] <= 1360 and 0 <= rect["height"] <= 749 and
                    -1360 <= rect["x"] <= 2720 and -200*749 <= rect["y"] <= 2*749,
                    "Marker rectangle exceeds fixed window/document bounds")
        result.append({"label": marker["label"], "document_line": marker["document_line"],
                       "line_text_offset_utf16": marker["column"], "line_text_offset_scalar": marker["column"],
                       "line_text_prefix_utf8_bytes": len(line[:marker["column"]].encode("utf-8")),
                       "line_start_api_value": start, "requested_position_api_value": marker["position"], "rect": rect})
    return result
geometry = FunctionType(resize_export.geometry.__code__, {**vars(resize_export), "probe": probe})
_source = extract(Path(resize_export.__file__), "def capture_facts(", "\n\ndef export(")
_source = adapt(_source, {
    "probe.audit_mechanics(directory)": 'probe.audit_mechanics(directory, plan["case_id"])',
    '"resize-state-result.json"': 'probe.RESULT_FILE',
    "6 <= len(messages) <= 69": 'len(probe.phases(plan["case_id"]))+3 <= len(messages) <= len(probe.phases(plan["case_id"]))+66',
    '"scenario": "resize-state-v1"': '"scenario": "resize-unicode-v1", "case_id": plan["case_id"]',
    "count = len(payloads)-5": 'count = len(payloads)-len(probe.phases(plan["case_id"]))-2',
    '"resize_state_phase"': '"resize_unicode_phase"',
    '"resize_state_result"': '"resize_unicode_result"',
    "for phase in (1, 2):": 'for phase in probe.resize_phases(plan["case_id"]):',
    "probe.phase_records(grids[0][1], grids[0][0])": 'probe.phase_records(plan["case_id"], grids[0][1], grids[0][0])',
    "synthetic_lines(grids[0][0])": 'probe.synthetic_lines(plan["case_id"], source["phase"])',
    '"dsr_request_bytes": 12': '"dsr_request_bytes": 4*len(probe.phases(plan["case_id"]))',
    'enumerate(peer["resize_boundaries"], 1)': 'zip(probe.RESIZE_PHASES, peer["resize_boundaries"])',
    '"phase_messages": 3': '"phase_messages": len(probe.phases(plan["case_id"]))',
})
_CAPTURE = {**vars(resize_export), "ROOT": ROOT, "probe": probe, "read": read,
            "expected_probe": expected_probe, "geometry": geometry, "marker_projection": marker_projection}
exec(compile(_source, str(Path(resize_export.__file__)), "exec"), _CAPTURE)


def capture_facts(directory, plan, source_root=ROOT):
    case = probe.validate_case(plan["case_id"])
    # A fresh binding supplies each immutable case's geometry. The imported
    # runtime and frozen exporter namespaces are never mutated.
    case_probe = SimpleNamespace(**{**vars(probe), "WINDOWS": probe.windows(case), "RESIZE_PHASES": probe.resize_phases(case)})
    function = FunctionType(_CAPTURE["capture_facts"].__code__, {**_CAPTURE, "probe": case_probe})
    result = function(directory, plan, source_root)
    raw = read(directory / probe.RESULT_FILE)
    roles = [label for label, _ in probe.phases(case)]
    for index, (phase, observed) in enumerate(zip(result["phases"], raw["observations"])):
        phase["stage_role"] = roles[index]
        lines = observed["terminal"]["lines"]
        phase["document_projection"]["synthetic_lines_sha256"] = hashlib.sha256(json.dumps(lines, separators=(",", ":")).encode()).hexdigest()
        # Validate exact source derivation before publishing any reconstructible
        # text. The unrestricted original recording remains private.
        probe.validate_document(case, index, observed["terminal"])
        catalog = sorted(probe.synthetic_lines(case, index))
        positions = {line: offset for offset, line in enumerate(catalog)}
        phase["document_projection"]["synthetic_text"] = {
            "encoding": "UTF-8", "catalog_source": "resize_unicode_reference.synthetic_lines(case_id, phase); sorted Unicode scalar order",
            "catalog_count": len(catalog),
            "catalog_sha256": hashlib.sha256(json.dumps(catalog, separators=(",", ":")).encode()).hexdigest(),
            "lines": [{"catalog_index": positions[line], "utf8_hex": line.encode("utf-8").hex(),
                       "utf8_bytes": len(line.encode("utf-8")), "unicode_scalars": len(line),
                       "utf16_code_units": len(line.encode("utf-16-le"))//2} for line in lines]}
    result["stateful_observation_count"] = len(roles)
    result["before_next_input_stage"] = 1
    result["independent_dsr_bounds"] = {"row": [1, 200], "column": [1, 500]}
    result["text_units"] = {"source": "UTF-8 bytes", "document_line": "original public API index",
                            "marker_text_offsets": "JS UTF-16 code units; scalar count coincides only for this BMP source alphabet",
                            "position_api": "recorded lineStart API value plus JS UTF-16 offset, passed unchanged to visibleRectFromPosition",
                            "caret_api": "original API scalar values; numbering policy not inferred",
                            "rectangles": "actual public API pixel rectangles; overlap and non-ASCII widths are preserved",
                            "protocol_cursor": "DSR values kept independent of text offsets and pixel rectangles",
                            "cpp_cell_column_mapping": None, "document_position_unit_classification": None,
                            "normalization_catalog_unicode_version": probe.unicodedata.unidata_version}
    return result


def export(directory, plan_path, destination, expected_plan_sha256, source_root=ROOT, reference_source=None):
    directory, plan_path, destination, source_root = [Path(path).resolve() for path in (directory, plan_path, destination, source_root)]
    require(not destination.exists(), "Export destination must be new")
    require(sha(plan_path) == digest(expected_plan_sha256), "Authorized plan bytes differ")
    plan = read(plan_path)
    require(all(type(plan.get(k)) is type(v) and plan[k] == v for k, v in probe.fixed_contract(plan.get("case_id")).items()),
            "Fixed resize-unicode contract differs")
    require(plan.get("execution_authorized") is True, "Plan did not authorize execution")
    facts = capture_facts(directory, plan, source_root)
    reference_source = Path(plan["source"]) if reference_source is None else Path(reference_source)
    provenance = provenance_facts(directory, plan_path, plan, source_root, reference_source.resolve())
    result = {"schema": "termforge-original-resize-unicode-reference-v1", "issue": 29, "related_issue": 27,
              "case_id": plan["case_id"], "authorized_plan_sha256": expected_plan_sha256,
              "offline_export_source_files_sha256": {name: sha(ROOT / name) for name in EXPORT_SOURCES},
              "status": "bounded-direct-protocol-and-unicode-text-observation", "actual_original_runs": 1, "maximum_original_runs": 1,
              "semantic_acceptance": False, "unicode_policy_classification": None, "full_active_grid_verified": False,
              "terminal_styles_verified": False, "local_shell_transport_verified": False, "presented_done": None,
              "performance_gate": "not_evaluated", "r2_complete": False, "rea_capture_seconds": 45,
              "cli_wait_timeout_seconds": 60, "whole_lifecycle_bound_seconds": None,
              "deliberate_original_process_termination": True, "natural_gui_exit_verified": False,
              "capture": facts, "provenance": provenance, "limitations": [
                  "One fresh direct Telnet process executes one immutable wide or combining sequence; every phase is observed before the next input or resize.",
                  "Stage 1 observes shrink without modifying text or CUP; stage 2 sends only Q or U+0302; stage 3 restores width after that action.",
                  "Document text offsets, API position values, UTF-8 bytes, DSR and pixel rectangles are different observations; no C++ Cell-column mapping is inferred.",
                  "All input scalars are BMP. Equal UTF-16 and scalar offsets in this sample cannot establish the original API's general Unicode unit.",
                  "DSR is bounded independently to row 1..200 and column 1..500; a reply beyond the current 61-column grid is preserved, not rejected as a viewport mismatch.",
                  "Only finite source-derived candidates, declared NFC variants and fixed boundary blanks are publishable. Missing or split marks and shared rectangles remain observations, not predicted semantics.",
                  "Only 91x20 -> 61x20 -> 91x20 is covered, with no full grid, local-shell transport, reliability repetition or performance acceptance.",
                  "Public text contains only validated synthetic candidate indices and UTF-8 hex. Unrestricted recordings, paths, process identifiers, profiles, resources and provider diagnostics remain private."]}
    payload = (json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False)+"\n").encode()
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("xb") as stream:
        stream.write(payload)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("directory", "plan", "destination"):
        parser.add_argument("--"+name, type=Path, required=True)
    parser.add_argument("--expected-plan-sha256", required=True)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--reference-source", type=Path)
    args = parser.parse_args()
    result = export(args.directory, args.plan, args.destination, args.expected_plan_sha256, args.source_root, args.reference_source)
    print(json.dumps({"exported": True, "case_id": result["case_id"], "phases": len(result["capture"]["phases"]), "sha256": sha(args.destination)}))
