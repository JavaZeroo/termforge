"""Validate and whitelist an owned R1 reference observation; never infer missing gates."""
from __future__ import annotations
import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

EXPECTED_EXE = "7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0"
EXPECTED_FILES = "e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203"
SELECTION = "ALPHA  tail\nBETA 中文 e\u0301"
SINGLE = "TF-SINGLE"
MULTILINE = "TF-FIRST\rTF-SECOND\rTF-THIRD"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def menu_action_id(text):
    if not isinstance(text, str):
        return None
    label = re.sub(r"\(&.\)", "", text.split("\t")[0]).replace("&", "").strip().rstrip(".…").lower()
    return {"copy": "copy", "复制": "copy", "paste": "paste", "粘贴": "paste",
            "find": "find", "查找": "find", "select all": "select-all", "全选": "select-all"}.get(label)


def r1_menu_actions(actions):
    return [{k: action.get(k) for k in ("text", "name", "enabled", "submenu")}
            for action in actions if menu_action_id(action.get("text", ""))]


def validate_paste(phases, baseline, *, peer_field="received", encode=lambda text: text):
    """Use the pre-action prefix, never a prefix sampled after opening a dialog."""
    require(isinstance(baseline, str), "Missing pre-paste peer baseline")
    expected = {
        "paste-before": (baseline, None, "ScintillaEditBase"),
        "paste-single-delivered": (baseline + encode(SINGLE), None, "ScintillaEditBase"),
        "paste-single-after-escape": (baseline + encode(SINGLE), None, "ScintillaEditBase"),
        "paste-multiline": (baseline + encode(SINGLE), "PasteDialog", "QPushButton"),
        "paste-multiline-cancel": (baseline + encode(SINGLE), None, "ScintillaEditBase"),
        "paste-multiline-confirm": (baseline + encode(SINGLE), "PasteDialog", "QPushButton"),
        "paste-multiline-accepted": (baseline + encode(SINGLE + MULTILINE), None, "ScintillaEditBase"),
    }
    for key, (received, modal, focus) in expected.items():
        require(key in phases and isinstance(phases[key].get("peer"), dict), "Missing paste outcome: " + key)
        value = phases[key]
        require(value["peer"].get(peer_field) == received, "Paste peer bytes differ: " + key)
        require("modal" in value and value["modal"] == modal, "Paste confirmation state differs: " + key)
        require(value.get("focus") == focus, "Paste focus differs: " + key)
    order = [list(phases).index(key) for key in expected]
    require(order == sorted(order), "Paste observation order differs")
    return {"single_line_confirmation": False, "single_line_sent": SINGLE,
            "single_line_escape_after_send": True, "multiline_confirmation": "PasteDialog",
            "before_confirmation_preserved_peer": True, "cancel_preserved_peer": True,
            "confirmation_closed": True, "paste_accepted": MULTILINE}


