"""Compare extended terminal capabilities without treating unsupported strict-mode features as passes."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import platform

from cases import Case
from compare_candidate import compare_traces, run_case
from run import ROOT, sha, write_json
from experiments.candidates.alacritty.run import VERSION as CANDIDATE_VERSION, vendor_sources


class RawCase(Case):
    @property
    def data(self):
        return self.text.encode("latin-1")


CAPABILITIES = (
    dict(name="alternate_screen", supported=False,
         case=Case("alternate_screen", "A\r\nB\x1b[?1049hALT\x1b[?1049lZ")),
    dict(name="scroll_region", supported=False,
         case=Case("scroll_region", "A\r\nB\r\nC\r\nD\x1b[2;3r\x1b[3;1H\nX\x1b[r\x1b[1;1H")),
    dict(name="cursor_query", supported=False,
         case=Case("cursor_query", "A\x1b[6n")),
    dict(name="recovery_stream", supported=True, arguments=("--recovery",),
         case=RawCase("recovery_stream", "A\x1b[?9999zB\x1b]0;title\x07C\xffD")),
    dict(name="resize_stream", supported=True, arguments=("--resize=2x6",), fragmentation=False,
         case=Case("resize_stream", "abc中\r\n1234567890")),
)


def row_text(row):
    return "".join(cell["text"] for cell in row).rstrip()


def raw_records(output, prefix):
    return [json.loads(line) for line in (output / f"{prefix}.jsonl").read_text(encoding="utf-8").splitlines()]


def run_suite(core_binary, candidate_binary, output, selected=None, plans=None):
    output.mkdir(parents=True, exist_ok=False)
    records = []
    for capability in selected or CAPABILITIES:
        case = capability["case"]
        arguments = capability.get("arguments", ())
        byte_core, byte_candidate = None, None
        for plan in plans or ["byte", "whole", "seed:42", "seed:20261022", "seed:8675309"]:
            folder = output / capability["name"] / plan.replace(":", "-")
            candidate_arguments = arguments if capability["name"] == "resize_stream" else ()
            report, core, candidate = run_case(core_binary, candidate_binary, case, plan, folder, arguments,
                                               candidate_arguments)
            raw_candidate = raw_records(folder, "candidate")
            report.update(capability=capability["name"], expected_core_support=capability["supported"],
                          core_arguments=list(arguments), candidate_arguments=list(candidate_arguments),
                          replies=[reply for record in raw_candidate for reply in record["replies"]],
                          candidate_final_rows=[row_text(row) for row in candidate[-1]["screen"]["cells"]]
                          if candidate else [],
                          candidate_final_dimensions=[len(candidate[-1]["screen"]["cells"]),
                                                    len(candidate[-1]["screen"]["cells"][0])] if candidate else [],
                          core_final_dimensions=[len(core[-1]["screen"]["cells"]),
                                                 len(core[-1]["screen"]["cells"][0])] if core else [])
            if not capability["supported"] and report["core_returncode"] != 0 and report["candidate_returncode"] == 0 \
                    and not report["candidate_error"]:
                report["status"] = "core_unsupported"
            if capability["name"] == "resize_stream" and report["status"] == "difference":
                report["status"] = "resize_difference"
            if plan == "byte":
                byte_core, byte_candidate = core, candidate
            elif byte_core is not None and capability["supported"] and capability.get("fragmentation", True):
                offsets = [item["offset"] for item in core]
                report["core_fragmentation_difference"] = compare_traces([byte_core[i] for i in offsets], core)
                report["candidate_fragmentation_difference"] = compare_traces(
                    [byte_candidate[i] for i in offsets], candidate)
                if report["core_fragmentation_difference"] or report["candidate_fragmentation_difference"]:
                    report["status"] = "fragmentation_failure"
            write_json(folder / "report.json", report)
            records.append(report)
    passed = all(record["status"] in ("match", "reference_difference", "core_unsupported", "resize_difference")
                 for record in records)
    sources = ["experiments/core/trace.cpp", "experiments/differential/compare_candidate.py",
               "experiments/differential/capability_matrix.py", "experiments/differential/candidate-known-differences.json",
               "experiments/candidates/alacritty/src/main.rs", "experiments/candidates/alacritty/run.py"] + vendor_sources()
    summary = dict(schema=1, candidate="alacritty_terminal", version=CANDIDATE_VERSION,
                   platform=platform.platform(), core_trace_sha256=sha(core_binary),
                   candidate_trace_sha256=sha(candidate_binary),
                   source_sha256={path: sha(ROOT / path) for path in sources},
                   runs=len(records), checkpoints=sum(record["checkpoints"] for record in records),
                   counts={status: sum(record["status"] == status for record in records)
                           for status in sorted({record["status"] for record in records})},
                   passed=passed, execution_passed=passed, semantic_acceptance=False,
                   performance_measured=False,
                   unsupported_is_not_a_pass=True,
                   resize_covered=any(record["capability"] == "resize_stream" for record in records),
                   real_application_covered=False,
                   records=records)
    write_json(output / "summary.json", summary)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True, help="new output directory; never overwritten")
    parser.add_argument("--case", choices=[item["name"] for item in CAPABILITIES])
    parser.add_argument("--plan", help="byte, whole, or seed:<integer>; default runs all five plans")
    args = parser.parse_args()
    selected = [item for item in CAPABILITIES if args.case is None or item["name"] == args.case]
    result = run_suite(args.trace.resolve(strict=True), args.candidate.resolve(strict=True), args.out.resolve(),
                       selected, [args.plan] if args.plan else None)
    print(json.dumps({key: result[key] for key in ("execution_passed", "runs", "checkpoints", "counts",
                                                   "semantic_acceptance", "resize_covered")}, indent=2))
    return 0 if result["execution_passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
