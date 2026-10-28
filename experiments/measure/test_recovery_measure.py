"""Pure public-protocol tests; importing this module never launches a probe."""
import copy
import unittest

from experiments.measure.recovery import validate_identity


def identity(wrapper=True):
    return {
        "protocol": "termforge-recovery-core-measure-v1", "kind": "identity",
        "parse_mode": "recovery", "rows": 24, "columns": 80, "history_limit": 100,
        "allocator_wrapper_compiled": wrapper,
        "routes": ["project", "interactive"],
        "observe_0": "feed without per-round projection",
        "observe_1": "feed with per-round projection and hash",
        "scope": "core replay only",
    }


class IdentityContract(unittest.TestCase):
    def test_recovery_build_identity_must_match_the_requested_wrapper(self):
        for wrapper in (False, True):
            validate_identity(identity(wrapper), wrapper)
        for field, value in (("protocol", "legacy"), ("parse_mode", "strict"),
                             ("rows", 25), ("columns", True), ("history_limit", 0),
                             ("allocator_wrapper_compiled", 1), ("routes", ["project"]),
                             ("observe_0", "none"), ("scope", "Qt product")):
            broken = identity()
            broken[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_identity(broken, True)
        with self.assertRaises(ValueError):
            validate_identity(identity(False), True)


if __name__ == "__main__":
    unittest.main()
