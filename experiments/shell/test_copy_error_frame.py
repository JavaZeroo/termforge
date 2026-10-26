"""Synthetic bitmap/geometry protocol tests; these are not native clipboard evidence."""
import copy
import struct
import unittest

from experiments.shell import selection_checks as oracle


def geometry(width, height):
    return {"schema": "termforge-copy-error-client-geometry-v1", "coordinate_space": "physical-client-pixels",
            "client_rect": [0, 0, width, height], "client_rect_after": [0, 0, width, height],
            "physical_size": [width, height], "bitmap_size": [width, height],
            "bitmap_stride_bytes": width * 4, "bitmap_format": [1, 32]}


def contention(width=1008, height=681):
    return {"schema": "termforge-product-copy-contention-v2", "error_frame_geometry": geometry(width, height)}


def bitmap(width, height):
    size = 54 + width * height * 4
    return struct.pack("<2sIHHIIiiHHIIiiII", b"BM", size, 0, 0, 54, 40, width, -height,
                       1, 32, 0, 0, 0, 0, 0, 0) + bytes(size - 54)


class CopyErrorFrameOracleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.short = bitmap(1008, 681)
        cls.original = bitmap(1360, 860)

    def test_v2_requires_native_geometry_matching_every_bitmap_byte(self):
        for width, height, frame in ((1008, 681, self.short), (1360, 860, self.original)):
            self.assertEqual(oracle._validate_copy_error_frame(contention(width, height), frame),
                             (width, height))

    def test_legacy_v1_keeps_its_original_fixed_geometry_and_cannot_claim_v2_metadata(self):
        legacy = {"schema": "termforge-product-copy-contention-v1"}
        self.assertEqual(oracle._validate_copy_error_frame(legacy, self.original), (1360, 860))
        for value, frame in ((legacy, self.short), ({**legacy, "error_frame_geometry": geometry(1360, 860)}, self.original)):
            with self.assertRaises(ValueError):
                oracle._validate_copy_error_frame(value, frame)

    def test_missing_or_conflicting_independent_geometry_is_rejected(self):
        valid = contention()
        bad = copy.deepcopy(valid); del bad["error_frame_geometry"]
        with self.assertRaises(ValueError): oracle._validate_copy_error_frame(bad, self.short)
        for field, wrong in (("schema", "unobserved"), ("coordinate_space", "logical-client-dips"),
                             ("client_rect", [0, 0, 1009, 681]), ("client_rect", [1, 0, 1008, 681]),
                             ("client_rect", [0, 0, True, 681]), ("client_rect", [0, 0, 6021, 681]),
                             ("client_rect_after", [0, 0, 1008, 682]), ("physical_size", [1008, 682]),
                             ("bitmap_size", [1009, 681]), ("bitmap_stride_bytes", 4036),
                             ("bitmap_format", [1, 24])):
            with self.subTest(field=field, wrong=wrong):
                bad = copy.deepcopy(valid); bad["error_frame_geometry"][field] = wrong
                with self.assertRaises(ValueError): oracle._validate_copy_error_frame(bad, self.short)
        for field in valid["error_frame_geometry"]:
            with self.subTest(missing=field):
                bad = copy.deepcopy(valid); del bad["error_frame_geometry"][field]
                with self.assertRaises(ValueError): oracle._validate_copy_error_frame(bad, self.short)

    def test_truncation_extra_payload_and_changed_native_bmp_header_are_rejected(self):
        for frame in (self.short[:53], self.short[:-1], self.short + b"\0"):
            with self.assertRaises(ValueError): oracle._validate_copy_error_frame(contention(), frame)
        fields = list(struct.unpack_from("<2sIHHIIiiHHIIiiII", self.short))
        for index, wrong in ((0, b"XX"), (1, len(self.short)-4), (2, 1), (3, 1), (4, 58),
                             (5, 124), (6, 1009), (7, 681), (8, 2), (9, 24), (10, 1), (11, 4032*681)):
            with self.subTest(header_field=index):
                changed = fields.copy(); changed[index] = wrong
                frame = struct.pack("<2sIHHIIiiHHIIiiII", *changed) + self.short[54:]
                with self.assertRaises(ValueError): oracle._validate_copy_error_frame(contention(), frame)


if __name__ == "__main__": unittest.main()
