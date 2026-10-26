"""Prepare fixed shell prerequisites only on the designated disposable CI VM.

Never enable Windows features, reboot, convert/remove an existing distro, or
change WSL defaults. Existing Ubuntu startup failure is a real failure.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path, PureWindowsPath
import re
import shlex
import subprocess
import sys
import time
import traceback
import urllib.request

from experiments.conpty.run import OwnedJob

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "termforge-hosted-shell-preparation-v1"
ROOTFS_VERSION = "noble-20240423-amd64-wsl1"
ROOTFS_URL = "https://cloud-images.ubuntu.com/wsl/releases/noble/20240423/ubuntu-noble-wsl-amd64-wsl.rootfs.tar.gz"
ROOTFS_SHA256 = "8251e27ffff381a4af5f41dcb94d867de3e0d9774a9241908ab34555d99315ea"
MAX_DOWNLOAD = 400 * 1024 * 1024
STEPS = {"preflight": 45, "download": 120, "import": 90, "check-before": 25, "check-after": 25}


class PreparationError(Exception):
    """The argument is a fixed public failure code, never raw command output."""


def require_hosted(environment):
    expected = {"GITHUB_ACTIONS": "true", "RUNNER_ENVIRONMENT": "github-hosted",
                "RUNNER_OS": "Windows", "GITHUB_REPOSITORY": "JavaZeroo/termforge"}
    if (any(environment.get(key) != value for key, value in expected.items()) or
            environment.get("ImageOS") not in ("win25", "win25-vs2026") or
            not re.fullmatch(r"\d{8}\.\d+\.\d+", environment.get("ImageVersion", "")) or
            not re.fullmatch(r"\d+", environment.get("GITHUB_RUN_ID", ""))):
        raise PreparationError("hosted_guard")


def require_paths(workspace, out):
    workspace, out = Path(workspace).resolve(), Path(out).resolve()
    if workspace != ROOT or out == workspace / "local" or not out.is_relative_to(workspace / "local"):
        raise PreparationError("workspace_guard")
    return out


def require_runtime():
    require_hosted(os.environ)
    if os.name != "nt" or not ctypes.windll.shell32.IsUserAnAdmin():
        raise PreparationError("windows_admin_guard")
    return Path(os.environ["GITHUB_WORKSPACE"]).resolve()


def write_json(path, value):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, path)


def linux_path(path):
    path = PureWindowsPath(path)
    if not path.is_absolute() or not re.fullmatch(r"[A-Za-z]:", path.drive):
        raise PreparationError("cwd_identity")
    return "/mnt/" + path.drive[0].lower() + "/" + "/".join(path.parts[1:])


def decode_wsl(raw):
    if len(raw) > 65536:
        raise PreparationError("wsl_report_size")
    if raw.startswith((b"\xff\xfe", b"\xfe\xff")):
        return raw.decode("utf-16")
    return raw.decode("utf-16-le" if b"\x00" in raw else "utf-8-sig")


def ubuntu_present(raw):
    try:
        names = decode_wsl(raw).splitlines()
    except UnicodeError:
        raise PreparationError("wsl_list_encoding") from None
    if len(names) > 100 or any(len(name) > 256 or any(ord(c) < 32 for c in name) for name in names):
        raise PreparationError("wsl_list_identity")
    return any(name.strip().casefold() == "ubuntu" for name in names)


def ubuntu_registered():
    # Check the named registration via the first-party API. An empty `wsl -l`
    # can return a nonzero status; do not confuse that with an existing distro
    # which failed to start, or delete/replace an existing registration.
    api = ctypes.WinDLL("api-ms-win-wsl-api-l1-1-0.dll", use_last_error=True)
    api.WslIsDistributionRegistered.argtypes = [ctypes.c_wchar_p]
    api.WslIsDistributionRegistered.restype = ctypes.c_int
    return bool(api.WslIsDistributionRegistered("Ubuntu"))


def classify_registration(code, output, registered):
    if registered:
        if code or not ubuntu_present(output):
            raise PreparationError("wsl_list")
        return "ubuntu-existing"
    if code == 0 and ubuntu_present(output):
        raise PreparationError("wsl_list")
    return "ubuntu-absent"


def wsl_command(executable, cwd):
    # Match the product's explicit distribution, Windows --cd and final exec sh.
    return [str(executable), "--cd", str(cwd), "-d", "Ubuntu", "-e", "sh", "-c",
            "export PS1='TF> '; export LANG=C.UTF-8; exec sh"]


def wsl_input(cwd):
    expected = shlex.quote(linux_path(cwd))
    return ("test \"$WSL_DISTRO_NAME\" = Ubuntu || exit 71\n"
            "test -x /bin/sh || exit 72\n"
            f"test \"$PWD\" = {expected} || exit 73\n"
            "printf 'TERMFORGE_HOSTED_WSL_READY\\n'\nexit 37\n").encode("utf-8")


def validate_marker(stdout, returncode, marker, expected_code=0):
    try:
        lines = stdout.decode("utf-8-sig").splitlines()
    except UnicodeError:
        raise PreparationError("marker_encoding") from None
    if returncode != expected_code or lines != [marker]:
        raise PreparationError("shell_startup")


def import_command(executable, install, archive):
    return [str(executable), "--import", "Ubuntu", str(install), str(archive), "--version", "1"]


def copy_verified_rootfs(response, destination, *, expected_hash=ROOTFS_SHA256,
                         limit=MAX_DOWNLOAD, clock=time.monotonic):
    """Stream bounded bytes and reject an unverified file before it can be imported."""
    started, count, digest = clock(), 0, hashlib.sha256()
    with Path(destination).open("xb") as stream:
        while True:
            if clock() - started >= 120:
                raise PreparationError("download_deadline")
            chunk = response.read(1024 * 1024)
            if clock() - started >= 120:
                raise PreparationError("download_deadline")
            if not chunk:
                break
            count += len(chunk)
            if count > limit:
                raise PreparationError("download_size")
            stream.write(chunk)
            digest.update(chunk)
    if not count or digest.hexdigest() != expected_hash:
        raise PreparationError("download_hash")


def run_command(command, directory, name, *, cwd, timeout=10, data=None):
    # This function only runs inside a GO-gated worker already in the parent's
    # exact Job. No shell string construction or global-process cleanup.
    stdout_path, stderr_path = directory / (name + ".stdout"), directory / (name + ".stderr")
    with stdout_path.open("xb") as stdout, stderr_path.open("xb") as stderr:
        try:
            result = subprocess.run(command, input=data, stdout=stdout, stderr=stderr,
                                    cwd=cwd, timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
        except subprocess.TimeoutExpired:
            raise PreparationError("command_deadline") from None
    if stdout_path.stat().st_size > 65536:
        raise PreparationError("command_report_size")
    return result.returncode, stdout_path.read_bytes()


def require_worker_job():
    if os.name != "nt":
        raise PreparationError("worker_job_guard")
    api = ctypes.WinDLL("kernel32", use_last_error=True)
    api.GetCurrentProcess.restype = ctypes.c_void_p
    api.IsProcessInJob.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(ctypes.c_int)]
    api.IsProcessInJob.restype = ctypes.c_int
    contained = ctypes.c_int()
    if not api.IsProcessInJob(api.GetCurrentProcess(), None, ctypes.byref(contained)) or not contained.value:
        raise PreparationError("worker_job_guard")


def worker(step, out):
    if sys.stdin.buffer.readline(4) != b"GO\n":
        return 90
    require_worker_job()
    workspace = require_runtime()
    require_paths(workspace, out)
    private = out / "_private" / step
    if not private.is_dir():
        raise PreparationError("worker_directory_guard")
    system = Path(os.environ["SystemRoot"]) / "System32"
    wsl = system / "wsl.exe"
    cwd = out / "_private" / "launch cwd"
    cwd.mkdir(exist_ok=True)
    archive = out / "_private" / "ubuntu.rootfs.tar.gz"
    if step == "preflight":
        commands = [([str(system / "cmd.exe"), "/d", "/q", "/c", "echo TERMFORGE_HOSTED_CMD_READY"],
                     "cmd", "TERMFORGE_HOSTED_CMD_READY"),
                    ([str(system / "WindowsPowerShell/v1.0/powershell.exe"), "-NoLogo", "-NoProfile",
                      "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command",
                      "[Console]::WriteLine('TERMFORGE_HOSTED_PS51_READY')"], "ps51", "TERMFORGE_HOSTED_PS51_READY"),
                    ([str(Path(os.environ["ProgramFiles"]) / "PowerShell/7/pwsh.exe"), "-NoLogo", "-NoProfile",
                      "-NonInteractive", "-Command", "[Console]::WriteLine('TERMFORGE_HOSTED_PS7_READY')"],
                     "ps7", "TERMFORGE_HOSTED_PS7_READY")]
        for command, name, marker in commands:
            code, output = run_command(command, private, name, cwd=cwd)
            validate_marker(output, code, marker)
        code, output = run_command([str(wsl), "--list", "--quiet"], private, "list", cwd=cwd)
        return classify_registration(code, output, ubuntu_registered())
    if step == "download":
        request = urllib.request.Request(ROOTFS_URL, headers={"User-Agent": "TermForge-hosted-shell-preparation"})
        with urllib.request.urlopen(request, timeout=15) as response:
            if response.status != 200 or response.geturl() != ROOTFS_URL:
                raise PreparationError("download_response")
            length = response.headers.get("Content-Length")
            if length is not None and (not length.isdigit() or not 0 < int(length) <= MAX_DOWNLOAD):
                raise PreparationError("download_size")
            partial = archive.with_suffix(".download")
            copy_verified_rootfs(response, partial)
        os.replace(partial, archive)
        return "rootfs-verified"
    if step == "import":
        code, output = run_command([str(wsl), "--list", "--quiet"], private, "list-before-import", cwd=cwd)
        if classify_registration(code, output, ubuntu_registered()) != "ubuntu-absent":
            raise PreparationError("ubuntu_already_present")
        # Recheck the fixed archive before the single, fixed-version import.
        if not archive.is_file() or archive.stat().st_size > MAX_DOWNLOAD:
            raise PreparationError("download_size")
        digest = hashlib.sha256()
        with archive.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        if digest.hexdigest() != ROOTFS_SHA256:
            raise PreparationError("download_hash")
        install = out / "_private" / "ubuntu-root"
        install.mkdir(exist_ok=False)
        code, _ = run_command(import_command(wsl, install, archive), private, "import", cwd=cwd, timeout=80)
        if code:
            raise PreparationError("wsl_import")
        return "ubuntu-imported-wsl1"
    if step in ("check-before", "check-after"):
        code, output = run_command([str(wsl), "--list", "--quiet"], private, "list", cwd=cwd)
        if classify_registration(code, output, ubuntu_registered()) != "ubuntu-existing":
            raise PreparationError("ubuntu_missing")
        code, output = run_command(wsl_command(wsl, cwd), private, "startup", cwd=cwd,
                                   timeout=20, data=wsl_input(cwd))
        validate_marker(output, code, "TERMFORGE_HOSTED_WSL_READY", expected_code=37)
        return "ubuntu-sh-cwd-verified"
    raise PreparationError("worker_step")


RESULTS_BY_STEP = {"preflight": {"ubuntu-existing", "ubuntu-absent"},
                   "download": {"rootfs-verified"}, "import": {"ubuntu-imported-wsl1"},
                   "check-before": {"ubuntu-sh-cwd-verified"}, "check-after": {"ubuntu-sh-cwd-verified"}}
FAILURES = {"hosted_guard", "workspace_guard", "windows_admin_guard", "worker_job_guard", "cwd_identity", "wsl_report_size",
            "wsl_list_encoding", "wsl_list_identity", "marker_encoding", "shell_startup", "download_deadline",
            "download_size", "download_hash", "command_deadline", "command_report_size", "worker_directory_guard",
            "wsl_list", "download_response", "ubuntu_already_present", "wsl_import", "ubuntu_missing", "worker_step"}


def run_step(step, out):
    private = out / "_private" / step
    private.mkdir(parents=True, exist_ok=False)
    record = {"schema": SCHEMA, "step": step, "status": "failed", "failure_code": "worker_failed"}
    job = process = None
    assigned = False
    stdout = stderr = b""
    try:
        job = OwnedJob()
        command = [sys.executable, "-m", "tools.prepare_hosted_shells", "--out", str(out), "--worker", step]
        process = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, creationflags=subprocess.CREATE_NO_WINDOW)
        job.assign(process.pid)
        assigned = True
        try:
            stdout, stderr = process.communicate(b"GO\n", timeout=STEPS[step])
        except subprocess.TimeoutExpired:
            record["failure_code"] = "worker_deadline"
            job.terminate()
            stdout, stderr = process.communicate(timeout=5)
        if process.returncode == 0 and len(stdout) <= 4096:
            value = json.loads(stdout)
            if set(value) == {"result"} and value["result"] in RESULTS_BY_STEP[step]:
                record.update(status="passed", failure_code="none", result=value["result"])
        elif len(stdout) <= 4096:
            try:
                value = json.loads(stdout)
                if set(value) == {"failure_code"} and value["failure_code"] in FAILURES:
                    record["failure_code"] = value["failure_code"]
            except (ValueError, TypeError):
                pass
    except Exception:
        (private / "exception.txt").write_text(traceback.format_exc(), encoding="utf-8")
    finally:
        if job is not None:
            try:
                if process is not None and process.poll() is None:
                    if assigned:
                        job.terminate()
                    else:
                        process.kill()  # no GO was sent
                    stdout, stderr = process.communicate(timeout=5)
                if assigned and job.drain()["active"]:
                    record.update(status="failed", failure_code="cleanup_failed")
                    job.terminate()
                    if job.drain()["active"]:
                        record.update(status="failed", failure_code="cleanup_failed")
            except Exception:
                record.update(status="failed", failure_code="cleanup_failed")
            finally:
                job.close()
        (private / "worker.stdout").write_bytes(stdout)
        (private / "worker.stderr").write_bytes(stderr)
    return record


def prepare(out):
    report = {"schema": SCHEMA, "status": "failed", "fixed_import_rootfs_version": ROOTFS_VERSION,
              "fixed_import_rootfs_sha256": ROOTFS_SHA256, "rootfs_verified": False,
              "steps": [], "failure_code": "none"}
    def step(name):
        record = run_step(name, out)
        report["steps"].append(record)
        write_json(out / "summary.json", report)
        if record["status"] != "passed":
            raise PreparationError(record["failure_code"])
        if record.get("result") not in RESULTS_BY_STEP[name]:
            raise PreparationError("worker_step")
        return record.get("result")
    try:
        existing = step("preflight") == "ubuntu-existing"
        if not existing:
            step("download")
            report["rootfs_verified"] = True
            step("import")
        # An unusable pre-existing distro fails here. Never delete or replace it.
        step("check-before")
        step("check-after")
        report["status"] = "passed"
    except PreparationError as error:
        report["failure_code"] = str(error)
    except Exception:
        report["failure_code"] = "preparation_exception"
        (out / "_private" / "preparation.exception.txt").write_text(traceback.format_exc(), encoding="utf-8")
    finally:
        write_json(out / "summary.json", report)
    return report


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--out", type=Path, required=True)
    cli.add_argument("--worker", choices=STEPS, help=argparse.SUPPRESS)
    args = cli.parse_args()
    if args.worker:
        try:
            result = worker(args.worker, args.out.resolve())
            if isinstance(result, int):
                return result
            print(json.dumps({"result": result}), flush=True)
            return 0
        except PreparationError as error:
            print(json.dumps({"failure_code": str(error) if str(error) in FAILURES else "worker_step"}), flush=True)
            return 1
        except Exception:
            # Raw traceback stays in the parent's private stderr file.
            traceback.print_exc()
            return 1
    try:
        workspace = require_runtime()
        out = require_paths(workspace, args.out)
        out.mkdir(parents=True, exist_ok=False)
    except (PreparationError, OSError, KeyError):
        print(json.dumps({"schema": SCHEMA, "status": "failed", "failure_code": "preparation_guard"}))
        return 2
    try:
        report = prepare(out)
    except Exception:
        print(json.dumps({"schema": SCHEMA, "status": "failed", "failure_code": "preparation_exception"}))
        return 1
    print(json.dumps({"schema": SCHEMA, "status": report["status"], "failure_code": report["failure_code"]}))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
