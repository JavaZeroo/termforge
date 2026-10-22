"""Pinned official WindTerm static analysis; all generated artifacts stay in local/."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
TARGET_SHA256 = "7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0"


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def run(target, ghidra, java_home, output, timeout=1800):
    if timeout <= 0:
        raise ValueError("Timeout must be positive")
    target, ghidra, java_home, output = [Path(p).resolve() for p in (target, ghidra, java_home, output)]
    if not output.is_relative_to(ROOT / "local"):
        raise ValueError("Raw analysis must be written inside repository local/")
    if digest(target) != TARGET_SHA256:
        raise ValueError("Target differs from pinned official WindTerm 2.7.0 x64")
    headless = ghidra / "support" / "analyzeHeadless.bat"
    java = java_home / "bin" / "java.exe"
    if not headless.is_file() or not java.is_file():
        raise ValueError("Extracted Ghidra and full JDK are required")
    properties = (ghidra / "Ghidra" / "application.properties").read_text()
    if "application.version=12.1.4\n" not in properties:
        raise ValueError("Expected Ghidra 12.1.4")
    output.mkdir(parents=True, exist_ok=False)
    projects = output / "projects"
    projects.mkdir()
    script = ROOT / "research" / "ghidra" / "ExportTerminalIndex.java"
    command = [str(headless), str(projects), "WindTerm", "-import", str(target),
               "-scriptPath", str(script.parent), "-postScript", script.name, str(output / "export"),
               "-max-cpu", "2", "-log", str(output / "analysis.log"),
               "-scriptlog", str(output / "scripts.log")]
    env = os.environ.copy()
    env["JAVA_HOME"] = str(java_home)
    env["PATH"] = str(java.parent) + os.pathsep + env.get("PATH", "")
    manifest = {"kind": "headless_static_run", "status": "running", "target_sha256": TARGET_SHA256,
                "script_sha256": digest(script), "runner_sha256": digest(__file__),
                "ghidra_version": "12.1.4", "max_cpu": 2, "timeout_seconds": timeout,
                "target_executed": False, "command": command,
                "java_version": subprocess.run([str(java), "-version"], capture_output=True, text=True, check=True).stderr.strip()}
    path = output / "run.json"
    path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    start = time.monotonic()
    with (output / "console.log").open("wb") as log:
        proc = None
        try:
            proc = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT,
                                    stdin=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)
            code = proc.wait(timeout=timeout)
            manifest["exit_code"] = code
            summary = output / "export" / "summary.json"
            manifest["status"] = "complete" if code == 0 and summary.is_file() else "failed"
            if manifest["status"] == "complete":
                data = json.loads(summary.read_text())
                if data["executable_sha256"].lower() != TARGET_SHA256:
                    manifest["status"] = "identity_mismatch"
                manifest["summary"] = data
        except (subprocess.TimeoutExpired, KeyboardInterrupt) as exc:
            manifest["status"] = "timeout" if isinstance(exc, subprocess.TimeoutExpired) else "interrupted"
            # Kill only this owned launcher tree; never WindTerm or other analysis sessions.
            if proc is not None:
                subprocess.run(["taskkill", "/PID", str(proc.pid), "/T", "/F"], capture_output=True)
                proc.wait()
        except Exception as exc:
            manifest["status"] = "failed"
            manifest["error"] = f"{type(exc).__name__}: {exc}"
        finally:
            manifest["elapsed_seconds"] = time.monotonic() - start
            manifest["target_unchanged"] = target.is_file() and digest(target) == TARGET_SHA256
            if not manifest["target_unchanged"]:
                manifest["status"] = "identity_mismatch"
            path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", required=True)
    parser.add_argument("--ghidra", required=True)
    parser.add_argument("--java-home", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    result = run(args.target, args.ghidra, args.java_home, args.out)
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result["status"] == "complete" else 1)
