"""Pin a separate four-process immutable Search recipe; never build or run it."""
from __future__ import annotations
import argparse
import ast
import hashlib
import json
from pathlib import Path
import shutil
import sys
from prepare import HERE, PRODUCT, ROOT, generated_sources, git, product, require

BASE = "a8b338b4f5f96e72eba00a5e495fb8dcb85cd0d3"
STAGES = ["cold_absent_start", "cold_absent_step", "warm_needle_start",
          "warm_needle_step", "last_match", "navigate_no_wrap"]


def identity(path):
    data = Path(path).read_bytes()
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def same_types(actual, expected):
    if type(actual) is not type(expected):
        return False
    if isinstance(expected, dict):
        return set(actual) == set(expected) and all(same_types(actual[k], v) for k, v in expected.items())
    if isinstance(expected, list):
        return len(actual) == len(expected) and all(same_types(a, b) for a, b in zip(actual, expected))
    return actual == expected


def bounds(history):
    n = history + 6
    work = 10 * n - 1
    calls = (work + 63) // 64
    lookups = 23 * n + 4 * calls + 152
    # Ten work units per non-final row. A step beginning inside a loaded
    # row validates it once; the persistent first row costs one prune probe.
    loaded_checks = [64*k//10+1 for k in range(calls) if (64*k) % 10]
    final_probe = int(work % 64 != 0)
    return dict(retained_rows=n, bytes_per_scan=8*n-1, work_per_scan=work,
                step_calls_per_scan=calls, lookups_per_sample_upper_bound=lookups,
                locate_calls_per_scan=3*n-2+final_probe+calls-1+len(loaded_checks),
                scan_probes_per_scan=3*(n*(n+1)//2)-n-1+final_probe*n+calls-1+sum(loaded_checks),
                last_match_lookups=n+5, navigate_lookups=16*(n+9)+1,
                scan_last_match_probes=n*(n+1)//2+5*n,
                scan_navigate_probes=16*(n*(n+1)//2)+7*sum(j*(n-1)//15+1 for j in range(16))+33*n,
                scan_probes_per_process_upper_bound=4*lookups*n,
                lazy_lookup_probes_per_process_upper_bound=4*lookups*((n-1).bit_length()+1))


def recipe():
    return dict(screen_rows=6, columns=8, histories=[100, 1000], hit_count=16,
                hit_rank="floor(j*(N-1)/15), j=0..15", hit_text="needle.", other_text="..x....",
                swap_visible_rows=[2, 3], sorted_proven=False, order_revision=1,
                immutable=True, notify_change_calls=0, step_budget=64, result_limit=64,
                case_sensitive=True, queries=["absent!", "needle"], samples=[-1, 0, 1, 2],
                fresh_adapter_each_sample=True, reuse_adapter_between_queries=True,
                stages=STAGES, fixture_rows=1, stage_rows_per_process=24,
                match_index=15, navigation="next; anchor last row col7; no wrap; no result",
                bounds={str(h): bounds(h) for h in (100, 1000)}, rss_bytes=None,
                counter_scope="lookup comparisons/visits; excludes table traversal/sort/unique check",
                time_scope="start call; sum/max step calls; one match; one navigate; counters included; literal oracle/fixture/JSON/object destruction excluded")


def cases():
    return [dict(id=f"static-search-{adapter}-{h}", adapter=adapter, history=h,
                 arguments=["--static-search-cost", adapter, str(h), "{permit}"], process_timeout_seconds=10)
            for adapter in ("scan", "lazy_table") for h in (100, 1000)]


def source_files():
    return sorted([p.relative_to(ROOT).as_posix() for p in HERE.iterdir() if p.is_file()] +
                  ["experiments/core/" + name for name in PRODUCT])


def bound_file(path):
    path = Path(path).resolve(strict=True)
    require(path.is_file(), "A real tool file is required")
    return {"path": str(path), **identity(path)}


def static_check():
    for name in PRODUCT:
        product(name)
    generated = generated_sources()  # The published generator/Search transformation is unchanged.
    for path in HERE.glob("*.py"):
        ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    cmake = (HERE / "CMakeLists.txt").read_text(encoding="utf-8")
    require("add_executable(r164_static_search_cost EXCLUDE_FROM_ALL" in cmake, "Separate excluded target missing")
    require(cmake.index('set(R164_SEARCH_HEADER ') < cmake.index('add_executable(r164_static_search_cost '), "Header binding must precede target")
    require("static_search_cost" not in (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"), "Root build must not include this experiment")
    return generated


def materialize(destination, compiler, cmake):
    generated = static_check()
    require(not git("status", "--porcelain", "--", "experiments/core/line_order").strip(), "Commit reviewed preparation sources before freezing a plan")
    require(destination.is_relative_to(ROOT / "local") and not destination.exists(), "New local preparation directory required")
    tools = {"python": bound_file(sys.executable), "compiler": bound_file(compiler), "cmake": bound_file(cmake)}
    headers = destination / "generated"
    headers.mkdir(parents=True)
    for name, text in generated.items():
        (headers / name).write_bytes(text.encode("utf-8"))
    head = git("rev-parse", "HEAD").decode().strip()
    plan = dict(schema="termforge-r164-static-search-cost-plan-v1", base_commit=BASE, source_commit=head,
                execution_authorized=False, actual_runs=0, authorized_cases=[], recipe=recipe(), cases=cases(),
                maximum_attempts_per_case=1, maximum_batch_seconds=50, whole_batch_bound_seconds=None,
                source_identity={name: identity(ROOT / name) for name in source_files()},
                generated_directory=str(headers), generated_headers={name: bound_file(headers / name) for name in generated},
                tools=tools, actual_executable={"path": None, "bytes": None, "sha256": None, "observed": False},
                build_evidence=None, output_directory=None, attempt_marker=None,
                whole_lifecycle_hard_bound_seconds=None, product_acceptance=None, search_cost_result=None)
    path = destination / "false-plan.json"
    path.write_text(json.dumps(plan, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return {"plan": str(path), **identity(path), "source_commit": head, "execution_authorized": False,
            "target_compiled": False, "target_runs": 0}


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--materialize", type=Path)
    cli.add_argument("--compiler", type=Path)
    cli.add_argument("--cmake", type=Path)
    args = cli.parse_args()
    if args.materialize:
        require(args.compiler is not None, "Select the compiler file explicitly; no implicit version fallback")
        cmake = args.cmake or shutil.which("cmake")
        require(cmake is not None, "Select the actual CMake file")
        print(json.dumps(materialize(args.materialize.resolve(), args.compiler, cmake)))
    else:
        generated = static_check()
        print(json.dumps({"python_ast": "parsed", "generated_sha256": {
            name: hashlib.sha256(text.encode()).hexdigest() for name, text in generated.items()},
            "bounds": {str(h): bounds(h) for h in (100, 1000)}, "target_compiled": False, "target_runs": 0}))


if __name__ == "__main__":
    main()
