"""Execute only an independently authorized and completely bound R164 plan."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import subprocess
import sys
import time
from prepare import HERE, PRODUCT, case_definitions, generated_sources

ROOT = Path(__file__).resolve().parents[3]


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", required=True, type=Path)
    args = parser.parse_args()
    plan_bytes = args.plan.read_bytes()
    plan = json.loads(plan_bytes)
    require(plan.get("schema") == "termforge-line-order-r164-plan-v1", "wrong plan schema")
    require(plan.get("execution_authorized") is True, "execution_authorized must be true; false plan cannot create attempt/output")
    require(plan.get("actual_runs") == 0, "plan already records execution")
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    require(head == plan["source_commit"], "source HEAD differs")
    expected_sources = {path.relative_to(ROOT).as_posix() for path in HERE.iterdir() if path.is_file()}
    expected_sources.update("experiments/core/" + name for name in PRODUCT)
    require(set(plan["source_sha256"]) == expected_sources, "source closure incomplete or unexpected")
    require(len(plan["generated_sha256"]) == 2, "generated caller closure incomplete")
    generated = generated_sources()
    for name, expected in plan["generated_sha256"].items():
        require(Path(name).name in generated and hashlib.sha256(generated[Path(name).name].encode()).hexdigest() == expected,
                "generated caller seam/identity differs")
    for name, expected in {**plan["source_sha256"], **plan["generated_sha256"]}.items():
        path = (ROOT / name).resolve()
        require(path.is_relative_to(ROOT) and path.is_file() and sha(path) == expected, "source bytes differ: " + name)
    python = plan["tools"]["python"]
    require(Path(sys.executable).resolve() == Path(python["path"]).resolve() and sha(Path(sys.executable)) == python["sha256"], "Python identity differs")
    require(set(plan["tools"]) == {"python", "cmake", "compiler"}, "tool closure differs")
    for name, tool in plan["tools"].items():
        require(tool.get("path") and tool.get("sha256") and Path(tool["path"]).is_file() and sha(Path(tool["path"])) == tool["sha256"],
                "recorded tool identity differs: " + name)
    binary = plan["actual_executable"]
    require(binary.get("observed") is True and binary.get("path") and binary.get("sha256"), "actual executable identity missing")
    executable = Path(binary["path"]).resolve()
    require(executable.is_file() and sha(executable) == binary["sha256"], "actual executable bytes differ")
    selected = plan["authorized_cases"]
    require(isinstance(selected, list) and selected and len(set(selected)) == len(selected), "authorized cases empty or repeated")
    cases = {case["id"]: case for case in plan["cases"]}
    known = {case["id"]: case for case in case_definitions()}
    require(len(cases) == len(plan["cases"]) and cases == known, "case recipe/count/budget differs from prepared source")
    require(all(name in cases for name in selected), "unknown case")
    cost = any(name.startswith("cost-") for name in selected)
    require(not cost or plan["proposed_cost_lease"]["authorization"] is True, "cost lease not authorized")
    require(cost or selected == ["existing-red", "adapter-correctness"], "first lease must preserve exact single RED and correctness order")
    require(not cost or selected == [case["id"] for case in case_definitions() if case["id"].startswith("cost-")],
            "cost lease must preserve the frozen eighteen-case order/count")
    bound = plan["whole_batch_bound_seconds"]
    require(type(bound) is int and 1 <= bound <= (200 if cost else 25), "whole batch bound missing or enlarged")
    output = Path(plan["output_directory"]).resolve()
    marker = Path(plan["attempt_marker"]).resolve()
    require(output.is_relative_to(ROOT / "local") and marker.is_relative_to(ROOT / "local"), "output/marker outside local")
    require(not output.exists() and not marker.exists(), "output/attempt already exists")
    require(output != marker and not marker.is_relative_to(output) and not output.is_relative_to(marker), "marker must be separate from output")
    for name in selected:
        require(cases[name]["process_timeout_seconds"] == 10, "case deadline differs")
    # All identity/authorization checks precede mutation. This CLI never builds.
    marker.parent.mkdir(parents=True, exist_ok=True)
    with marker.open("x", encoding="utf-8") as handle:
        handle.write(hashlib.sha256(plan_bytes).hexdigest() + "\n")
    output.mkdir(parents=True)
    (output / "authorized-plan.json").write_bytes(plan_bytes)
    started = time.monotonic()
    permit = secrets.token_hex(16)
    environment = dict(os.environ, TERMFORGE_R164_COST_AUTHORIZED=permit)
    results = []
    for name in selected:
        case = cases[name]
        remaining = bound - (time.monotonic() - started)
        require(remaining > 0, "whole batch deadline elapsed")
        command = [str(executable), *[value.replace("{permit}", permit) for value in case["arguments"]]]
        case_started = time.monotonic()
        try:
            result = subprocess.run(command, cwd=ROOT, env=environment, capture_output=True,
                                    timeout=min(case["process_timeout_seconds"], remaining))
            stdout, stderr, exit_code, timed_out = result.stdout, result.stderr, result.returncode, False
        except subprocess.TimeoutExpired as error:
            stdout, stderr, exit_code, timed_out = error.stdout or b"", error.stderr or b"", None, True
        (output / (name + ".stdout.log")).write_bytes(stdout)
        (output / (name + ".stderr.log")).write_bytes(stderr)
        results.append(dict(case=name, exit_code=exit_code, timed_out=timed_out,
                            elapsed_seconds=time.monotonic() - case_started,
                            stdout_sha256=hashlib.sha256(stdout).hexdigest(), stderr_sha256=hashlib.sha256(stderr).hexdigest()))
        (output / "results.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
        require(not timed_out and exit_code in case["expected_exit_codes"], "timeout or unexpected case exit; stop without retry")
    print(json.dumps(dict(cases=results, plan_sha256=hashlib.sha256(plan_bytes).hexdigest(), elapsed_seconds=time.monotonic() - started)))


if __name__ == "__main__":
    main()
