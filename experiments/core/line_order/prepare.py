"""Static preparation only: pin product sources, materialize isolated copies, false plan."""
from __future__ import annotations
import argparse
import ast
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
BASE = "35a323a3165dae6998237bfd768c7c54c9c8ae99"
PRODUCT = ("screen.hpp", "terminal.hpp", "unicode_width.hpp", "text_projection.hpp",
           "text_document.hpp", "text_selection.hpp", "text_search.hpp", "history_view.hpp")


def require(ok, message):
    if not ok:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def product(name):
    relative = "experiments/core/" + name
    baseline = git("show", BASE + ":" + relative).decode("utf-8").replace("\r\n", "\n")
    current = (ROOT / relative).read_text(encoding="utf-8")
    require(current == baseline, "production source differs from pinned baseline: " + name)
    return current


def once(text, old, new):
    require(text.count(old) == 1, "transformation seam differs: " + old[:70])
    return text.replace(old, new)


def generated_sources():
    selection = product("text_selection.hpp")
    selection = ('#pragma once\n#include "text_selection.hpp"\n#include "document.hpp"\nnamespace termforge {\n' +
                 selection[selection.index("class TextSelection {"):])
    selection = once(selection, "class TextSelection {", "template<class Adapter> class R164Selection {")
    selection = selection.replace("const TextDocument&", "const line_order::Document<Adapter>&")
    search = product("text_search.hpp")
    search = ('#pragma once\n#include "text_search.hpp"\n#include "document.hpp"\nnamespace termforge {\n' +
              search[search.index("class TextSearch {"):])
    search = once(search, "class TextSearch {", "template<class Adapter> class R164Search {\n    uint64_t prototype_revision_ = 0;")
    search = search.replace("const TextDocument&", "const line_order::Document<Adapter>&")
    old = ("    static bool before(const TextPosition& a, const TextPosition& b) {\n"
           "        return a.line_id < b.line_id || (a.line_id == b.line_id && a.column < b.column);\n    }")
    new = ("    static bool before(const line_order::Document<Adapter>& doc, const TextPosition& a, const TextPosition& b) {\n"
           "        const auto order = doc.compare(a, b); return order && *order < 0;\n    }")
    search = once(search, old, new)
    search = once(search, "before(*anchor, hit->range.begin)", "before(doc, *anchor, hit->range.begin)")
    search = once(search, "before(hit->range.begin, *anchor)", "before(doc, hit->range.begin, *anchor)")
    start = search.index("        const auto first = std::lower_bound(rows_.begin(), rows_.end(), hit.range.begin.line_id,")
    end = search.index("        return hit;", start)
    search = search[:start] + """        const auto begin = doc.index_of(hit.range.begin.line_id), end = doc.index_of(hit.range.end.line_id);
        if (!begin || !end || *begin > *end) return {};
        for (const auto& row : rows_) {
            const auto index = doc.index_of(row.meta.id);
            if (!index) return {};
            if (*index >= *begin && *index <= *end && !row_valid(doc, row.meta, row.successor_id)) return {};
        }
        if (loaded_) {
            const auto index = doc.index_of(current_meta_.id);
            if (!index || (*index >= *begin && *index <= *end && !row_valid(doc, current_meta_, current_successor_id_))) return {};
        }
""" + search[end:]
    start = search.index("    uint64_t notify_change(")
    end = search.index("    SearchStep step(", start)
    search = search[:start] + "    // Incremental mutation is unsupported in this snapshot-only caller.\n    uint64_t notify_change(const line_order::Document<Adapter>&) = delete;\n" + search[end:]
    search = once(search, "        advance_generation(); reset_scan(); query_ = std::move(query);",
                  "        prototype_revision_ = doc.order_revision();\n        advance_generation(); reset_scan(); query_ = std::move(query);")
    search = once(search, "        sync_epoch(doc);\n        SearchStep progress",
                  "        if (prototype_revision_ != doc.order_revision()) return {SearchStatus::invalidated, 0, 0, 0, false};\n        sync_epoch(doc);\n        SearchStep progress")
    search = once(search, "        if (document_epoch_ != doc.epoch() || index >= matches_.size()) return {};",
                  "        if (prototype_revision_ != doc.order_revision() || document_epoch_ != doc.epoch() || index >= matches_.size()) return {};")
    banner = "// Generated from pinned TermForge sources; experimental immutable-snapshot caller only.\n"
    require("enum class SearchStatus" not in search and "struct SelectionColumns" not in selection,
            "generated caller must reuse existing DTOs instead of redefining them")
    return {"selection.hpp": banner + selection, "search.hpp": banner + search}


def static_check():
    for name in PRODUCT:
        product(name)
    for path in HERE.glob("*.py"):
        ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    outputs = generated_sources()
    require("EXCLUDE_FROM_ALL" in (HERE / "CMakeLists.txt").read_text(), "target must remain excluded")
    require("line_order" not in (ROOT / "CMakeLists.txt").read_text(), "production build unexpectedly includes prototype")
    return outputs