def summarize(identity, evidence, cleanup, profile, script_digest, final_peer=None):
    require(identity.get("sha256") == EXPECTED_EXE, "Unpinned target")
    require(identity.get("files_sha256") == EXPECTED_FILES, "Unpinned distribution")
    require(identity.get("profile") == "synthetic-local-only", "Non-synthetic profile")
    require(identity.get("clipboard") in ("owned-window-station", "hosted-disposable-vm"), "Clipboard isolation unknown")
    require(cleanup.get("active") == 0, "Owned processes remain")
    require(profile == {"only_synthetic_session": True, "session_count": 1}, "Profile scope changed")
    require(evidence.get("ok") is True, "REA invocation failed")
    record = evidence["data"]["evidence"]
    require(record.get("operation") == "instrument_with_frida", "Wrong observation operation")
    result = record["normalized_result"]
    require(result.get("source_sha256") == identity.get("script_sha256") == script_digest, "Script identity mismatch")
    require(result.get("pid") == identity.get("pid"), "Wrong owned process")
    require(not result.get("messages_truncated"), "Truncated observation")
    require(result.get("cleanup_error") is None and evidence["data"].get("cleanup_error") is None, "Frida cleanup failed")
    messages = result["messages"]
    require(not any(m.get("type") == "error" or m.get("payload", {}).get("error") for m in messages), "Probe contains an error")
    payloads = [m["payload"] for m in messages if m.get("type") == "send"]
    complete = [p for p in payloads if p.get("kind") == "scenario_complete"]
    require(len(complete) == 1, "Scenario did not complete exactly once")
    snapshots = [p for p in payloads if p.get("kind") == "snapshot"]
    phases = {p["phase"]: p for p in snapshots}
    require(len(phases) == len(snapshots), "Duplicate scenario phase")
    required = ("session", "select-all", "find-open", "find-typed", "find-needle-enter", "find-f3", "find-f3-wrap", "find-shift-f3",
                "find-escape", "history-home", "history-after-output", "history-end", "shift-page-up",
                "shift-page-down", "doubleclick-word", "tripleclick-line", "before-typing", "after-typing",
                "select-word-api", "select-line-api", "find-unicode", "drag-unicode", "reverse-drag-unicode")
    require(all(p in phases for p in required), "Missing navigation/selection phase")
    states = {key: json.loads(phases[key]["text_state"]) for key in required}
    initial = states["session"]
    require(initial["lines"] > initial["capacity"], "Fixture did not create history")
    require(states["history-home"]["scroll"] == 0, "History Home failed")
    require(states["history-after-output"]["lines"] == states["history-home"]["lines"] + 20, "Append fixture missing")
    require(states["history-after-output"]["scroll"] == 0, "Output changed paused anchor")
    require(states["history-end"]["scroll"] == states["history-end"]["lines"] - states["history-end"]["capacity"], "History End failed")
    require(states["before-typing"]["scroll"] == 0, "Typing test did not start paused")
    after = states["after-typing"]
    require(after["lines"] == states["before-typing"]["lines"] and after["scroll"] == after["lines"] - after["capacity"], "Typing follow state differs")
    require(phases["after-typing"].get("peer", {}).get("received", "").endswith("x"), "Peer did not receive typed x")
    require(phases["find-open"]["focus"] == "ComboBox" and phases["find-escape"]["focus"] == "ScintillaEditBase", "Find focus contract differs")
    for key in ("find-needle-enter", "find-f3", "find-shift-f3"):
        require(states[key]["selection"] == "needle", "Search did not select fixture match")
    require(states["find-typed"]["anchor"] == 79 * 34 + 21 and states["find-needle-enter"]["anchor"] == 78 * 34 + 21, "Incremental search / Enter direction differs")
    require(states["find-f3"]["anchor"] == 79 * 34 + 21 and states["find-f3-wrap"]["anchor"] == 21 and states["find-shift-f3"]["anchor"] == 79 * 34 + 21, "Search direction/wrap differs")
    require(states["find-unicode"]["selection"] == "中文", "Unicode search differs")
    require(states["doubleclick-word"]["selection"] == "TF-HISTORY-000", "Mouse word selection differs")
    require(states["tripleclick-line"]["selection"] == "TF-HISTORY-000 alpha needle omega", "Mouse line selection differs")
    require(states["select-line-api"]["selection"] == "TF-HISTORY-000 alpha needle omega\n", "API line selection differs")
    for key in ("drag-unicode", "reverse-drag-unicode"):
        require(states[key]["selection"] == SELECTION, "Mouse drag text differs")
    core = {key: {"focus": phases[key]["focus"], **states[key]} for key in required}
    # The all-history content is synthetic, but a short projection is sufficient.
    core["select-all"]["selection"] = states["select-all"]["selection"][-300:]
    peer_text = phases["after-typing"]["peer"]["received"]
    require("needle" not in peer_text and "中文" not in peer_text, "Find input leaked to peer")
    full = complete[0].get("coverage") == "navigation-selection-system-clipboard-paste"
    clipboard = {"verified": False, "reason": "requires-disposable-hosted-vm"}
    if full:
        require(identity["clipboard"] == "hosted-disposable-vm", "Full clipboard run lacks controlled VM")
        copies = [p["system_clipboard"] for p in payloads if p.get("kind") == "copy-system"]
        require(len(copies) == 1 and copies[0] == SELECTION.replace("\n", "\r\n"), "CF_UNICODETEXT copy differs")
        before = phases["reverse-drag-unicode"]["peer"]["received"]
        require(before == peer_text, "Input leaked before paste")
        clipboard = {"verified": True, "copy": copies[0], **validate_paste(phases, before)}
        require(isinstance(final_peer, dict) and final_peer.get("received") == before + SINGLE + MULTILINE,
                "Final peer has missing or late paste bytes")
    return {"schema": "termforge.r1-reference.v1", "target_sha256": EXPECTED_EXE,
            "distribution_sha256": EXPECTED_FILES, "fixture_sha256": identity.get("fixture_sha256"),
            "template_sha256": identity.get("template_sha256"), "evidence_id": record["evidence_id"],
            "complete": full, "navigation_selection": core, "system_clipboard": clipboard,
            "cleanup": {"owned_active": 0, "only_synthetic_session": True},
            "limitations": ["Controlled Qt event replay on an owned desktop; not physical input or performance evidence.",
                            "Shell fixture uses the reference local Windows transport; no claim about remote VT soft wraps."]}


