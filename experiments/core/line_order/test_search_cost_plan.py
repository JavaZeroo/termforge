"""Pure plan/protocol seams only; never run the C++ probe, CMake or compiler."""
import copy
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import prepare_search_cost as prepared
import run_search_cost as runner


def protocol(case):
    h, adapter = case["history"], case["adapter"]
    b = prepared.bounds(h); n = h+6
    identities = [[i+1, 8] for i in range(n)]
    identities[h+2], identities[h+3] = identities[h+3], identities[h+2]
    ranks = [j*(n-1)//15 for j in range(16)]
    expected = [[*identities[r], 0, 0, *identities[r], 6, 0] for r in ranks]
    rows = [dict(schema="termforge-r164-static-search-fixture-v1", adapter=adapter, history=h,
                 retained_rows=n, epoch=0, order_revision=1, sorted_proven=False, rows=identities,
                 hit_ranks=ranks, oracle_ranges=expected, rss_bytes=None)]
    for sample in (-1, 0, 1, 2):
        for stage_index, stage in enumerate(prepared.STAGES):
            scan = stage.endswith("_step")
            lookups = b["locate_calls_per_scan"] if scan else b["last_match_lookups"] if stage == "last_match" else b["navigate_lookups"] if stage == "navigate_no_wrap" else 0
            probes = b["scan_probes_per_scan"] if scan else b["scan_last_match_probes"] if stage == "last_match" else b["scan_navigate_probes"] if stage == "navigate_no_wrap" else 0
            if adapter == "lazy_table": probes = lookups  # Synthetic values within that decoder's bound, not a target measurement.
            rows.append(dict(schema="termforge-r164-static-search-stage-v1", adapter=adapter, history=h,
                sample=sample, stage=stage, elapsed_ns=1, max_step_ns=1 if scan else None,
                step_calls=b["step_calls_per_scan"] if scan else 0, work_used=b["work_per_scan"] if scan else 0,
                bytes_scanned=b["bytes_per_scan"] if scan else 0, rows_loaded=n if scan else 0,
                lookups=lookups, probes=probes, rebuilds=int(adapter == "lazy_table" and stage_index == 1),
                table_payload_bytes=n*16 if adapter == "lazy_table" and stage_index else 0,
                search_status=1 if stage.endswith("_start") else 2,
                result_ranges=expected if stage == "warm_needle_step" else [expected[-1]] if stage == "last_match" else [],
                oracle_passed=True, rss_bytes=None))
    return rows


def encode(rows):
    return ("\r\n".join(json.dumps(row, separators=(",", ":")) for row in rows)+"\r\n").encode("ascii")


def with_epoch(rows, epoch):
    rows = copy.deepcopy(rows)
    rows[0]["epoch"] = epoch
    ranges = rows[0]["oracle_ranges"] + [r for row in rows[1:] for r in row["result_ranges"]]
    for value in ranges:
        value[3] = value[7] = epoch
    return rows


class PlanTests(unittest.TestCase):
    def test_false_real_main_rejects_before_identity_launch_or_mutation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); path = root/"false.json"
            path.write_text(json.dumps(dict(execution_authorized=False,
                output_directory=str(root/"out"), attempt_marker=str(root/"attempt"))))
            with patch.object(sys, "argv", ["run_search_cost.py", "--plan", str(path)]), \
                 patch.object(runner, "verify_inputs", side_effect=AssertionError("false must fail before identity work")), \
                 patch.object(runner.subprocess, "run", side_effect=AssertionError("no process allowed")):
                with self.assertRaisesRegex(ValueError, "False plan"): runner.main()
            self.assertFalse((root/"out").exists()); self.assertFalse((root/"attempt").exists())

    def test_recipe_rejects_bool_and_float_integer_contracts(self):
        for value in (True, 64.0, "64"):
            modified = prepared.recipe(); modified["step_budget"] = value
            self.assertFalse(prepared.same_types(modified, prepared.recipe()))
        self.assertEqual([x["id"] for x in prepared.cases()], ["static-search-scan-100", "static-search-scan-1000",
                         "static-search-lazy_table-100", "static-search-lazy_table-1000"])

    def test_static_bounds_are_the_frozen_small_workload(self):
        self.assertEqual(prepared.bounds(100)["scan_probes_per_process_upper_bound"], 1126992)
        self.assertEqual(prepared.bounds(1000)["scan_probes_per_process_upper_bound"], 96262128)
        self.assertEqual(prepared.bounds(1000)["lazy_lookup_probes_per_process_upper_bound"], 1052568)

    def test_real_loader_rejects_changed_or_loose_numeric_contracts_before_identity(self):
        valid = dict(schema="termforge-r164-static-search-cost-plan-v1", base_commit=prepared.BASE,
            execution_authorized=True, actual_runs=0, recipe=prepared.recipe(), cases=prepared.cases(),
            authorized_cases=[case["id"] for case in prepared.cases()], maximum_attempts_per_case=1,
            maximum_batch_seconds=50, whole_batch_bound_seconds=50)
        variants = []
        for value in (True, 64.0, "64"):
            plan = copy.deepcopy(valid); plan["recipe"]["step_budget"] = value; variants.append(plan)
        for key, value in (("actual_runs", False), ("maximum_attempts_per_case", True), ("whole_batch_bound_seconds", 51)):
            plan = copy.deepcopy(valid); plan[key] = value; variants.append(plan)
        plan = copy.deepcopy(valid); plan["authorized_cases"].reverse(); variants.append(plan)
        with tempfile.TemporaryDirectory() as temporary, patch.object(runner, "verify_inputs", side_effect=AssertionError("invalid contract reached identity")):
            path = Path(temporary)/"plan.json"
            for plan in variants:
                path.write_text(json.dumps(plan))
                with self.assertRaises(ValueError): runner.load_plan(path)

    def test_synthetic_protocol_all_four_recipes(self):
        for case in prepared.cases():
            with self.subTest(case=case["id"]): runner.validate_output(encode(protocol(case)), b"", case)

    def test_fixture_epoch_is_exact_initial_zero_including_all_ranges(self):
        case = prepared.cases()[0]
        for epoch in (-1, 1, 2, True, 0.0, "0"):
            with self.subTest(epoch=epoch):
                with self.assertRaisesRegex(ValueError, "Fixture identity/frame contract"):
                    runner.validate_output(encode(with_epoch(protocol(case), epoch)), b"", case)

    def test_retained_actual_scan100_stdout_and_epoch_mutations(self):
        path = os.environ.get("TERMFORGE_R164_STATIC_SEARCH_OBSERVED_STDOUT")
        if not path:
            self.skipTest("Supply the retained Scan100 stdout path for the offline capture regression")
        raw = Path(path).read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),
                         "5419a5053275d985457834d94e4ed7970ac4bc022bc311af7129621b281251ea")
        case = prepared.cases()[0]
        runner.validate_output(raw, b"", case)
        rows = [json.loads(line) for line in raw.decode("ascii").splitlines()]
        self.assertEqual(rows[0]["epoch"], 0)
        for epoch in (-1, 1, 2, True, 0.0, "0"):
            with self.subTest(epoch=epoch):
                with self.assertRaisesRegex(ValueError, "Fixture identity/frame contract"):
                    runner.validate_output(encode(with_epoch(rows, epoch)), b"", case)

    def test_decoder_rejects_truncation_bad_ranges_types_counters_and_stages(self):
        case = prepared.cases()[0]; original = protocol(case)
        variants = []
        variants.append(original[:-1])
        for offset, key, value in ((4, "work_used", 0), (4, "bytes_scanned", True),
                                   (4, "result_ranges", []), (5, "lookups", 0),
                                   (6, "probes", 0), (1, "oracle_passed", False),
                                   (1, "stage", "warm_needle_start"), (1, "rss_bytes", 0)):
            rows = copy.deepcopy(original); rows[offset][key] = value; variants.append(rows)
        rows = copy.deepcopy(original); rows[0]["rows"][0][0] = rows[0]["rows"][1][0]; variants.append(rows)
        for rows in variants:
            with self.assertRaises((ValueError, KeyError, TypeError)): runner.validate_output(encode(rows), b"", case)
        with self.assertRaises(ValueError): runner.validate_output(encode(original), b"unexpected", case)


if __name__ == "__main__":
    unittest.main(verbosity=2)