def tool(path):
    value = Path(path).resolve() if path else None
    return {"path": str(value) if value and value.is_file() else None,
            "sha256": digest(value.read_bytes()) if value and value.is_file() else None,
            "identity_kind": "file_sha256_only; no version process invoked"}


def case_definitions():
    cases = [dict(id="existing-red", arguments=["--existing-red"], expected_exit_codes=[1], process_timeout_seconds=10),
             dict(id="adapter-correctness", arguments=["--correctness"], expected_exit_codes=[0], process_timeout_seconds=10)]
    for adapter in ("scan", "lazy_table"):
        for history in (100, 1000, 10000):
            for workflow in ("append", "reorder", "evict"):
                cases.append(dict(id=f"cost-{adapter}-{history}-{workflow}",
                                  arguments=["--cost", adapter, str(history), workflow, "{permit}"],
                                  expected_exit_codes=[0], process_timeout_seconds=10))
    return cases


def materialize(destination):
    outputs = static_check()
    require(destination.is_relative_to(ROOT / "local"), "preparation destination must stay under local")
    require(not destination.exists(), "refuse existing preparation directory")
    generated = destination / "generated"
    generated.mkdir(parents=True)
    for name, value in outputs.items():
        (generated / name).write_bytes(value.encode("utf-8"))
    head = git("rev-parse", "HEAD").decode().strip()
    source_files = {path.relative_to(ROOT).as_posix(): digest(path.read_bytes())
                    for path in sorted(HERE.iterdir()) if path.is_file()}
    source_files.update({"experiments/core/" + name: digest((ROOT / "experiments/core" / name).read_bytes()) for name in PRODUCT})
    cases = case_definitions()
    plan = dict(schema="termforge-line-order-r164-plan-v1", issue=164, base_commit=BASE, source_commit=head,
                execution_authorized=False, authorized_cases=[],
                actual_executable=dict(path=None, sha256=None, observed=False,
                                       candidate_path=str(ROOT / "build/line-order-r164/Release/r164_probe.exe")),
                tools=dict(python=tool(sys.executable), cmake=tool(shutil.which("cmake")),
                           compiler=tool("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.42.34433/bin/Hostx64/x64/cl.exe")),
                source_sha256=source_files,
                generated_sha256={str((generated / name).relative_to(ROOT)).replace("\\", "/"): digest(value.encode("utf-8")) for name, value in outputs.items()},
                proposed_first_lease=dict(cases=["existing-red", "adapter-correctness"], whole_batch_bound_seconds=25),
                proposed_cost_lease=dict(processes=18, whole_batch_bound_seconds=200, authorization=False,
                                        warmup_samples_per_process=1, measured_samples_per_process=3,
                                        rounds_per_sample=32, mutations_per_round=1, lookups_per_round=16,
                                        lookups_per_sample=512, lookups_per_process_including_warmup=2048,
                                        hit_queries_per_round=14, missing_queries_per_round=2,
                                        query_ranks=["0", "N/4", "N/2", "3N/4", "N-1", "H", "H+1", "H+2", "H+3", "H+4", "H+5", "0", "N/2", "N-1"],
                                        missing_ids=["0", "UINT64_MAX"], static_searches_per_round=0,
                                        static_searches_per_process=0, search_cost=None,
                                        memory_metric="position-table capacity payload bytes only; not allocator total or RSS",
                                        timing="mutation separately; first lookup separately; other15 lookups separately; literal checks outside lookup timings; counters included"),
                cases=cases, whole_batch_bound_seconds=None,
                output_directory=None, attempt_marker=None,
                actual_runs=0, compiler_executed=False, actual_results=None,
                unknown=["prototype C++ syntax/runtime until leased build", "incremental Search mutation/rewind/prune/emit semantics",
                         "production structure notifications/cache invalidation", "UI fallback for missing anchor",
                         "full allocator/RSS memory", "search cost at full retained-history sizes", "product choice", "WindTerm performance parity"])
    target = destination / "false-plan.json"
    target.write_bytes((json.dumps(plan, ensure_ascii=False, indent=2) + "\n").encode())
    return {"plan": str(target), "plan_sha256": digest(target.read_bytes()), "source_commit": head,
            "execution_authorized": False, "generated_files": len(outputs), "cpp_executions": 0}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--materialize", type=Path)
    args = parser.parse_args()
    if args.materialize:
        print(json.dumps(materialize(args.materialize.resolve()), ensure_ascii=False))
    else:
        outputs = static_check()
        print(json.dumps(dict(python_ast="parsed", pinned_product_files=len(PRODUCT),
                              generated_text_seams="matched", generated_sha256={name: digest(value.encode()) for name, value in outputs.items()},
                              cpp_compiled=False, cpp_executed=False), ensure_ascii=False))


if __name__ == "__main__":
    main()
