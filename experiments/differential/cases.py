"""Original synthetic cases; expected differences stay visible, never silently skipped."""
from dataclasses import dataclass
import random


@dataclass(frozen=True)
class Case:
    name: str
    text: str
    rows: int = 3
    columns: int = 12
    history: int = 3
    difference: str | None = None

    @property
    def data(self):
        return self.text.encode("utf-8")


def cases():
    rng = random.Random(20261022)
    # No margins/wide-cell overwrite in this generated common-subset case.
    random_text = "".join(rng.choice(("A", "z", " ", "\r\n",
                                          "\x1b[2;3H", "\x1b[2K")) for _ in range(100))
    return [
        Case("ascii_wrap_scroll", "0123456789ABCDEF\r\nnext\r\nthird\r\nfourth\r\nfifth"),
        Case("cursor_addressing", "abcdef\x1b[2;4HX\x1b[A\x1b[2CZ\x1b[2Dq\x1b[3dY\x1b[1GB"),
        Case("cursor_defaults_clamping", "ab\x1b[99;99H\x1b[99A\x1b[99Dq\x1b[H!\x1b[;f?"),
        Case("cr_lf_bs_tab", "abc\bX\tY\rZ\r\nlast"),
        Case("erase_line", "abcdefghij\x1b[5G\x1b[K\x1b[3G\x1b[1K\x1b[2K"),
        Case("erase_display", "one\r\ntwo\r\nthree\x1b[2;2H\x1b[J\x1b[1J\x1b[2J"),
        Case("palette_bold_reset", "\x1b[1;31;44mA\x1b[22;39;49mB\x1b[91;104mC\x1b[0mD"),
        Case("styled_erase", "abc\x1b[44m\x1b[2K\x1b[0mX"),
        Case("sgr_pending_wrap", "123456789012\x1b[31mX\x1b[0m"),
        Case("unicode_split", "中A文B😀 α\r\nλè"),
        Case("combining_canonical_equivalence", "Ae\u0301o\u0308B"),
        Case("styled_wide_history", "\x1b[31m中\x1b[0m\r\nX\r\nY\r\nZ\r\nQ\r\nR"),
        Case("seeded_common_stream", random_text),
        Case("wide_at_right_margin", "12345678901中", difference="wide-margin"),
        Case("wide_lead_overwrite", "中\rX", difference="wide-overwrite"),
        Case("wide_trailer_overwrite", "中\x1b[2GX", difference="wide-overwrite"),
        Case("combining_at_pending_wrap", "12345678901e\u0301", difference="combining-wrap"),
        Case("vertical_move_at_pending_wrap", "123456789012\x1b[A", difference="vertical-wrap"),
        Case("styled_scroll_blank", "\x1b[31mA\r\nB\r\nC\r\n"),
    ]
