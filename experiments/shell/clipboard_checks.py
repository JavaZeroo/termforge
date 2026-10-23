"""Independent byte oracles for hosted product clipboard-read acceptance."""
import json
from pathlib import Path


def require_hosted_clipboard(environment):
    if environment.get("GITHUB_ACTIONS") != "true" or environment.get("RUNNER_ENVIRONMENT") != "github-hosted":
        raise ValueError("system clipboard scenario requires a disposable GitHub-hosted VM")


def validate_owner_release(row: dict) -> dict | None:
    name = row["name"]
    if "owner_release" not in row:
        raise ValueError("clipboard owner release observation is missing")
    owner = row["owner_release"]
    if name == "oversize":
        if owner is not None:
            raise ValueError("oversize case must not invent a clipboard owner")
        return None
    proof_names = ("owner_entered", "requester_exited", "target_quiescent", "payload_unchanged")
    proof = owner.get("proof") if isinstance(owner, dict) else None
    if not isinstance(proof, dict) or set(proof) != set(proof_names):
        raise ValueError("clipboard owner release proof is incomplete")
    delayed = name in ("cancel", "close")
    reason = "requester-cancelled" if name == "cancel" else "target-closed" if name == "close" else "ordinary"
    if owner.get("reason") != reason or any(proof.get(field) is not delayed for field in proof_names):
        raise ValueError("clipboard requester exit was not established before the correct release")
    flags = ("publication_attempted", "publication_succeeded", "release_event", "thread_exited", "expected_requester_gone")
    integers = ("native_error", "worker_error", "secondary_native_error", "secondary_worker_error")
    if (any(type(owner.get(field)) is not bool for field in flags) or
            any(type(owner.get(field)) is not int or not 0 <= owner[field] <= 0xffffffff for field in integers) or
            not isinstance(owner.get("failure_step"), str) or not isinstance(owner.get("secondary_failure_step"), str) or
            owner["publication_attempted"] is not delayed or not owner["release_event"] or not owner["thread_exited"] or
            owner["secondary_failure_step"] != "" or owner["secondary_native_error"] or owner["secondary_worker_error"]):
        raise ValueError("clipboard owner publication, real release, exit or secondary error check failed")
    actual = (owner["publication_succeeded"], owner["failure_step"], owner["native_error"],
              owner["worker_error"], owner["expected_requester_gone"])
    allowed = {(False, "", 0, 0, False)} if not delayed else {
        (True, "", 0, 0, False),
        (False, "SetClipboardData-render", 1418, 1418, True),
    }
    if actual not in allowed:
        raise ValueError("unexpected clipboard owner error or falsely successful late publication")
    return {"reason": reason, "publication_succeeded": owner["publication_succeeded"],
            "expected_requester_gone": owner["expected_requester_gone"], "native_error": owner["native_error"],
            "release_event": True, "thread_exited": True}


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
        owner = validate_owner_release(row)
        # Literal ASCII oracle is independent of the C++ encoder and fixture.
        expected = b"B" * 256 + (b"" if name == "close" else b"RETRY_" + name.encode("ascii"))
        received = (directory / f"{name}.bin").read_bytes()
        if received != expected or len(received) != row["receiver_bytes"]:
            raise ValueError("clipboard receiver contains missing, late, duplicated or Escape bytes")
        if name in ("occupied", "oversize"):
            frame = (directory / f"{name}-error.bmp").read_bytes()
            if len(frame) < 54 or frame[:2] != b"BM":
                raise ValueError("visible clipboard error frame is missing")
        summary[name] = {"received": len(received), "helper_exited": True, "retry": name != "close", "owner_release": owner}
    return summary