def read_run(directory):
    require(not (directory / "launcher-failure.json").exists(), "Launcher failure invalidates the run")
    read = lambda name: json.loads((directory / name).read_text(encoding="utf-8"))
    if read("run.json").get("scenario") == "action-context":
        from research.ui.r1_action_context import summarize_context
        return summarize_context(read("run.json"), read("evidence.json"), read("cleanup.json"), read("profile-check.json"),
                                 hashlib.sha256((directory / "probe.js").read_bytes()).hexdigest(), read("peer.json"))
    if read("run.json").get("scenario") == "menu-wrap":
        return summarize_menu(read("run.json"), read("evidence.json"), read("cleanup.json"), read("profile-check.json"),
                              hashlib.sha256((directory / "probe.js").read_bytes()).hexdigest(), read("peer.json"))
    return summarize(read("run.json"), read("evidence.json"), read("cleanup.json"), read("profile-check.json"),
                     hashlib.sha256((directory / "probe.js").read_bytes()).hexdigest(), read("peer.json"))


def compare_runs(first, second):
    """Compare validated behavior, leaving run-specific history size/PIDs outside it."""
    for key in ("schema", "target_sha256", "distribution_sha256", "fixture_sha256", "template_sha256"):
        require(first.get(key) is not None and first.get(key) == second.get(key), "Repeat identity differs: " + key)
    require(isinstance(first.get("evidence_id"), str) and isinstance(second.get("evidence_id"), str)
            and first["evidence_id"] and second["evidence_id"] and first["evidence_id"] != second["evidence_id"],
            "Repeat must use distinct observations")
    require(first.get("system_clipboard", {}).get("verified") is True and second.get("system_clipboard", {}).get("verified") is True,
            "Repeat lacks system clipboard coverage")
    require(first["system_clipboard"] == second["system_clipboard"], "Repeated clipboard/paste behavior differs")
    if first["schema"] == "termforge.r1-action-context.v1":
        require(first.get("context_verified") is True and second.get("context_verified") is True, "Repeat lacks action context")
        for key in ("find_edits", "actions", "confirmation", "vt"):
            require(first[key] == second[key], "Repeated action context differs: " + key)
    elif first["schema"] == "termforge.r1-menu-wrap.v1":
        require(first.get("menu_verified") is True and second.get("menu_verified") is True, "Repeat lacks all menu actions")
        require(first["vt"] == second["vt"] and first["activations"] == second["activations"], "Repeated menu/VT behavior differs")
    else:
        require(first["schema"] == "termforge.r1-reference.v1" and first.get("complete") is True and second.get("complete") is True,
                "Repeat lacks text contract")
        for phase in ("find-typed", "find-needle-enter", "find-f3", "find-f3-wrap", "find-shift-f3", "find-unicode",
                      "doubleclick-word", "tripleclick-line", "select-line-api", "drag-unicode", "reverse-drag-unicode"):
            for field in ("selection", "anchor", "focus"):
                require(first["navigation_selection"][phase][field] == second["navigation_selection"][phase][field], "Repeated text behavior differs")
    return {"verified": True, "first_evidence_id": first["evidence_id"], "second_evidence_id": second["evidence_id"]}


