"""Run the six original Qt resize inputs in an isolated WindTerm local shell."""
from __future__ import annotations

import argparse
import json
from pathlib import Path


def launcher(path):
    source = path.read_text(encoding="utf-8")
    substitutions = {
        'fixture = Path(__file__).with_name("r1_fixture.py").resolve()':
            'fixture = Path(__file__).with_name("winpty_bcd_native.py").resolve()',
        '"{fixture}" "{out}""':
            '"{fixture}" "{out}" --child --legacy --await-ui"',
        'script = Path(__file__).with_name("qt_r1_text.js").read_text(encoding="utf-8")':
            'script = Path(__file__).with_name("qt_r1_text.js").read_text(encoding="utf-8")'
            '\n        script = script.split("// Scenario driver", 1)[0]'
            ' + Path(__file__).with_name("r29_local_legacy_reference.js").read_text(encoding="utf-8")',
    }
    for old, new in substitutions.items():
        if source.count(old) != 1:
            raise ValueError("Frozen launcher template anchor differs")
        source = source.replace(old, new)
    namespace = {"__name__": "termforge_r29_local_legacy_launcher", "__file__": str(path)}
    exec(compile(source, str(path), "exec"), namespace)
    return namespace


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--rea", type=Path, required=True)
    args = parser.parse_args()
    inherited = launcher(Path(__file__).with_name("r1_launch.py"))
    inherited["run"](args.source, args.out, args.rea, 32)
    ui = json.loads((args.out / "local-legacy-result.json").read_text(encoding="utf-8"))
    if ui["status"] != "observed" or len(ui["observations"]) != 6:
        raise RuntimeError("WindTerm local shell phases incomplete")
    for index, observation in enumerate(ui["observations"]):
        native = json.loads((args.out / f"phase-{index}.json").read_text(encoding="utf-8"))
        if observation["phase"] != index or observation["native_window"] != native["window"]:
            raise RuntimeError("WindTerm local and native observation identity differs")


if __name__ == "__main__":
    main()
