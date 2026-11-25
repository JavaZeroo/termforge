"""Prepare a new resize observation plan without starting the reference app."""

import hashlib
import json
from pathlib import Path

from research.ui import mode_state_reference as common


def prepare_plan(source, out, rea, destination, case, execution_authorized, *, root,
                 executable_sha256, fixed_contract, source_identity, launcher_source):
    source, out, rea, destination = [Path(path).resolve() for path in (source, out, rea, destination)]
    local_root = root / "local"
    common.require(type(execution_authorized) is bool, "Execution authorization must be boolean")
    common.require(not out.exists() and not destination.exists(), "Plan/output must be new")
    common.require(out != local_root and out.is_relative_to(local_root) and destination.is_relative_to(local_root),
                   "Plan/output must belong to this worktree local directory")
    common.require(not out.is_relative_to(source) and not source.is_relative_to(out), "Source/output overlap")
    common.require(common.sha(source / "WindTerm.exe") == executable_sha256, "Pinned original executable differs")

    plan = {**fixed_contract(case), **source_identity(), **common.host_identity(),
            "execution_authorized": execution_authorized, "source": str(source), "out": str(out), "rea": str(rea),
            "rea_prepared_identity": common.rea_identity(rea),
            "launcher_source_sha256": hashlib.sha256(launcher_source(case).encode()).hexdigest()}
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(plan, indent=2) + "\n")
    return plan
