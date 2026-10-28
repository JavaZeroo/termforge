"""Strict public contract for the separate recovery-core replay diagnostic."""
from __future__ import annotations
import math

PROTOCOL = "termforge-recovery-core-measure-v1"
WORKLOADS = {
    "project": {"id": "ascii-project-78000-v1", "bytes": 78000,
                "payload_fnv1a64": "13372643718718663029", "state_fnv1a64": "5455070173120736870"},
    "interactive": {"id": "ascii-interactive-22000-v1", "bytes": 22000,
                    "payload_fnv1a64": "14158139299817394597", "state_fnv1a64": "8292075482910706113"},
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def _common(value, wrapper, kind):
    require(type(value) is dict and type(wrapper) is bool, "Expected typed identity")
    expected = {
        "protocol": PROTOCOL, "kind": kind, "parse_mode": "recovery",
        "rows": 24, "columns": 80, "history_limit": 100,
        "allocator_wrapper_compiled": wrapper, "scope": "core replay only",
        "recovery_probe_passed": True,
    }
    for name, expected_value in expected.items():
        actual = value.get(name)
        require(type(actual) is type(expected_value) and actual == expected_value,
                f"Recovery identity differs: {name}")


def validate_identity(value, wrapper):
    """Reject the wrong parser, layout or allocator build before starting work."""
    _common(value, wrapper, "identity")
    require(value.get("routes") == ["project", "interactive"], "Recovery routes differ")
    require(value.get("observe_0") == "feed without per-round projection" and
            value.get("observe_1") == "feed with per-round projection and hash",
            "Observer semantics differ")


def _integer(value, minimum=0):
    return type(value) is int and value >= minimum


def _finite(value, minimum=0):
    return type(value) in (int, float) and math.isfinite(value) and value >= minimum


def validate_sample(value, wrapper, route, chunk, observe, floor):
    """Validate one fixed replay; this does not judge speed or the Qt product."""
    require(route in WORKLOADS and _integer(chunk, 1) and chunk <= 1048576 and
            type(observe) is int and observe in (0, 1) and _finite(floor, .1) and floor <= 10,
            "Invalid requested recovery sample")
    _common(value, wrapper, "core_cost")
    require(value.get("valid") is True, "Recovery oracle did not pass")
    workload = WORKLOADS[route]
    expected = {"route": route, "chunk": chunk, "observe": observe,
                "bytes_per_round": workload["bytes"], "workload_id": workload["id"],
                "payload_fnv1a64": workload["payload_fnv1a64"], "state_fnv1a64": workload["state_fnv1a64"],
                "observer_semantics": "feed with per-round projection and hash" if observe else "feed without per-round projection",
                "oracle_scope": "warmup and final; every measured projection also hashed" if observe else "warmup and final iteration"}
    for name, expected_value in expected.items():
        actual = value.get(name)
        require(type(actual) is type(expected_value) and actual == expected_value,
                f"Recovery sample differs: {name}")
    require(_finite(value.get("cpu_floor_seconds"), .1) and value["cpu_floor_seconds"] == floor,
            "CPU floor differs")
    require(all(_finite(value.get(name), floor) for name in ("interval_cpu_seconds", "interval_wall_seconds")),
            "Continuous interval did not meet the requested floor")
    require(all(_integer(value.get(name), 1) for name in ("rounds", "iterations", "wall_gate_polls", "cpu_gate_polls")),
            "Missing interval progress")
    require(value["iterations"] == value["rounds"] == 8 * value["wall_gate_polls"] and
            type(value.get("wall_gate_every_iterations")) is int and value["wall_gate_every_iterations"] == 8 and
            type(value.get("cpu_interval_differences")) is int and value["cpu_interval_differences"] == 1 and
            _integer(value.get("counted_parser_destructions")) and
            value["counted_parser_destructions"] == value["rounds"] - 1 and
            _finite(value.get("cpu_gate_min_wall_spacing_seconds")) and value["cpu_gate_min_wall_spacing_seconds"] == .25,
            "Continuous interval endpoint differs")
    require(all(_finite(value.get(name)) for name in ("warmup_and_oracle_seconds", "final_oracle_seconds")),
            "Invalid outside-interval timing")
    require(value.get("cpu_clock") in ("GetProcessTimes kernel plus user", "std::clock process CPU") and
            value.get("wall_clock") == "std::chrono::steady_clock" and value.get("wall_clock_is_steady") is True and
            all(_finite(value.get(name)) and value[name] > 0 for name in
                ("cpu_representation_unit_seconds", "wall_representation_unit_seconds")),
            "Clock identity differs")
    for name in ("ordinary_allocations", "ordinary_allocated_bytes"):
        require(name in value and (_integer(value[name], 1) if wrapper and observe else value[name] is None),
                "Allocation metric does not match this observer/build")
    require("peak_working_set_bytes" in value and
            (value["peak_working_set_bytes"] is None or _integer(value["peak_working_set_bytes"], 1)),
            "Invalid process peak memory")
    require(all(name in value and value[name] is None for name in ("receive_seconds", "presented_done")),
            "Unmeasured endpoint must be explicit unknown")
