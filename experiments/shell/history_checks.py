"""Independent numbered-output oracle for the real cmd history workflow."""
import re


def validate_native_scroll(rows):
    """Require the OS tracker path in addition to the numbered-content oracle."""
    if not isinstance(rows, list) or len(rows) != 2:
        raise ValueError("missing two native scrollbar gestures")
    for row in rows:
        if (not isinstance(row, dict) or row.get("source") != "native-nonclient-tracker" or
                any(row.get(key) is not True for key in ("non_input_desktop", "queued_mouse_messages",
                                                         "nonclient_down", "native_command")) or
                row.get("physical_mouse") is not False):
            raise ValueError("scrollbar gesture did not exercise the isolated native tracker")
        fields = ("initial", "requested", "final", "minimum", "maximum", "tracks", "positions", "ends", "moves", "system_command")
        if any(type(row.get(key)) is not int for key in fields):
            raise ValueError("invalid native scrollbar measurements")
        if (not 0 <= row["initial"] <= 100 or row["initial"] == 50 or
                row["requested"] != 50 or row["final"] != 50 or row["minimum"] != 0 or row["maximum"] != 100 or
                not 2 <= row["tracks"] <= 63 or row["positions"] != 1 or row["ends"] != 1 or
                not 1 <= row["moves"] <= 20 or (row["system_command"] & 0xfff0) not in (0xf070, 0xf080)):
            raise ValueError("native scrollbar tracker did not complete its requested movement")
        tracked = row.get("tracked")
        if (not isinstance(tracked, list) or len(tracked) != row["tracks"] + 1 or
                any(type(n) is not int or not 0 <= n <= 100 for n in tracked) or
                tracked[0] != row["initial"] or tracked[-2:] != [50, 50]):
            raise ValueError("native scrollbar notifications lack the observed move and release")


def validate_history(data):
    phases = ("live-160", "oldest-160", "page-down", "wheel-half", "wheel-up",
              "anchored-160", "hidden-after-append", "restored-tab", "evicted-500",
              "bottom-500", "before-zoom", "after-zoom", "bottom-after-zoom", "typed-return-bottom")
    if (not isinstance(data, dict) or data.get("schema") != "termforge-history-v2" or
            type(data.get("wheel_scroll_lines")) is not int or not 0 <= data["wheel_scroll_lines"] <= 0xffffffff or
            not isinstance(data.get("snapshots"), list) or
            [s.get("phase") for s in data["snapshots"] if isinstance(s, dict)] != list(phases)):
        raise ValueError("missing real history phases or wheel setting")
    by_phase = dict(zip(phases, data["snapshots"]))
    for name, last in (("live-160", 160), ("evicted-500", 500)):
        raw_hex = by_phase[name].get("raw_output_tail_hex")
        if not isinstance(raw_hex, str) or len(raw_hex) > 16384:
            raise ValueError("missing bounded raw ConPTY observation")
        raw = bytes.fromhex(raw_hex)
        # cmd emits an OSC title around echo and an extra line feed before its
        # next prompt. ConPTY can omit CR when already in column zero. Verify
        # the two line feeds independently of the screen parser.
        without_titles = re.sub(rb"\x1b\][^\x07]*?(?:\x07|\x1b\\)", b"", raw)
        if not re.search(f"TF_H_{last}".encode() + rb"\r*\n\r*\nTF>", without_titles):
            raise ValueError("raw cmd prompt separator differs from the fixture contract")
    for s in data["snapshots"]:
        if any(type(s.get(k)) is not int for k in ("top", "total", "page", "maximum", "anchor", "cursor_row")):
            raise ValueError("invalid history range types")
        if (not 1 <= s["page"] <= 200 or s["maximum"] != 100 or s["total"] != s["maximum"] + s["page"] or
                not 0 <= s["top"] <= s["maximum"] or s["anchor"] <= 0 or
                type(s.get("following")) is not bool or type(s.get("hidden")) is not bool or
                not isinstance(s.get("lines"), list) or len(s["lines"]) != s["page"] or
                any(not isinstance(line, str) for line in s["lines"])):
            raise ValueError("history bounds or snapshot contract failed")
        if s["following"]:
            if s["top"] != 100 or not 0 <= s["cursor_row"] < s["page"]:
                raise ValueError("following view does not show the live cursor")
        elif s["cursor_row"] != -1:
            raise ValueError("historical view shows an unrelated live cursor")

    def numbered(s, last):
        # The independently checked raw separator creates a blank row before
        # the prompt. Initial commands have already been evicted.
        expected = [f"TF_H_{n}" for n in range(1, last + 1)] + ["", "TF>"]
        retained = expected[-s["total"]:]
        visible = retained[s["top"]:s["top"] + s["page"]]
        if s["lines"] != visible:
            raise ValueError(f"independent numbered content mismatch: {s['phase']}")
        return visible

    for name in phases[:6]:
        numbered(by_phase[name], 160)
    if by_phase["oldest-160"]["top"] != 0 or by_phase["page-down"]["top"] != by_phase["page-down"]["page"] - 1:
        raise ValueError("top/page controls did not change the view correctly")
    if by_phase["wheel-half"]["top"] != by_phase["page-down"]["top"]:
        raise ValueError("partial wheel delta scrolled early")
    amount = data["wheel_scroll_lines"]
    if amount == 0xffffffff:
        amount = by_phase["wheel-up"]["page"] - 1
    if by_phase["wheel-up"]["top"] != max(0, by_phase["page-down"]["top"] - amount):
        raise ValueError("accumulated wheel delta did not follow the OS setting")
    anchored = by_phase["anchored-160"]
    if anchored["top"] != 50 or anchored["following"]:
        raise ValueError("native thumb control did not pause at its requested position")
    for name in ("hidden-after-append", "restored-tab"):
        s = by_phase[name]
        if s["anchor"] != anchored["anchor"] or s["lines"] != numbered(anchored, 160) or s["following"]:
            raise ValueError("append or tab switch moved the retained reading position")
    if not by_phase["hidden-after-append"]["hidden"] or by_phase["restored-tab"]["hidden"]:
        raise ValueError("hidden tab scenario did not run")
    for name in ("evicted-500", "bottom-500", "before-zoom"):
        numbered(by_phase[name], 500)
    if by_phase["evicted-500"]["top"] != 0 or by_phase["evicted-500"]["following"]:
        raise ValueError("evicted anchor did not clamp without enabling follow")
    before, after = by_phase["before-zoom"], by_phase["after-zoom"]
    expected = numbered(before, 500)
    if after["anchor"] != before["anchor"] or after["following"] or after["lines"] != expected[:after["page"]]:
        raise ValueError("zoom changed a retained historical reading position")
    if not by_phase["bottom-after-zoom"]["following"]:
        raise ValueError("return to bottom did not resume following")
    typed = by_phase["typed-return-bottom"]
    if not typed["following"] or typed["lines"][-1] != "TF>x":
        raise ValueError("ordinary input did not return to the live prompt")
