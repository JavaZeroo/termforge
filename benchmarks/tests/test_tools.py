import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import tracemalloc
import unittest

TOOLS = Path(__file__).resolve().parents[1] / "tools"


def load(name):
    spec = importlib.util.spec_from_file_location(name, TOOLS / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


generate = load("generate")
play = load("play")


class Workloads(unittest.TestCase):
    def test_generation_memory_does_not_scale_with_payload(self):
        with tempfile.TemporaryDirectory() as temp:
            peaks = []
            for size in (65536, 1048576):
                tracemalloc.start()
                try:
                    generate.generate(Path(temp) / str(size), target_bytes=size)
                    peaks.append(tracemalloc.get_traced_memory()[1])
                finally:
                    tracemalloc.stop()
            # A 16x larger payload must not accumulate payload/oracle in RAM.
            self.assertLess(peaks[1], 512 * 1024)
            self.assertLess(peaks[1], peaks[0] + 128 * 1024)

    def test_determinism_all_kinds_and_manifest_integrity(self):
        with tempfile.TemporaryDirectory() as temp:
            for kind in generate.KINDS:
                a, b = Path(temp) / (kind + "a"), Path(temp) / (kind + "b")
                ma = generate.generate(a, kind, count=20, variant="pseudorandom")
                mb = generate.generate(b, kind, count=20, variant="pseudorandom")
                self.assertEqual(ma, mb)
                wire = (a / "payload.bin").read_bytes()
                self.assertEqual(wire, (b / "payload.bin").read_bytes())
                self.assertEqual(hashlib.sha256(wire).hexdigest(), ma["sha256"])
                self.assertEqual(len(wire), ma["payload_bytes"])
                self.assertEqual(hashlib.sha256((a / "oracle.txt").read_bytes()).hexdigest(),
                                 ma["oracle_sha256"])
                wire.decode("utf-8", errors="strict")
                self.assertTrue(wire.endswith((ma["marker"] + "\r\n").encode()))

    def test_plain_and_ansi_match_independent_text_oracle(self):
        with tempfile.TemporaryDirectory() as temp:
            for kind in ("ascii", "history", "unicode", "longline", "ansi"):
                dest = Path(temp) / kind
                generate.generate(dest, kind, count=16)
                wire = (dest / "payload.bin").read_bytes()
                plain = re.sub(rb"\x1b\[[0-9;]*m", b"", wire).replace(b"\r\n", b"\n")
                self.assertEqual(plain, (dest / "oracle.txt").read_bytes())
                if kind == "ansi":
                    colors = set(re.findall(rb"\x1b\[(\d+)m", wire)) - {b"0"}
                    self.assertEqual(len(colors), 16)

    def test_control_workloads_have_expected_main_screen_text(self):
        with tempfile.TemporaryDirectory() as temp:
            over = Path(temp) / "over"
            generate.generate(over, "overwrite", count=13)
            text = (over / "oracle.txt").read_text().splitlines()
            self.assertEqual(len(text), 2)
            self.assertTrue(text[0].startswith("0000000012 "))
            self.assertEqual(len(text[0]), 79)
            alt = Path(temp) / "alt"
            generate.generate(alt, "alternate", count=13)
            data = (alt / "payload.bin").read_bytes()
            self.assertEqual(data.count(b"\x1b[?1049h"), 13)
            self.assertEqual(data.count(b"\x1b[?1049l"), 13)
            self.assertEqual((alt / "oracle.txt").read_text().splitlines()[0], "MAIN-SCREEN")

    def test_size_boundary_seed_and_exclusive_destination(self):
        with tempfile.TemporaryDirectory() as temp:
            a = Path(temp) / "a"
            m = generate.generate(a, target_bytes=1001, variant="pseudorandom")
            self.assertGreaterEqual(m["payload_bytes"] - m["marker_bytes"], 1001)
            self.assertLess(m["payload_bytes"] - m["marker_bytes"], 1001 + 77)
            with self.assertRaises(FileExistsError):
                generate.generate(a)
            b = generate.generate(Path(temp) / "b", target_bytes=1001,
                                  variant="pseudorandom", seed=1)
            self.assertNotEqual(m["sha256"], b["sha256"])
            with self.assertRaises(ValueError):
                generate.generate(Path(temp) / "bad", count=0)

    def test_partial_write_and_zero_progress(self):
        received = bytearray()

        def short_write(fd, data):
            part = data[:3]
            received.extend(part)
            return len(part)

        original = "中文e\u0301\x1b[31m\n".encode()
        play.write_all(1, original, short_write)
        self.assertEqual(bytes(received), original)
        with self.assertRaises(OSError):
            play.write_all(1, b"test", lambda fd, data: 0)

    def test_replay_arbitrary_chunking_preserves_bytes_and_detects_corruption(self):
        with tempfile.TemporaryDirectory() as temp:
            dest = Path(temp) / "unicode"
            generate.generate(dest, "unicode", count=3)
            expected = (dest / "payload.bin").read_bytes()
            for chunk in (1, 7, 4096, 65536):
                result = Path(temp) / f"{chunk}.json"
                run = subprocess.run([sys.executable, str(TOOLS / "play.py"), str(dest),
                                      "--result", str(result), "--chunk", str(chunk)],
                                     capture_output=True)
                self.assertEqual(run.returncode, 0, run.stderr)
                self.assertEqual(run.stdout, expected)
                events = json.loads(result.read_text())
                self.assertIsNone(events["consumer_done"])
                self.assertIsNone(events["presented_done"])
                self.assertEqual(events["fully_written_chunk_bytes"], len(expected))
            (dest / "payload.bin").write_bytes(b"corrupt")
            failed = subprocess.run([sys.executable, str(TOOLS / "play.py"), str(dest),
                                     "--result", str(Path(temp) / "failed.json")],
                                    capture_output=True)
            self.assertNotEqual(failed.returncode, 0)
            self.assertEqual(failed.stdout, b"")


@unittest.skipUnless(os.name == "nt", "Windows private bytes")
class Sampling(unittest.TestCase):
    def test_controlled_child_memory_cpu_and_trace(self):
        sample = load("sample")
        # Root remains alive while a late-created child allocates 32 MiB and
        # does CPU work. Only test-owned processes are launched.
        child = "import time; a=bytearray(32*1024*1024); t=time.monotonic()+1.2\nwhile time.monotonic()<t: sum(range(10000))\ntime.sleep(.5)"
        parent = ("import subprocess,sys,time; time.sleep(.3); "
                  f"p=subprocess.Popen([sys.executable,'-c',{child!r}]); "
                  "p.wait(); time.sleep(.5)")
        with tempfile.TemporaryDirectory() as temp:
            proc = subprocess.Popen([sys.executable, "-c", parent],
                                    creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                path = Path(temp) / "trace.jsonl"
                sample.sample(proc.pid, path, seconds=3, interval=.05)
                proc.wait(timeout=5)
            finally:
                if proc.poll() is None:
                    proc.terminate()
                    proc.wait()
            records = [json.loads(line) for line in path.read_text().splitlines()]
            self.assertEqual(records[0]["type"], "metadata")
            self.assertEqual(records[-1]["status"], "complete")
            samples = [r for r in records if r["type"] == "sample"]
            self.assertGreater(len(samples), 10)
            self.assertEqual(sorted(r["elapsed_ns"] for r in samples),
                             [r["elapsed_ns"] for r in samples])
            descendants = [p for r in samples for p in r["processes"]
                           if p["group"] == "descendant" and p["status"] == "observed"]
            self.assertTrue(descendants)
            self.assertGreater(max(p["private_bytes"] for p in descendants), 32 * 1024 * 1024)
            self.assertGreater(max(p["cpu_seconds_since_first_observation"] for p in descendants), .05)
            self.assertTrue(any(p["group"] == "sampler" for p in samples[0]["processes"]))
            report = load("summarize_resources").summarize(path)
            self.assertEqual(report["sample_count"], len(samples))
            self.assertGreater(report["groups"]["descendant"]["observed_cpu_seconds"], .05)
            self.assertGreater(report["groups"]["descendant"]["sampled_peak_observed_private_bytes"],
                               32 * 1024 * 1024)


if __name__ == "__main__":
    unittest.main()