def summarize_menu(identity, evidence, cleanup, profile, script_digest, peer):
    require(identity.get("sha256") == EXPECTED_EXE and identity.get("files_sha256") == EXPECTED_FILES, "Unpinned target")
    require(identity.get("profile") == "synthetic-local-only" and identity.get("scenario") == "menu-wrap", "Unexpected scenario")
    require(cleanup.get("active") == 0 and peer.get("closed") is True, "Owned resources remain")
    require(profile == {"only_synthetic_session": True, "session_count": 1}, "Profile scope changed")
    require(evidence.get("ok") is True, "REA invocation failed")
    require(evidence.get("data", {}).get("cleanup_error") is None, "REA cleanup failed")
    record = evidence["data"]["evidence"]
    result = record["normalized_result"]
    require(record.get("operation") == "instrument_with_frida" and result.get("pid") == identity.get("pid"), "Wrong observation")
    require(result.get("source_sha256") == identity.get("script_sha256") == script_digest, "Script identity mismatch")
    require(not result.get("messages_truncated") and result.get("cleanup_error") is None, "Incomplete evidence")
    messages = result["messages"]
    payloads = [m["payload"] for m in messages if m.get("type") == "send"]
    snapshots = [p for p in payloads if p.get("kind") == "snapshot"]
    phases = {p["phase"]: p for p in snapshots}
    require(len(phases) == len(snapshots), "Duplicate scenario phase")
    require("vt-select-all" in phases, "No VT selection observation")
    columns = peer.get("sent_columns")
    require(peer.get("connected") and not peer.get("error") and isinstance(columns, int) and 40 <= columns <= 300, "Invalid peer observation")
    soft = "TF-SOFT:" + "s" * (columns - 12) + "LEFTRIGHT:END"
    hard = "TF-HARD:" + "h" * (columns - 12) + "LEFT\nRIGHT:END"
    expected = soft + "\n" + hard + "\nTF-TAIL:two  \nTF-UNICODE:中文 e\u0301\nTF-VT-END\n"
    sent = ("\x1b[2J\x1b[H\x1b[?7h" + expected.replace("\n", "\r\n")).encode("utf-8")
    require(peer.get("sent_sha256") == hashlib.sha256(sent).hexdigest(), "Unexpected bytes sent to target")
    selection = json.loads(phases["vt-select-all"]["text_state"])["selection"]
    require(selection == expected, "VT soft/hard breaks, whitespace or Unicode differ")
    search = {}
    for phase in ("vt-soft-search", "vt-soft-search-wrap"):
        if phase in phases:
            state = json.loads(phases[phase]["text_state"])
            require(state["selection"] == "LEFTRIGHT" and state["anchor"] == columns - 4, "Soft-wrap search differs")
            search[phase] = state
    actions = [{k: p.get(k) for k in ("action", "text", "name", "input")} for p in payloads if p.get("kind") == "menu_activation"]
    menus = [{"phase": p["phase"], "actions": r1_menu_actions(p["actions"])} for p in payloads if p.get("kind") == "menu"]
    successful = [p for p in payloads if p.get("kind") == "scenario_complete"]
    errors = any(m.get("type") == "error" or m.get("payload", {}).get("error") for m in messages)
    selection_find_verified = len(successful) == 1 and not errors and successful[0].get("coverage") in ("menu-wrap-only", "menu-wrap-system-clipboard-paste")
    full = selection_find_verified and successful[0].get("coverage") == "menu-wrap-system-clipboard-paste"
    if selection_find_verified:
        require(len(actions) >= 2 and len(search) == 2, "Incomplete menu actions")
        require([menu_action_id(a["text"]) for a in actions[:2]] == ["select-all", "find"], "Wrong menu entries activated")
        require(json.loads(phases["before-context"]["text_state"])["selection"] != expected, "Menu test lacked a distinct initial selection")
        require(json.loads(phases["menu-select-all"]["text_state"])["selection"] == expected, "Menu selection differs")
        require(phases["menu-find-open"]["focus"] == "ComboBox", "Menu find focus differs")
        require(json.loads(phases["menu-find-result"]["text_state"])["selection"] == "LEFTRIGHT", "Menu find effect missing")
    clipboard = {"verified": False}
    if full:
        require(identity.get("clipboard") == "hosted-disposable-vm", "Menu clipboard lacks controlled VM")
        expected_actions = ["select-all", "find", "copy", "paste", "paste", "paste"]
        require([a["action"] for a in actions] == expected_actions and [menu_action_id(a["text"]) for a in actions] == expected_actions,
                "Incomplete menu Copy/Paste actions")
        require(all(a["input"] == "QMenu active action plus queued Return key" for a in actions), "Menu input path differs")
        require(json.loads(phases["menu-copy-before"]["text_state"])["selection"] == expected, "Menu Copy selection differs")
        require(phases["menu-copy-before"]["peer"].get("received_hex") == "" and phases["menu-find-result"]["peer"].get("received_hex") == "", "Search/menu input leaked to peer")
        writes = [p["system_clipboard"] for p in payloads if p.get("kind") == "clipboard_set"]
        require(writes == ["TF-COPY-SENTINEL", SINGLE, "TF-FIRST\r\nTF-SECOND\nTF-THIRD"], "Clipboard fixture differs")
        copies = [p["system_clipboard"] for p in payloads if p.get("kind") == "menu-copy-system"]
        require(copies == [expected.replace("\n", "\r\n")], "VT CF_UNICODETEXT soft/hard breaks, whitespace or Unicode differ")
        encoded = lambda value: value.encode("utf-8").hex()
        clipboard = {"verified": True, "copy": copies[0], **validate_paste(phases, "", peer_field="received_hex", encode=encoded)}
        final = encoded(SINGLE + MULTILINE)
        require(phases.get("paste-settled", {}).get("peer", {}).get("received_hex") == final and peer.get("received_hex") == final,
                "Final menu peer has missing or late paste bytes")
    else:
        require(peer.get("received_hex") == "", "Search/menu input leaked to peer")
    return {"schema": "termforge.r1-menu-wrap.v1", "target_sha256": EXPECTED_EXE, "distribution_sha256": EXPECTED_FILES,
            "fixture_sha256": identity.get("fixture_sha256"), "template_sha256": identity.get("template_sha256"),
            "evidence_id": record["evidence_id"], "complete": False, "menu_verified": full,
            "selection_find_verified": selection_find_verified,
            "vt": {"protocol": "Telnet-loopback", "columns": columns, "sent_sha256": peer["sent_sha256"],
                   "selection": selection, "soft_wrap_joins": True, "hard_break_lf": True, "trailing_spaces_preserved": True,
                   "search": search}, "menus": menus, "activations": actions,
            "system_clipboard": clipboard, "cleanup": {"owned_active": 0, "peer_closed": True},
            "limitations": ["Qt context/key event replay; not physical mouse input.", "Menu and VT scenario only; full R1-01 also requires the text contract."]}


