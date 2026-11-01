"""Strict synthetic export for the separate duplicate/later-entry observation.

Reuse the mode-state export guards in a private namespace without changing the
already-used exporter. The reentry input, driver, markers and recording identity
remain separate, including hashes of both exporter source files.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path, PureWindowsPath
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from research.ui import export_mode_state_reference as original
from research.ui import mode_reentry_reference as probe


def expected_probe(plan, run, source_root):
    original.integer(run["pid"], 2**32-1)
    original.integer(run["tid"], 2**32-1)
    original.require(type(run["window_station"]) is str and re.fullmatch(r"TermForgeR1-[0-9]+", run["window_station"]),
                     "Owned station identity differs")
    driver = probe.javascript_source()
    original.require(hashlib.sha256(driver.encode()).hexdigest() == plan["generated_driver_sha256"], "Derived reentry driver differs")
    template = (source_root / "research/ui/qt_r1_text.js").read_text(encoding="utf-8").split("// Scenario driver", 1)[0]+driver
    original.require(hashlib.sha256(template.encode()).hexdigest() == run["template_sha256"], "Probe template differs")
    generated = template.replace("__UI_THREAD_ID__", str(run["tid"])).replace("__OWNED_STATION__", run["window_station"])
    generated = generated.replace("__RUN_DIRECTORY_JSON__", json.dumps(PureWindowsPath(plan["out"]).as_posix()))
    generated = generated.replace("__HOSTED_CI__", "false").replace("__SCENARIO_TIMEOUT_MS__", "43000")
    return generated.encode().replace(b"\n", b"\r\n")


_source = Path(original.__file__).read_text(encoding="utf-8")
_import = "from research.ui import mode_state_reference as probe"
original.require(_source.count(_import) == 1, "Protected export import changed")
_source = _source.replace(_import, "from research.ui import mode_reentry_reference as probe")
_source = _source.replace("mode-state-", "mode-reentry-").replace("mode_state_phase", "mode_reentry_phase").replace("mode_state_result", "mode_reentry_result")
_anchor = '"offline_exporter_sha256": sha(Path(__file__)),'
original.require(_source.count(_anchor) == 1, "Protected export identity anchor changed")
_source = _source.replace(_anchor, _anchor+' "offline_export_source_files_sha256": {name: sha(ROOT / name) for name in EXPORT_SOURCES},')
_RUNTIME = {"__file__": __file__, "__name__": "termforge_mode_reentry_export"}
exec(compile(_source, str(Path(original.__file__)), "exec"), _RUNTIME)
_RUNTIME.update(expected_probe=expected_probe, MARKERS=probe.MARKERS,
                SYNTHETIC_LINE=re.compile(r" *(?:(?:R0[1-8]|(?:DS|DI|DO|ES|EI|EO)[1-8]?|DONE)(?: +(?:MP|A0|A1))?|MP|A0|A1)? *"),
                EXPORT_SOURCES=("research/ui/export_mode_reentry_reference.py", "research/ui/export_mode_state_reference.py"))
for _name in ("export", "capture_facts", "provenance_facts", "marker_projection", "read", "sha", "RAW_NAMES", "MODULES"):
    globals()[_name] = _RUNTIME[_name]


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
