"""Pure public-protocol tests; importing this module never launches a probe."""
import json
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from experiments.measure.recovery import decode_response, validate_identity, validate_sample
from experiments.measure.recovery_sanity import run_sanity


def identity(wrapper=True):
    return {
        "protocol": "termforge-recovery-core-measure-v1", "kind": "identity",
        "parse_mode": "recovery", "rows": 24, "columns": 80, "history_limit": 100,
        "allocator_wrapper_compiled": wrapper,
        "routes": ["project", "interactive"],
        "observe_0": "feed without per-round projection",
        "observe_1": "feed with per-round projection and hash",
        "scope": "core replay only",
        "recovery_probe_passed": True,
    }


class IdentityContract(unittest.TestCase):
    def test_recovery_build_identity_must_match_the_requested_wrapper(self):
        for wrapper in (False, True):
            validate_identity(identity(wrapper), wrapper)
        for field, value in (("protocol", "legacy"), ("parse_mode", "strict"),
                             ("rows", 25), ("columns", True), ("history_limit", 0),
                             ("allocator_wrapper_compiled", 1), ("routes", ["project"]),
                             ("observe_0", "none"), ("scope", "Qt product"),
                             ("recovery_probe_passed", False)):
            broken = identity()
            broken[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_identity(broken, True)
        with self.assertRaises(ValueError):
            validate_identity(identity(False), True)


def sample(wrapper=True, observe=0, route="project"):
    value = {name: item for name, item in identity(wrapper).items()
             if name not in ("routes", "observe_0", "observe_1")}
    value.update({
        "kind": "core_cost", "valid": True, "route": route, "chunk": 4096,
        "observe": observe, "cpu_floor_seconds": .1, "rounds": 8,
        "iterations": 8, "interval_cpu_seconds": .25, "interval_wall_seconds": .3,
        "wall_gate_polls": 1, "cpu_gate_polls": 1, "wall_gate_every_iterations": 8,
        "cpu_gate_min_wall_spacing_seconds": .25, "cpu_interval_differences": 1,
        "counted_parser_destructions": 7,
        "bytes_per_round": 78000 if route == "project" else 22000,
        "workload_id": "ascii-project-78000-v1" if route == "project" else "ascii-interactive-22000-v1",
        "payload_fnv1a64": "13372643718718663029" if route == "project" else "14158139299817394597",
        "state_fnv1a64": "5455070173120736870" if route == "project" else "8292075482910706113",
        "observer_semantics": "feed with per-round projection and hash" if observe else "feed without per-round projection",
        "oracle_scope": "warmup and final; every measured projection also hashed" if observe else "warmup and final iteration",
        "ordinary_allocations": 200 if wrapper and observe else None,
        "ordinary_allocated_bytes": 6400 if wrapper and observe else None,
        "warmup_and_oracle_seconds": .01, "final_oracle_seconds": .01,
        "cpu_clock": "GetProcessTimes kernel plus user", "cpu_representation_unit_seconds": 1e-7,
        "wall_clock": "std::chrono::steady_clock", "wall_clock_is_steady": True,
        "wall_representation_unit_seconds": 1e-9,
        "peak_working_set_bytes": 4000000, "receive_seconds": None, "presented_done": None,
    })
    return value


class SampleContract(unittest.TestCase):
    def test_fixed_complete_sample_cannot_hide_changed_work_or_unknown_counters(self):
        for route in ("project", "interactive"):
            for wrapper in (False, True):
                for observe in (0, 1):
                    validate_sample(sample(wrapper, observe, route), wrapper, route, 4096, observe, .1)
        for field, value in (("parse_mode", "strict"), ("chunk", True), ("observe", True),
                             ("state_fnv1a64", "wrong"), ("payload_fnv1a64", "wrong"),
                             ("workload_id", "other"), ("bytes_per_round", 78001),
                             ("interval_cpu_seconds", .01), ("interval_wall_seconds", float("nan")),
                             ("cpu_gate_polls", 0), ("iterations", 9),
                             ("counted_parser_destructions", 8), ("presented_done", 0),
                             ("ordinary_allocations", 0), ("ordinary_allocated_bytes", 0),
                             ("observer_semantics", "none"), ("recovery_probe_passed", False)):
            broken = sample(False)
            broken[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_sample(broken, False, "project", 4096, 0, .1)
        for field, value in (("ordinary_allocations", None), ("ordinary_allocated_bytes", True)):
            broken = sample(True, 1)
            broken[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_sample(broken, True, "project", 4096, 1, .1)

    def test_missing_unknown_endpoint_and_invalid_requests_are_rejected(self):
        for name in ("receive_seconds", "presented_done", "ordinary_allocations"):
            broken = sample(False)
            del broken[name]
            with self.subTest(name=name), self.assertRaises(ValueError):
                validate_sample(broken, False, "project", 4096, 0, .1)
        for route, chunk, observe, floor in (([], 4096, 0, .1), ("project", True, 0, .1),
                                            ("project", 4096, False, .1), ("project", 4096, 0, float("nan"))):
            with self.subTest(request=(route, chunk, observe, floor)), self.assertRaises(ValueError):
                validate_sample(sample(), True, route, chunk, observe, floor)


class RawResponseContract(unittest.TestCase):
    def test_response_decoder_never_repairs_invalid_bytes_or_ambiguous_json(self):
        self.assertEqual(decode_response(b'{"parse_mode":"recovery"}'), {"parse_mode": "recovery"})
        for raw in (b'{"parse_mode":"strict","parse_mode":"recovery"}',
                    b'{"value":NaN}', b'{"value":Infinity}', b'{"value":"\xff"}', b'partial'):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                decode_response(raw)

    def test_failed_first_process_keeps_exact_raw_streams_and_stops_the_batch(self):
        failures = (SimpleNamespace(returncode=0, stdout=b'\xffnot-json', stderr=b''),
                    subprocess.TimeoutExpired(["probe"], 90, output=b'partial\xff', stderr=b'waiting'))
        for failure in failures:
            with self.subTest(failure=type(failure).__name__), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                wrapped, noalloc = root / "wrapped.exe", root / "noalloc.exe"
                wrapped.write_bytes(b"synthetic wrapped binary")
                noalloc.write_bytes(b"synthetic noalloc binary")
                options = {"side_effect": failure} if isinstance(failure, Exception) else {"return_value": failure}
                with patch("experiments.measure.recovery_sanity.subprocess.run", **options):
                    with self.assertRaises(ValueError):
                        run_sanity(wrapped, noalloc, root / "out")
                report = json.loads((root / "out/manifest.json").read_text(encoding="utf-8"))
                self.assertEqual(report["status"], "failed")
                self.assertEqual(report["attempted_core_processes"], 1)
                self.assertEqual(len(report["records"]), 1)
                self.assertEqual((root / "out/00.stdout.bin").read_bytes(),
                                 failure.output if isinstance(failure, Exception) else failure.stdout)
                self.assertEqual((root / "out/00.stderr.bin").read_bytes(), failure.stderr)
                with self.assertRaises(FileExistsError):
                    run_sanity(wrapped, noalloc, root / "out")


if __name__ == "__main__":
    unittest.main()
