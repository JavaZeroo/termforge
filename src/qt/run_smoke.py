"""Run the public Qt smoke under an owned Job before any ConPTY is created."""
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from experiments.conpty.run import OwnedJob


def main():
    binary = Path(sys.argv[1]).resolve(strict=True)
    job, process, assigned = OwnedJob(), None, False
    result = {"schema": "termforge-qt-cpp-supervisor-v1", "status": "failed"}
    cleanup_errors = []

    def root_running():
        try:
            return process is not None and process.poll() is None
        except Exception as error:
            cleanup_errors.append("query root: " + str(error))
            return process is not None
    try:
        environment = os.environ.copy()
        environment["QT_QPA_PLATFORM"] = "offscreen"
        environment["QT_QPA_FONTDIR"] = str(binary.parent / "assets/fonts")
        process = subprocess.Popen([str(binary)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, env=environment,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        # The native smoke reads this bounded gate before constructing Qt.
        job.assign(process.pid)
        assigned = True
        try:
            stdout, stderr = process.communicate(b"GO\n", timeout=25)
            result["timed_out"] = False
        except subprocess.TimeoutExpired:
            result["timed_out"] = True
            result["job_at_timeout"] = job.accounting()
            job.terminate()
            stdout, stderr = process.communicate(timeout=5)
        result.update(exit_code=process.returncode, job_after=job.drain(timeout=3))
        if stderr:
            sys.stderr.write(stderr.decode("utf-8", "replace"))
        if result["timed_out"] or process.returncode or result["job_after"]["active"]:
            raise RuntimeError("Qt smoke failed or left owned processes active")
        if result["job_after"]["total"] < 3:
            raise RuntimeError("Qt smoke did not observe its root, cmd and ConPTY host")
        result["sample"] = json.loads(stdout.decode("utf-8"))
        result["status"] = "passed"
    except Exception as error:
        result["error"] = str(error)
    finally:
        # Preserve natural Job accounting above; forced cleanup cannot pass.
        try:
            if root_running():
                if assigned:
                    try:
                        job.terminate()
                    except Exception as error:
                        cleanup_errors.append("terminate Job: " + str(error))
                # Also reclaims the pre-GO root when assignment failed.
                try:
                    if root_running():
                        process.kill()
                except Exception as error:
                    cleanup_errors.append("terminate root: " + str(error))
                try:
                    process.communicate(timeout=5)
                except Exception as error:
                    cleanup_errors.append("drain root: " + str(error))
            try:
                remaining = job.accounting()
            except Exception as error:
                remaining = None
                cleanup_errors.append("query Job: " + str(error))
            if remaining and remaining["active"]:
                try:
                    job.terminate()
                except Exception as error:
                    cleanup_errors.append("terminate remaining Job: " + str(error))
                try:
                    if job.drain(timeout=3)["active"]:
                        cleanup_errors.append("owned processes remain active")
                except Exception as error:
                    cleanup_errors.append("drain Job: " + str(error))
        finally:
            try:
                job.close()
            except Exception as error:
                cleanup_errors.append("close Job: " + str(error))
    if cleanup_errors:
        result["cleanup_errors"] = cleanup_errors
        result["status"] = "failed"
    passed = result["status"] == "passed"
    print(json.dumps(result), file=sys.stdout if passed else sys.stderr)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
