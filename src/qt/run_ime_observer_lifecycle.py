"""Exercise the actual observer event loop and receivers, never OS IME input."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from experiments.conpty.run import OwnedJob


def main():
    binary = Path(sys.argv[1]).resolve(strict=True)
    scenario = sys.argv[2]
    if scenario not in ("normal", "cancel"):
        raise ValueError("Expected normal or cancel lifecycle scenario")
    artifacts = Path(os.environ["TERMFORGE_TEST_ARTIFACTS"])
    artifacts.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix="qt-ime-lifecycle-" + scenario + "-", dir=artifacts))
    app_directory = directory / "observer"  # The observer itself must create this.
    result = {"schema": "termforge-qt-ime-observer-lifecycle-v1", "scenario": scenario,
              "purpose": "offscreen-lifecycle-only", "actual_os_ime": False,
              "status": "failed", "execution_budget_seconds": 25, "cleanup_budget_seconds": 5,
              "directory": str(directory), "ready_observed": False, "marker_created": False,
              "timed_out": False, "forced_cleanup": False, "cleanup_errors": []}
    job, process, assigned = OwnedJob(), None, False
    started = time.monotonic()
    stdout, stderr = b"", b""
    try:
        environment = os.environ.copy()
        environment["QT_QPA_PLATFORM"] = "offscreen"
        environment["QT_QPA_FONTDIR"] = str(binary.parent / "assets/fonts")
        process = subprocess.Popen([str(binary), "--offscreen-ime-observer-lifecycle", str(app_directory)],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   env=environment, creationflags=subprocess.CREATE_NO_WINDOW)
        job.assign(process.pid)
        assigned = True
        result["root_pid"] = process.pid
        process.stdin.write(b"GO\n")
        process.stdin.close()
        process.stdin = None
        deadline = started + 25
        while process.poll() is None and time.monotonic() < deadline:
            ready_path = app_directory / "ready.json"
            if not result["marker_created"] and ready_path.exists():
                ready = json.loads(ready_path.read_text(encoding="utf-8"))
                if not (ready["ready"] and ready["qt_platform"] == "offscreen" and
                        ready["purpose"] == "offscreen-lifecycle-only" and
                        ready["a"]["running"] and ready["b"]["running"]):
                    raise RuntimeError("Actual two-receiver observer readiness is incomplete")
                result["ready_observed"] = True
                marker = app_directory / ("finish.request" if scenario == "normal" else "cancel.request")
                marker.write_bytes(b"")
                result["marker_created"] = True
                result["marker_elapsed_ms"] = round((time.monotonic() - started) * 1000)
            time.sleep(0.01)
        if process.poll() is None:
            result["timed_out"] = True
            result["job_at_timeout"] = job.accounting()
            raise TimeoutError("Observer did not exit within the unchanged 25 second execution budget")
        stdout, stderr = process.communicate(timeout=max(0.001, deadline - time.monotonic()))
        result["exit_code"] = process.returncode
        result["job_after_natural_exit"] = job.accounting()
        complete = json.loads((app_directory / "complete.json").read_text(encoding="utf-8"))
        result["complete"] = complete
        if not result["ready_observed"] or not result["marker_created"]:
            raise RuntimeError("Lifecycle exited before the declared control action")
        if complete["purpose"] != "offscreen-lifecycle-only" or complete["qt_platform"] != "offscreen":
            raise RuntimeError("Lifecycle evidence was confused with native OS IME")
        if not complete["sessions_completed"] or complete["deadline_expired"]:
            raise RuntimeError("Actual session completion callback did not end the event loop")
        if scenario == "normal":
            if process.returncode != 0 or not complete["observer_complete"] or complete["failed"]:
                raise RuntimeError("Normal observer completion did not report a successful application exit")
            if not all(complete[label]["natural_exit"] and complete[label]["cleanup_verified"]
                       for label in ("a", "b")):
                raise RuntimeError("Normal completion lacks both actual receiver exits")
        else:
            if process.returncode != 1 or complete["observer_complete"] or not complete["failed"]:
                raise RuntimeError("Cancelled observer did not return explicit failure")
            if complete["failure"] != "native observer cancelled by controller":
                raise RuntimeError("Cancelled observer lost the original failure reason")
            if not all(complete[label]["finished"] and complete[label]["cleanup_verified"]
                       for label in ("a", "b")):
                raise RuntimeError("Cancelled observer did not complete both session cleanups")
        if result["job_after_natural_exit"]["active"]:
            raise RuntimeError("Observer root exited while owned descendants were still active")
        if json.loads(stdout.decode("utf-8")) != complete:
            raise RuntimeError("Observer complete file differs from its actual stdout result")
        result["status"] = "passed"
    except Exception as error:
        result["error"] = str(error)
    finally:
        result["execution_elapsed_ms"] = round((time.monotonic() - started) * 1000)
        cleanup_deadline = time.monotonic() + 5
        try:
            if process is not None:
                remaining = job.accounting() if assigned else None
                if process.poll() is None or (remaining and remaining["active"]):
                    result["forced_cleanup"] = True
                    if assigned:
                        job.terminate()
                    else:
                        process.kill()
                stdout, stderr = process.communicate(timeout=max(0.001, cleanup_deadline - time.monotonic()))
                result["exit_code"] = process.returncode
            result["job_after_cleanup"] = job.drain(timeout=max(0, cleanup_deadline - time.monotonic()))
            if result["job_after_cleanup"]["active"]:
                raise RuntimeError("Observer lifecycle left processes in its exact owned Job")
        except Exception as error:
            result["cleanup_errors"].append(str(error))
        finally:
            job.close()
        (directory / "stdout.log").write_bytes(stdout)
        (directory / "stderr.log").write_bytes(stderr)
        result["complete_exists"] = (app_directory / "complete.json").exists()
        result["receiver_files"] = {path.name: path.stat().st_size
                                    for path in app_directory.glob("native-*.bin*")}
        if result["forced_cleanup"] or result["cleanup_errors"]:
            result["status"] = "failed"
        (directory / "supervisor.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result), file=sys.stdout if result["status"] == "passed" else sys.stderr)
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