def diagnostic(directory):
    """Public allowlist: synthetic outcomes only; no widgets, handles, paths or stacks."""
    data = {"schema": "termforge.r1-reference-diagnostic.v1", "validated": False}
    identity_path = directory / "run.json"
    if not identity_path.exists():
        return {**data, "failure": "launcher-did-not-produce-identity"}
    identity = json.loads(identity_path.read_text(encoding="utf-8"))
    require(identity.get("sha256") == EXPECTED_EXE and identity.get("profile") == "synthetic-local-only", "Unsafe diagnostic source")
    data["identity"] = {k: identity.get(k) for k in ("sha256", "files_sha256", "script_sha256", "template_sha256", "fixture_sha256", "clipboard")}
    for name in ("cleanup", "profile-check"):
        path = directory / (name + ".json")
        if path.exists():
            value = json.loads(path.read_text(encoding="utf-8"))
            keys = ("active", "total", "terminated") if name == "cleanup" else ("only_synthetic_session", "session_count")
            data[name] = {k: value.get(k) for k in keys}
    evidence_path = directory / "evidence.json"
    launcher_failure = directory / "launcher-failure.json"
    if launcher_failure.exists():
        reason = json.loads(launcher_failure.read_text(encoding="utf-8")).get("reason")
        allowed = ("rea-timeout", "rea-create-failed", "rea-job-assignment-failed", "rea-resume-failed", "rea-wait-failed", "rea-exit-failed")
        data["failure"] = reason if reason in allowed else "launcher-failed"
    if not evidence_path.exists():
        return {**data, "failure": "no-evidence"}
    try:
        evidence = json.loads(evidence_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return {**data, "failure": data.get("failure", "incomplete-rea-json")}
    record = evidence.get("data", {}).get("evidence", {})
    result = record.get("normalized_result", {})
    data["evidence_id"] = record.get("evidence_id")
    data["snapshots"], data["errors"] = [], []
    for message in result.get("messages", []):
        p = message.get("payload", {})
        if p.get("error") or message.get("type") == "error":
            # Stage and error category suffice; provider stacks can contain paths.
            allowed_errors = ("OpenClipboard failed", "OpenClipboard write failed", "GlobalLock failed",
                              "EmptyClipboard failed", "GlobalAlloc failed", "SetClipboardData failed",
                              "clipboard bound", "scenario timeout", "first run guard", "no editor",
                              "expected one default confirmation button", "system error", "no context popup", "menu choice guard", "menu bound", "event allocation failed")
            reason = next((s for s in allowed_errors if p.get("error") == "Error: " + s), "unclassified")
            error = {"stage": p.get("stage"), "category": "probe-error" if p.get("error") else "frida-error", "reason": reason}
            if p.get("operation") in ("ui-ready", "resize-main", "shell-ready", "vt-ready", "context-construct", "context-send", "context-destroy", "post-context"):
                error["operation"] = p["operation"]
            data["errors"].append(error)
        if p.get("kind") == "readiness":
            data.setdefault("readiness", []).append({k: p.get(k) for k in ("phase", "ready", "has_tree", "has_engine", "has_editor", "peer_ready")})
        if p.get("kind") == "snapshot":
            item = {k: p.get(k) for k in ("phase", "focus", "modal", "peer")}
            item["text_state"] = json.loads(p["text_state"]) if p.get("text_state") else None
            item["buttons"] = [{k: w.get(k) for k in ("text", "default")} for w in p.get("widgets", []) if w.get("class") == "QPushButton"]
            data["snapshots"].append(item)
        elif p.get("kind") in ("scenario_complete", "copy-system", "menu-copy-system", "clipboard_set", "confirmation_action"):
            data.setdefault("outcomes", []).append({k: p[k] for k in ("kind", "coverage", "system_clipboard", "text") if k in p})
        elif p.get("kind") in ("find_edit", "context_action"):
            data.setdefault("context_observations", []).append({k: p[k] for k in ("kind", "phase", "text", "selected", "system_clipboard", "action", "input") if k in p})
        elif p.get("kind") == "menu":
            data.setdefault("menus", []).append({"phase": p["phase"], "actions": r1_menu_actions(p["actions"])})
        elif p.get("kind") == "menu_activation":
            data.setdefault("activations", []).append({k: p.get(k) for k in ("action", "text", "name", "input")})
        elif p.get("kind") == "clipboard_entry":
            data.setdefault("clipboard_entries", []).append({"stage": p.get("stage")})
    return data


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--require-complete", action="store_true")
    parser.add_argument("--require-menu-complete", action="store_true")
    parser.add_argument("--require-context-complete", action="store_true")
    parser.add_argument("--diagnostic-only", action="store_true")
    parser.add_argument("--export-context", action="store_true", help="Export replayable allowlisted context evidence after full validation")
    parser.add_argument("--compare-with", type=Path, help="Require repeat equality with the first validated summary")
    args = parser.parse_args()
    if args.export_context:
        from research.ui.r1_action_context import export_context
        require(not args.diagnostic_only and not args.compare_with, "Context export is a separate operation")
        require(not (args.run / "launcher-failure.json").exists(), "Launcher failure invalidates the run")
        read = lambda name: json.loads((args.run / name).read_text(encoding="utf-8"))
        result = export_context(read("run.json"), read("evidence.json"), read("cleanup.json"), read("profile-check.json"),
                                hashlib.sha256((args.run / "probe.js").read_bytes()).hexdigest(), read("peer.json"))
        args.out.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        raise SystemExit(0)
    if args.diagnostic_only:
        args.out.write_text(json.dumps(diagnostic(args.run), indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        raise SystemExit(0)
    result = read_run(args.run)
    if args.compare_with:
        result["repeat"] = compare_runs(json.loads(args.compare_with.read_text(encoding="utf-8")), result)
    args.out.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    require(not args.require_complete or result["complete"], "R1 reference still has missing clipboard/paste evidence")
    require(not args.require_menu_complete or result.get("menu_verified") is True, "Menu behavior is not verified")
    require(not args.require_context_complete or result.get("context_verified") is True, "Action context is not verified")
    print(json.dumps({"complete": result["complete"], "evidence_id": result["evidence_id"]}))
