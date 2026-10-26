"""Fixed streaming-grapheme contract through the real candidate Term/Processor."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from experiments.candidates.alacritty.run import run_trace, sha, trace_binary, write_json


FIXED_SAMPLES = (("zwj", "\U0001f469\u200d\U0001f4bb", 2),
                 ("keycap", "1\ufe0f\u20e3", 2))


def run_fixed_samples(binary: Path, output: Path):
    output.mkdir(parents=True, exist_ok=False)
    cases = []
    for name, text, width in FIXED_SAMPLES:
        payload = text.encode("utf-8")
        for plan, sizes in (("whole", [len(payload)]), ("byte", [1] * len(payload))):
            report, records = run_trace(binary, payload, 3, 8, 2, sizes, output / f"{name}-{plan}")
            final = records[-1]["screen"] if records else None
            occupied = ([[cell["text"], cell["width"]] for cell in final["cells"][0] if cell["width"]]
                        if final else None)
            passed = (report["passed"] and occupied == [[text, width]] and
                      final["row"] == 0 and final["column"] == width and final["wrap_pending"] is False)
            cases.append(dict(case=name, plan=plan, expected_text=text, expected_width=width,
                              occupied=occupied, row=final["row"] if final else None,
                              column=final["column"] if final else None, passed=passed, trace=report))
    result = dict(schema="termforge-fixed-streaming-graphemes-v1", trace_sha256=sha(binary),
                  passed=all(case["passed"] for case in cases), cases=cases,
                  windterm_runtime_executed=False, performance_measured=False)
    write_json(output / "summary.json", result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    arguments = parser.parse_args()
    result = run_fixed_samples(trace_binary(arguments.trace), arguments.out.resolve())
    print(json.dumps(result, ensure_ascii=True, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
