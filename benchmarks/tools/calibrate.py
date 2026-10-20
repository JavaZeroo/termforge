"""Alternating sampler on/off calibration against an owned CPU/memory worker."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import statistics
import subprocess
import sys

from sample import sample

WORKER = """
import hashlib,json,time
memory=bytearray(64*1024*1024)
block=b'x'*65536
start=time.perf_counter_ns()
cpu=time.process_time()
for i in range(20000):
    hashlib.sha256(block).digest()
print(json.dumps({'elapsed_ns':time.perf_counter_ns()-start,
                  'cpu_seconds':time.process_time()-cpu,
                  'allocation_bytes':len(memory)}))
"""


def calibrate(output):
    directory = Path(output)
    directory.mkdir(parents=True, exist_ok=False)
    samples = []
    # Warm the worker executable/code paths once; not a reported sample.
    subprocess.run([sys.executable, "-c", WORKER], check=True, capture_output=True,
                   creationflags=subprocess.CREATE_NO_WINDOW)
    for pair in range(5):
        for enabled in ((False, True) if pair % 2 == 0 else (True, False)):
            proc = subprocess.Popen([sys.executable, "-c", WORKER],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                    creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                if enabled:
                    sample(proc.pid, directory / f"pair-{pair}.jsonl", seconds=30,
                           interval=.1, stop=lambda: proc.poll() is not None)
                stdout, stderr = proc.communicate(timeout=30)
                if proc.returncode:
                    raise RuntimeError(stderr.decode(errors="replace"))
                row = json.loads(stdout)
                row.update(pair=pair, sampler_enabled=enabled)
                samples.append(row)
            finally:
                if proc.poll() is None:
                    proc.terminate()
                    proc.wait()
    off = statistics.median(r["elapsed_ns"] for r in samples if not r["sampler_enabled"])
    on = statistics.median(r["elapsed_ns"] for r in samples if r["sampler_enabled"])
    delta = (on / off - 1) * 100
    report = {
        "kind": "synthetic_sampler_calibration", "samples": samples,
        "python_version": platform.python_version(), "os": platform.platform(),
        "worker_source_sha256": hashlib.sha256(WORKER.encode()).hexdigest(),
        "sampler_source_sha256": hashlib.sha256(Path(__file__).with_name("sample.py").read_bytes()).hexdigest(),
        "calibrator_source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "median_sampler_off_ns": off, "median_sampler_on_ns": on,
        "median_elapsed_change_percent": delta,
        "above_protocol_5_percent_trigger": abs(delta) > 5,
        "limitations": [
            "Synthetic CPU/memory worker only; not T01, PTY, renderer or WindTerm.",
            "Five pairs are diagnostic, not a universal overhead bound.",
            "Sampling every 100 ms until worker exit (30 second cap); final CPU after exit may be missed.",
            "No capture/input tool or inter-process clock alignment calibrated."
        ]}
    (directory / "summary.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out", required=True)
    calibrate(p.parse_args().out)
