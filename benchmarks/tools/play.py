"""Replay verified bytes to inherited stdout; report producer time only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import time


def write_all(fd, data, writer=os.write):
    view = memoryview(data)
    while view:
        size = writer(fd, view)
        if size <= 0:
            raise OSError("write made no progress")
        view = view[size:]


def play(directory, output, chunk=65536):
    if chunk <= 0:
        raise ValueError("chunk must be positive")
    directory = Path(directory)
    manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    # Preload deliberately outside the timing window; include this memory in
    # the producer group, not terminal memory. File size is explicit in report.
    data = (directory / "payload.bin").read_bytes()
    if len(data) != manifest["payload_bytes"] or hashlib.sha256(data).hexdigest() != manifest["sha256"]:
        raise ValueError("payload differs from manifest")
    # Exclusive output avoids clobbering prior measurements.
    with Path(output).open("x", encoding="utf-8") as result:
        fd = sys.stdout.fileno()
        if os.name == "nt":
            import msvcrt
            msvcrt.setmode(fd, os.O_BINARY)
        start = time.perf_counter_ns()
        status, error = "complete", None
        sent = 0
        try:
            view = memoryview(data)
            for offset in range(0, len(data), chunk):
                block = view[offset:offset + chunk]
                write_all(fd, block)
                sent += len(block)
        except OSError as exc:
            status, error = "failed", type(exc).__name__
        end = time.perf_counter_ns()
        json.dump({
            "status": status, "clock": "perf_counter_ns",
            "clock_resolution_seconds": time.get_clock_info("perf_counter").resolution,
            "t0_counter_ns": start, "producer_done_counter_ns": end if not error else None,
            "attempt_ended_counter_ns": end, "consumer_done": None, "presented_done": None,
            "payload_bytes": len(data), "fully_written_chunk_bytes": sent,
            "chunk_bytes": chunk, "sha256": manifest["sha256"], "error": error,
            "limitations": ["Producer endpoint only; no terminal correctness or presentation observed.",
                            "Preloaded payload consumes producer memory; partial failed chunk count is unknown."]
        }, result, indent=2)
        result.write("\n")
    return 1 if error else 0


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("directory")
    p.add_argument("--result", required=True)
    p.add_argument("--chunk", type=int, default=65536)
    args = p.parse_args()
    sys.exit(play(args.directory, args.result, args.chunk))
