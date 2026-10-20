"""Enroll/watch a fixed client ROI template. No OCR, no input injection."""
import argparse
import ctypes
import hashlib
import json
import math
from pathlib import Path
import time

import psutil
from visual import Capture, Windows, template_matches


def watch(mode, pid, hwnd, rect, template, output=None, timeout=10, interval=.008):
    if not all(math.isfinite(v) and v > 0 for v in (timeout, interval)):
        raise ValueError("timeout/interval must be positive and finite")
    process = psutil.Process(pid)
    identity = process.create_time()
    ctypes.windll.user32.SetProcessDPIAware()
    api = Windows()
    template = Path(template)
    if mode == "record":
        template.mkdir(parents=True, exist_ok=False)
        with Capture(api, hwnd, pid, *rect) as capture:
            data, times = capture.grab()
        (template / "roi.bgra").write_bytes(data)
        (template / "template.json").write_text(json.dumps({
            "rect": rect, "sha256": hashlib.sha256(data).hexdigest(),
            "capture": times, "format": "top-down BGRA; alpha ignored",
            "provenance": "operator-selected ROI; manually confirm marker AND final-state context",
        }, indent=2) + "\n", encoding="utf-8")
        return {"status": "template_recorded_not_semantically_verified"}
    if not output:
        raise ValueError("watch requires --out")
    metadata = json.loads((template / "template.json").read_text(encoding="utf-8"))
    expected = (template / "roi.bgra").read_bytes()
    if list(rect) != metadata["rect"] or len(expected) != rect[2] * rect[3] * 4:
        raise ValueError("template dimensions/position differ")
    if hashlib.sha256(expected).hexdigest() != metadata["sha256"]:
        raise ValueError("template corrupt")
    status = "timeout"
    last_negative = None
    start = time.perf_counter_ns()
    with Path(output).open("x", encoding="utf-8") as stream:
        def emit(row):
            stream.write(json.dumps(row) + "\n")
            stream.flush()

        emit({"type": "metadata", "template_sha256": metadata["sha256"],
              "pid": pid, "create_time": identity, "rect": list(rect),
              "started_counter_ns": start, "requested_interval_ns": int(interval * 1e9),
              "limitations": ["Fixed pixels only; not OCR or terminal-history validation.",
                              "Must observe nonmatch before match; repeated/stale state rejected.",
                              "Foreground required; no physical display timestamp.",
                              "Template and first-match images can contain private text; keep local."]})
        try:
            with Capture(api, hwnd, pid, *rect) as capture:
                while time.perf_counter_ns() - start < timeout * 1e9:
                    if not process.is_running():
                        raise RuntimeError("target identity expired")
                    data, times = capture.grab()
                    matching = template_matches(data, expected)
                    emit({"type": "frame", **times, "match_finished_ns": time.perf_counter_ns(),
                          "matched": matching})
                    if matching:
                        status = "observed" if last_negative else "stale_template"
                        emit({"type": "observation", "status": status,
                              "previous_nonmatch": last_negative, "first_match": times,
                              "frame_sha256": hashlib.sha256(data).hexdigest()})
                        break
                    last_negative = times
                    time.sleep(interval)
        except Exception as exc:
            status = "aborted"
            emit({"type": "error", "reason": f"{type(exc).__name__}: {exc}"})
        emit({"type": "end", "status": status, "ended_counter_ns": time.perf_counter_ns()})
    return {"status": status}


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("mode", choices=("record", "watch"))
    p.add_argument("--pid", required=True, type=int)
    p.add_argument("--hwnd", required=True, type=lambda s: int(s, 0))
    p.add_argument("--rect", required=True, type=int, nargs=4, metavar=("X", "Y", "W", "H"))
    p.add_argument("--template", required=True)
    p.add_argument("--out")
    p.add_argument("--timeout", type=float, default=10)
    args = p.parse_args()
    result = watch(args.mode, args.pid, args.hwnd, args.rect, args.template, args.out, args.timeout)
    print(json.dumps(result))
    raise SystemExit(0 if result["status"] in ("observed", "template_recorded_not_semantically_verified") else 1)
