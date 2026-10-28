"""Strict public contract for the separate recovery-core replay diagnostic."""
from __future__ import annotations

PROTOCOL = "termforge-recovery-core-measure-v1"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def _common(value, wrapper, kind):
    require(type(value) is dict and type(wrapper) is bool, "Expected typed identity")
    expected = {
        "protocol": PROTOCOL, "kind": kind, "parse_mode": "recovery",
        "rows": 24, "columns": 80, "history_limit": 100,
        "allocator_wrapper_compiled": wrapper, "scope": "core replay only",
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
