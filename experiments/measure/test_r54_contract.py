"""Pure R54 schedule/provenance/oracle guards; these do not run performance loads."""
import copy
import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest

from experiments.measure.r54_contract import (
    CHUNKS, CPU_SECONDS, REPEATS, SEEDS, SOURCES, canonical, expected_sample,
    prepare, protocol, sample_order, validate_sample,
)
from experiments.measure.run import run_command, validate_result


def sample(observe=False, wrapped=False):
    return dict(schema_version=2, kind="core_cost", valid=True, cpu_floor_seconds=CPU_SECONDS,
                counter_off_is_uninstrumented=False, route="project", chunk=4096, observe=observe,
                rounds=8, iterations=8, bytes_per_round=78000,
                interval_cpu_seconds=2.1, interval_wall_seconds=2.2, cpu_gate_polls=8,
                cpu_interval_differences=1, counted_parser_destructions=7,
                state_fnv1a64=SOURCES["candidate"]["states"]["project"],
                receive_seconds=None, presented_done=None,
                ordinary_allocations=100 if observe and wrapped else None,
                ordinary_allocated_bytes=200 if observe and wrapped else None,
                allocator_wrapper_compiled=wrapped,
                cpu_clock="GetProcessTimes kernel plus user", wall_clock="std::time::Instant",
                wall_gate_every_iterations=8, cpu_gate_min_wall_spacing_seconds=0.25)


class CostPlanTests(unittest.TestCase):
    def test_complete_adjacent_source_pairs_and_alternating_order(self):
        for seed in SEEDS:
            order = sample_order(seed)
            self.assertEqual(order, sample_order(seed))
            self.assertEqual(len(order), sum(map(len, CHUNKS.values())) * REPEATS * 2)
            groups = {}
            for index in range(0, len(order), 2):
                first, second = order[index:index + 2]
                self.assertEqual(first["sequence"], index)
                self.assertEqual(second["sequence"], index + 1)
                self.assertEqual((first["route"], first["chunk"], first["repeat"]),
                                 (second["route"], second["chunk"], second["repeat"]))
                self.assertEqual({first["variant"], second["variant"]}, set(SOURCES))
                groups.setdefault((first["route"], first["chunk"]), []).append(first["variant"])
            self.assertEqual(len(groups), sum(map(len, CHUNKS.values())))
            for values in groups.values():
                self.assertEqual(len(values), REPEATS)
                self.assertTrue(all(a != b for a, b in zip(values, values[1:])))
        self.assertNotEqual(sample_order(SEEDS[0]), sample_order(SEEDS[1]))

    def test_workload_bytes_and_full_state_identities_remain_fixed(self):
        p = protocol()
        fixtures = dict(project=(b"x" * 76 + b"\r\n") * 1000,
                        interactive=b"abcdefgh\x1b[D\x1b[D\x1b[DZ\x08Q\r\n" * 1000)
        for route, payload in fixtures.items():
            self.assertEqual(p["workloads"][route]["bytes"], len(payload))
            self.assertEqual(p["workloads"][route]["sha256"], hashlib.sha256(payload).hexdigest())
            self.assertNotEqual(SOURCES["baseline"]["states"][route], SOURCES["candidate"]["states"][route])
        self.assertIsNone(p["budget"]["numerical_regression_limit"])
        self.assertEqual(p["budget"]["performance_gate"], "not_evaluated")
        self.assertFalse(p["original_diagnostic"]["comparable_to_this_protocol"])

    def test_prepare_is_exclusive_and_reports_unmeasured_identity(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "plan.json"
            value = prepare(path)
            self.assertEqual(json.loads(path.read_text(encoding="utf-8")), value)
            self.assertEqual(value["protocol_sha256"], hashlib.sha256(canonical(value["protocol"])).hexdigest())
            self.assertEqual(value["phase"], "prepared_not_built_or_measured")
            self.assertEqual(value["builds"], dict(baseline=None, candidate=None))
            self.assertIsNone(value["environment_evidence"])
            with self.assertRaises(FileExistsError):
                prepare(path)

    def test_absent_allocator_must_be_explicit_and_does_not_claim_no_instrumentation(self):
        item = dict(variant="candidate", route="project", chunk=4096)
        validate_sample(sample(), item)
        for field, value in (("allocator_wrapper_compiled", True), ("allocator_wrapper_compiled", None),
                             ("counter_off_is_uninstrumented", True), ("state_fnv1a64", "wrong"),
                             ("interval_cpu_seconds", .1), ("interval_wall_seconds", float("nan")),
                             ("interval_wall_seconds", 60), ("ordinary_allocations", 0),
                             ("bytes_per_round", 77999), ("wall_gate_every_iterations", 1),
                             ("cpu_gate_min_wall_spacing_seconds", .01), ("cpu_clock", "unknown"),
                             ("wall_clock", "unknown")):
            with self.subTest(field=field, value=value):
                broken = sample()
                broken[field] = value
                with self.assertRaises(ValueError):
                    validate_sample(broken, item)

    def test_projection_switch_is_independent_of_allocator_build(self):
        for wrapped in (False, True):
            result = sample(observe=True, wrapped=wrapped)
            expected = expected_sample(dict(variant="candidate", route="project", chunk=4096))
            expected.update(observe=1, allocator_wrapper_compiled=wrapped)
            validate_result(result, CPU_SECONDS, expected)
            if wrapped:
                result["ordinary_allocations"] = None
                with self.assertRaises((ValueError, TypeError)):
                    validate_result(result, CPU_SECONDS, expected)

    def test_legacy_probes_remain_wrapped_and_absent_provenance_is_not_accepted(self):
        result = sample()
        del result["allocator_wrapper_compiled"]
        expected = expected_sample(dict(variant="candidate", route="project", chunk=4096))
        with self.assertRaises(ValueError):
            validate_result(result, CPU_SECONDS, expected)
        del expected["allocator_wrapper_compiled"]
        validate_result(result, CPU_SECONDS, expected)
        result["allocator_wrapper_compiled"] = False
        with self.assertRaises(ValueError):
            validate_result(result, CPU_SECONDS, expected)


class OptionalAllocatorBuildSmoke(unittest.TestCase):
    """Opt-in sanity on two actually built modes; not part of pure plan checks."""
    def test_default_and_absent_wrapper_identity(self):
        paths = [os.environ.get(name) for name in ("TERMFORGE_R54_WRAPPED", "TERMFORGE_R54_UNWRAPPED")]
        if not all(paths):
            self.skipTest("requires explicit default-feature and no-default-feature measurement binaries")
        for path, wrapped in zip(paths, (True, False)):
            response = run_command([path, "--identity"], timeout=10)
            self.assertEqual(response["exit_code"], 0, response)
            identity = json.loads(response["stdout"])
            self.assertIs(identity["allocator_wrapper_compiled"], wrapped)
            self.assertTrue(identity["timing_gates_compiled"])
            for route in ("project", "interactive"):
                for observe in (0, 1):
                    response = run_command([path, route, "4096", str(observe), ".1"], timeout=90)
                    self.assertEqual(response["exit_code"], 0, response)
                    expected = expected_sample(dict(variant="candidate", route=route, chunk=4096))
                    expected.update(observe=observe, allocator_wrapper_compiled=wrapped)
                    validate_result(json.loads(response["stdout"]), .1, expected)


if __name__ == "__main__":
    unittest.main()
