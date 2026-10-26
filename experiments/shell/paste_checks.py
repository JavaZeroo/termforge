"""Independent literal payload oracles for the real HWND paste workflow."""
import json
from pathlib import Path


def validate_paste_fence(row: dict, generation: int) -> None:
    fence = row.get("fence")
    if (not isinstance(fence, dict) or type(fence.get("generation")) is not int or
            fence["generation"] != generation or fence.get("held") is not True or
            fence.get("wait_finished") is not True or fence.get("other_responsive") is not True or
            fence.get("wrong_generation_rejected") is not True or
            fence.get("exit") != ("cancelled" if row.get("name") == "cancel" else "stopped") or
            type(fence.get("owned_bytes")) is not int or not 0 < fence["owned_bytes"] <= 1024 or
            row["before_accepted"] - row["before_sent"] != fence["owned_bytes"]):
        raise ValueError("window paste sender fence identity, hold, wake or isolation evidence is incomplete")


def validate_paste_workflow(directory: Path) -> dict:
    directory = Path(directory)
    data = json.loads((directory / "paste-workflow.json").read_text(encoding="utf-8"))
    if data.get("schema") != "termforge-window-paste-v2" or data.get("keyboard") != {
        "default_enter_accept": True, "default_escape_cancel": True, "tab_enter_accept": True
    }:
        raise ValueError("window paste keyboard confirmation coverage is incomplete")
    if (directory / "input.bin").read_bytes() != b"DEFAULT_ENTER_ACCEPT\r" + b"P\xe6\xb1\x89\r" * 5000:
        raise ValueError("confirmed receiver bytes differ or rejected text leaked")
    cases = data.get("cases")
    if (not isinstance(cases, list) or len(cases) != 2 or any(not isinstance(row, dict) for row in cases) or
            [row.get("name") for row in cases] != ["cancel", "close"]):
        raise ValueError("window paste cancellation and close evidence is incomplete")
    expected = b"\xe6\xb1\x89" * 1000000
    summary = {}
    for generation, row in enumerate(cases, 1):
        integers = ("target", "total", "before_sent", "before_accepted", "accepted", "sent",
                    "in_flight", "retained_capacity", "checkpoint_bytes", "received")
        if any(type(row.get(key)) is not int or row[key] < 0 for key in integers):
            raise ValueError("invalid window paste byte accounting")
        if (row["target"] <= 0 or row["total"] != len(expected) or
                not 16384 <= row["before_sent"] <= row["before_accepted"] < row["total"] or
                not row["before_sent"] <= row["sent"] <= row["accepted"] < row["total"] or
                row["accepted"] < row["before_accepted"] or
                row["in_flight"] or row["retained_capacity"] > 32 or row["checkpoint_bytes"] != 768 or
                row.get("flood_observed") is not True or row.get("state") not in ("cancelled", "failed")):
            raise ValueError("window paste did not remain partial, bounded and terminal under output flood")
        validate_paste_fence(row, generation)
        received = (directory / f"{row['name']}.bin").read_bytes()
        if (not received or len(received) != row["received"] or len(received) > row["sent"] or
                received != expected[:len(received)]):
            raise ValueError("window paste receiver differs from independent exact prefix")
        received.decode("utf-8", errors="strict")
        if row["name"] == "cancel":
            if row["state"] != "cancelled" or not len(received) == row["sent"] == row["accepted"]:
                raise ValueError("Escape cancellation left hidden or unconsumed submitted bytes")
        elif received != b"\xe6\xb1\x89" * 256:
            raise ValueError("closed receiver consumed beyond its held checkpoint")
        summary[row["name"]] = {"sent": row["sent"], "received": len(received), "state": row["state"]}
    return summary
