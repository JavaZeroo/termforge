"""Apply the mode-state publication guards to the alternate RIS recording."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path, PureWindowsPath
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from research.ui import alternate_ris_reference as probe
from research.ui import export_mode_state_reference as base


def expected_probe(plan, run, source_root):
    base.integer(run["pid"], 2**32-1)
    base.integer(run["tid"], 2**32-1)
    base.require(re.fullmatch(r"TermForgeR1-[0-9]+", run["window_station"]), "Owned station identity differs")
    driver = probe.javascript_source()
    base.require(hashlib.sha256(driver.encode()).hexdigest() == plan["generated_driver_sha256"], "RIS driver differs")
    template = (source_root / "research/ui/qt_r1_text.js").read_text(encoding="utf-8").split("// Scenario driver", 1)[0] + driver
    base.require(hashlib.sha256(template.encode()).hexdigest() == run["template_sha256"], "Probe template differs")
    generated = template.replace("__UI_THREAD_ID__", str(run["tid"])).replace("__OWNED_STATION__", run["window_station"])
    generated = generated.replace("__RUN_DIRECTORY_JSON__", json.dumps(PureWindowsPath(plan["out"]).as_posix()))
    generated = generated.replace("__HOSTED_CI__", "false").replace("__SCENARIO_TIMEOUT_MS__", "43000")
    return generated.encode().replace(b"\n", b"\r\n")


source = probe.substitute(Path(base.__file__).read_text(encoding="utf-8"), {
    "from research.ui import mode_state_reference as probe": "from research.ui import alternate_ris_reference as probe",
    "termforge-original-mode-state-reference-v1": "termforge-original-alternate-ris-reference-v1",
    '"response_hex": row["response_hex"], "private_dsr_prefix":':
        '"response_hex": row["response_hex"], "flow_control_hex": row.get("flow_control_hex"), "private_dsr_prefix":',
    '"offline_exporter_sha256": sha(Path(__file__)),':
        '"offline_exporter_sha256": sha(Path(__file__)), "base_exporter_sha256": sha(ROOT / "research/ui/export_mode_state_reference.py"),',
})
_runtime = {"__file__": __file__, "__name__": "termforge_alternate_ris_export"}
exec(compile(source, str(Path(base.__file__)), "exec"), _runtime)
_runtime.update(expected_probe=expected_probe, MARKERS=probe.MARKERS,
                SYNTHETIC_LINE=re.compile(r" *(?:HIST|MAIN|ALT|AFTER)? *"))
export = _runtime["export"]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("directory", "plan", "destination", "reference-source"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--expected-plan-sha256", required=True)
    args = parser.parse_args()
    result = export(args.directory, args.plan, args.destination, args.expected_plan_sha256, ROOT, args.reference_source)
    print(json.dumps({"phases": len(result["capture"]["phases"]), "sha256": base.sha(args.destination)}))
