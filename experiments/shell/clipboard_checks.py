"""Independent byte oracles for hosted product clipboard-read acceptance."""
import json
from pathlib import Path


def require_hosted_clipboard(environment):
    if environment.get("GITHUB_ACTIONS") != "true" or environment.get("RUNNER_ENVIRONMENT") != "github-hosted":
        raise ValueError("system clipboard scenario requires a disposable GitHub-hosted VM")


def validate_clipboard_workflow(directory: Path) -> dict:
    directory = Path(directory)
    data = json.loads((directory / "clipboard-workflow.json").read_text(encoding="utf-8"))
    names = ("occupied", "oversize", "cancel", "close")
    cases = data.get("cases")
    if (data.get("schema") != "termforge-window-clipboard-v1" or data.get("disposable_hosted") is not True or
            not isinstance(cases, list) or len(cases) != 4 or any(not isinstance(row, dict) for row in cases) or
            [row.get("name") for row in cases] != list(names)):
        raise ValueError("product clipboard read coverage is incomplete")
    summary = {}
    for row in cases:
        name = row["name"]
        if (row.get("action") != "product-paste-command" or
                type(row.get("baseline_bytes")) is not int or row["baseline_bytes"] != 256 or
                type(row.get("helper_pid")) is not int or row["helper_pid"] <= 0 or
                type(row.get("receiver_bytes")) is not int or row["receiver_bytes"] < 256):
            raise ValueError("clipboard product action, helper or baseline is missing")
        expected_flags = {
            "owner_entered": name in ("cancel", "close"),
            "prompt_seen": False, "transfer_seen": False,
            "other_during": True, "other_after": True,
            "error_visible": name in ("occupied", "oversize"),
            "helper_exited": True, "busy_cleared": True,
            "target_closed": name == "close", "retry_verified": name != "close",
        }
        if any(row.get(field) is not value for field, value in expected_flags.items()):
            raise ValueError("clipboard read action, responsiveness, cancellation or cleanup gate failed")
        # Literal ASCII oracle is independent of the C++ encoder and fixture.
        expected = b"B" * 256 + (b"" if name == "close" else b"RETRY_" + name.encode("ascii"))
        received = (directory / f"{name}.bin").read_bytes()
        if received != expected or len(received) != row["receiver_bytes"]:
            raise ValueError("clipboard receiver contains missing, late, duplicated or Escape bytes")
        if name in ("occupied", "oversize"):
            frame = (directory / f"{name}-error.bmp").read_bytes()
            if len(frame) < 54 or frame[:2] != b"BM":
                raise ValueError("visible clipboard error frame is missing")
        summary[name] = {"received": len(received), "helper_exited": True, "retry": name != "close"}
    return summary
