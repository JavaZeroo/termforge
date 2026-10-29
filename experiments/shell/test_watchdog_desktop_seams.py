"""AST-only #160 seams: no diagnostic imports/main, Win32, desktop or target."""
import argparse
import ast
import copy
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import time
from types import MethodType, SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE_COMMIT = None
ARTIFACTS = None


def diagnostic_tree():
    name = "experiments/shell/diagnose_watchdog_desktop.py"
    data = (subprocess.check_output(["git", "show", SOURCE_COMMIT + ":" + name], cwd=ROOT).decode("utf-8")
            if SOURCE_COMMIT else (ROOT / name).read_text(encoding="utf-8"))
    return ast.parse(data)


def execute_nodes(nodes, namespace):
    # Only explicitly selected definitions/constants; never module imports/main.
    exec(compile(ast.fix_missing_locations(ast.Module(body=copy.deepcopy(nodes), type_ignores=[])),
                 "<actual-source-AST-seam>", "exec"), namespace)


def functions(namespace):
    nodes = [node for node in diagnostic_tree().body if isinstance(node, ast.FunctionDef)
             and node.name not in {"main", "run_case", "observed_shell"}]
    execute_nodes(nodes, namespace)
    return namespace


def expected(data):
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


class OutputBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.runtime = self.root / "runtime"
        self.runtime.mkdir()
        self.scope = {"Path": Path, "json": json, "hashlib": hashlib, "ROOT": self.root,
                      "os": SimpleNamespace(name="nt"), "struct": SimpleNamespace(calcsize=lambda _: 8),
                      "platform": SimpleNamespace(version=lambda: "synthetic-Windows"),
                      "sys": SimpleNamespace(version="synthetic-Python"),
                      "subprocess": SimpleNamespace(check_output=lambda *a, **k: "b" * 40)}
        constants = [node for node in diagnostic_tree().body if isinstance(node, ast.Assign)
                     and isinstance(node.targets[0], ast.Name)
                     and node.targets[0].id in {"BASE", "SOURCE_FILES", "OFFSETS", "FIXED", "VARIABLE"}]
        execute_nodes(constants, self.scope)
        functions(self.scope)
        # Synthetic archive/OS/Git seams only; all actual load_plan validation runs.
        archive = self.root / "synthetic-archive.bin"
        archive.write_bytes(b"synthetic archive")
        self.scope["FIXED"]["baseline_archive_sha256"] = expected(archive.read_bytes())["sha256"]
        plan = copy.deepcopy(self.scope["FIXED"])
        source_hashes = {}
        for name in self.scope["SOURCE_FILES"]:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(name.encode())
            source_hashes[name] = expected(path.read_bytes())["sha256"]
        exe = self.runtime / "termforge_shell.exe"
        exe.write_bytes(b"not an executable; pure file fixture")
        launcher = self.runtime / "shell_test_launcher.exe"
        launcher.write_bytes(b"not launched")
        inventory = self.root / "inventory.json"
        inventory.write_text(json.dumps({exe.name: expected(exe.read_bytes()),
                                         launcher.name: expected(launcher.read_bytes())}), encoding="utf-8")
        build = self.root / "build.txt"
        build.write_bytes(b"synthetic build identity; no compiler")
        bound = lambda path: {"path": str(path), **expected(path.read_bytes())}
        plan.update(execution_authorized=True, execution_source_commit="b" * 40,
                    windows_version="synthetic-Windows", python_version="synthetic-Python",
                    source_sha256=source_hashes, actual_compiled_executable=bound(exe),
                    runtime_inventory=bound(inventory), build_evidence=bound(build),
                    baseline_archive=str(archive), output_directory=None)
        self.plan, self.path = plan, self.root / "authorized-fixture.json"

    def load(self, output):
        self.plan["output_directory"] = str(output)
        self.path.write_text(json.dumps(self.plan), encoding="utf-8")
        return self.scope["load_plan"](self.path)

    def test_runtime_equal_is_rejected(self):
        with self.assertRaises(ValueError):
            self.load(self.runtime)

    def test_nested_output_is_rejected(self):
        with self.assertRaises(ValueError):
            self.load(self.runtime / "new-results")

    def test_dotdot_alias_output_is_rejected(self):
        (self.runtime / "existing-alias").mkdir()
        with self.assertRaises(ValueError):
            self.load(self.runtime / "existing-alias" / ".." / "new-results")

    def test_sibling_output_is_allowed_without_creating_it(self):
        output = self.root / "runtime-other"
        self.load(output)
        self.assertFalse(output.exists())


class JobCloseSeamTests(unittest.TestCase):
    def test_close_failure_preserves_original_finally_sample(self):
        tree = diagnostic_tree()
        native_class = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == "Native")
        observed = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "observed_shell")
        job_class = next(n for n in observed.body if isinstance(n, ast.ClassDef) and n.name == "Job")
        native_close = copy.deepcopy(next(n for n in native_class.body if isinstance(n, ast.FunctionDef) and n.name == "close"))
        job_close = copy.deepcopy(next(n for n in job_class.body if isinstance(n, ast.FunctionDef) and n.name == "close"))
        native_close.name, job_close.name = "native_close", "job_close"
        calls = []
        native = SimpleNamespace(record={}, kernel=SimpleNamespace(CloseHandle=lambda value: calls.append(value) or False))
        scope = {"native": native, "time": time, "ctypes": SimpleNamespace(
            set_last_error=lambda value: None, get_last_error=lambda: 5,
            WinError=lambda value: OSError(value, "synthetic CloseHandle failure"))}
        execute_nodes([native_close, job_close], scope)
        native.close = MethodType(scope["native_close"], native)
        deferred = next((n for n in native_class.body if isinstance(n, ast.FunctionDef)
                         and n.name == "defer_observer_error"), None)
        if deferred is not None:
            execute_nodes([deferred], scope)
            native.defer_observer_error = MethodType(scope["defer_observer_error"], native)
        class Job:
            close = scope["job_close"]
            def __init__(self):
                self.handle = 123
                self.ledger = {"value": 123, "close_attempted": False, "closed": False}
            def assign(self, pid): pass
            def accounting(self): return {"active": 5, "total": 6, "terminated": 0}
            def terminate(self): pass
            def drain(self): return {"active": 0, "total": 6, "terminated": 0}
        class Desktop:
            name, full_name, handle = "synthetic", "synthetic\\owned", 456
            def close(self): self.handle = None; return False
        class Process:
            pid, returncode = 789, None
            def communicate(self, data=None, timeout=None):
                if data is not None:
                    raise subprocess.TimeoutExpired("synthetic, never launched", timeout)
                self.returncode = 99
                return b'{"watchdog_ready":true}\r\n', b''
            def poll(self): return self.returncode
        process = Process()
        production = ast.parse((ROOT / "experiments/shell/run.py").read_text(encoding="utf-8"))
        original_execute = next(n for n in production.body if isinstance(n, ast.FunctionDef) and n.name == "execute")
        run_scope = {"OwnedJob": Job, "OwnedTestDesktop": Desktop, "json": json,
                     "subprocess": SimpleNamespace(Popen=lambda *a, **k: process, CREATE_NO_WINDOW=0, PIPE=-1,
                                                    TimeoutExpired=subprocess.TimeoutExpired)}
        execute_nodes([original_execute], run_scope)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "original"
            caught = None
            try:
                run_scope["execute"](Path(directory) / "synthetic.exe", "watchdog", output, timeout=3)
            except Exception as error:
                caught = error
            self.assertTrue((output / "sample.json").exists(), "Job.close prevented the actual finally sample write")
            sample = json.loads((output / "sample.json").read_text(encoding="utf-8"))
            self.assertEqual(sample["stdout"], '{"watchdog_ready":true}\r\n')
            self.assertEqual(sample["stderr"], "")
            self.assertEqual((sample["status"], sample["error"]), ("failed", "owned desktop remained"))
            self.assertEqual(str(caught), "owned desktop remained")
        self.assertEqual(calls, [123])
        self.assertEqual(native.record["deferred_observer_errors"][0]["handle_ledger"]["close_error"], 5)
        verifier = functions({})["require_observer_cleanup"]
        with self.assertRaises(OSError):
            verifier(native.record)


def root_drain_fixture(test, *, desktop_gone=True, image_error=31, observer_fault=None, drain_error=False):
    """Memory-only API fakes around actual root_state/Job and original execute."""
    temporary = tempfile.TemporaryDirectory()
    test.addCleanup(temporary.cleanup)
    directory = Path(temporary.name)
    class DWord:
        def __init__(self, value=0): self.value = value
    class FileTime:
        dwHighDateTime = dwLowDateTime = 0
    class Kernel:
        exited, error, pid, creation, live_image_error, times_error = False, 0, 789, 100, 0, 0
        def WaitForSingleObject(self, handle, timeout): return 0 if self.exited else 258
        def GetProcessTimes(self, handle, created, exited, kernel, user):
            if self.exited and self.times_error:
                self.error = self.times_error
                return False
            created.dwLowDateTime = self.creation
            exited.dwLowDateTime = 200 if self.exited else 0
            return True
        def GetExitCodeProcess(self, handle, code): code.value = 99 if self.exited else 259; return True
        def GetProcessId(self, handle): return self.pid
        def QueryFullProcessImageNameW(self, handle, flags, image, size):
            failure = image_error if self.exited else self.live_image_error
            if failure:
                self.error = failure
                return False
            image.value = str(directory / "shell_test_launcher.exe")
            return True
        def IsProcessInJob(self, handle, job, member): member.value = True; return True
        def CloseHandle(self, value): return True
    kernel = Kernel()
    fake_ctypes = SimpleNamespace(byref=lambda value: value,
        create_unicode_buffer=lambda size: SimpleNamespace(value=""),
        set_last_error=lambda value: setattr(kernel, "error", value), get_last_error=lambda: kernel.error,
        WinError=lambda value: OSError(value, "synthetic retained-root API failure"))
    record = {"events": [], "handle_ledger": []}
    native = SimpleNamespace(record=record, kernel=kernel, root_identity=None, process=None,
                             expected_root_image=(directory / "shell_test_launcher.exe").resolve(),
                             last_job=None, root_observation_error=None)
    tree = diagnostic_tree()
    native_class = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == "Native")
    methods = [n for n in native_class.body if isinstance(n, ast.FunctionDef)
               and n.name in {"root_state", "event", "own", "close", "defer_observer_error"}]
    scope = {"native": native, "Path": Path, "time": time, "datetime": datetime, "timezone": timezone,
             "ctypes": fake_ctypes, "wintypes": SimpleNamespace(DWORD=DWord, BOOL=DWord, FILETIME=FileTime)}
    execute_nodes(methods, scope)
    for method in methods:
        setattr(native, method.name, MethodType(scope[method.name], native))
    native.root = native.own(42, "synthetic retained duplicate; no native handle acquired")
    actual_event = native.event
    def event(kind, **fields):
        if observer_fault == kind and (kind != "job_accounting" or kernel.exited):
            raise RuntimeError("synthetic observer event: " + kind)
        return actual_event(kind, **fields)
    native.event = event
    class Process:
        pid, returncode, started = 789, None, False
        def communicate(self, data=None, timeout=None):
            if data is not None:
                self.started = True
                raise subprocess.TimeoutExpired("synthetic; never launched", timeout)
            self.returncode = 99
            kernel.exited = True
            return b'{"watchdog_ready":true}\r\n', b''
        def poll(self): return self.returncode
    process = Process()
    class BaseJob:
        def __init__(self):
            self.handle = 123
            self.accounting_calls = 0
            self.api = SimpleNamespace(AssignProcessToJobObject=lambda *unused: True)
        @staticmethod
        def check(value):
            if not value: raise OSError("synthetic original Job API failure")
        def accounting(self):
            self.accounting_calls += 1
            if observer_fault == "terminate-accounting-api" and self.accounting_calls == 3 and not kernel.exited:
                raise OSError("synthetic extra terminate accounting API failure")
            return {"active": 0 if kernel.exited else (5 if process.started else 1),
                    "total": 6 if process.started else 1, "terminated": 0}
        def terminate(self): pass
        def drain(self, timeout=5):
            if drain_error: raise OSError("synthetic original drain API failure")
            return self.accounting()
    scope["original_job"] = BaseJob
    observed = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "observed_shell")
    job = next(n for n in observed.body if isinstance(n, ast.ClassDef) and n.name == "Job")
    execute_nodes([job], scope)
    class Desktop:
        name, full_name, handle = "synthetic", "synthetic\\owned", 456
        def close(self): self.handle = None; return desktop_gone
    def popen(*args, **kwargs):
        native.process = process
        native.root_state("created-before-assignment-and-GO")
        return process
    production = ast.parse((ROOT / "experiments/shell/run.py").read_text(encoding="utf-8"))
    original_execute = next(n for n in production.body if isinstance(n, ast.FunctionDef) and n.name == "execute")
    original = {"OwnedJob": scope["Job"], "OwnedTestDesktop": Desktop, "json": json,
        "subprocess": SimpleNamespace(Popen=popen, CREATE_NO_WINDOW=0, PIPE=-1, TimeoutExpired=subprocess.TimeoutExpired)}
    execute_nodes([original_execute], original)
    def run():
        caught = None
        try:
            original["execute"](directory / "synthetic.exe", "watchdog", directory / "original", timeout=3)
        except Exception as error:
            caught = error
        sample = json.loads((directory / "original/sample.json").read_text())
        if ARTIFACTS is not None:
            destination = ARTIFACTS / ("case-" + str(len(list(ARTIFACTS.iterdir()))))
            destination.mkdir()
            (destination / "sample.json").write_bytes((directory / "original/sample.json").read_bytes())
            (destination / "observer.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
            (destination / "fixture.json").write_text(json.dumps({"test": test.id(), "desktop_gone": desktop_gone,
                "image_error_after_exit": image_error, "observer_fault": observer_fault, "original_drain_error": drain_error,
                "caught_original_exception": str(caught) if caught else None,
                "scope": "Synthetic APIs/process/Job only; exact sample bytes written by actual original execute finally"}, indent=2), encoding="utf-8")
        return sample, caught
    return SimpleNamespace(native=native, kernel=kernel, run=run, output=directory / "original", process=process)


class RootDrainPollutionTests(unittest.TestCase):
    def check_original(self, gone):
        fixture = root_drain_fixture(self, desktop_gone=gone)
        sample, caught = fixture.run()
        self.assertEqual(sample.get("stdout"), '{"watchdog_ready":true}\r\n',
                         "post-exit observer exception interrupted original record.update")
        self.assertEqual(sample["stderr"], "")
        self.assertEqual(sample["exit_code"], 99)
        self.assertEqual(sample["job_after"], {"active": 0, "total": 6, "terminated": 0})
        self.assertEqual(sample["job_after_cleanup"], sample["job_after"])
        self.assertNotIn("cleanup_errors", sample)
        self.assertEqual(sample["desktop_gone"], gone)
        self.assertEqual(sample["status"], "passed" if gone else "failed")
        if gone:
            self.assertIsNone(caught)
            self.assertNotIn("error", sample)
        else:
            self.assertEqual(str(caught), "owned desktop remained")
            self.assertEqual(sample["error"], "owned desktop remained")
        after = [row for row in fixture.native.record["events"] if row.get("label") == "after-original-drain"]
        self.assertEqual(len(after), 2)
        for row in after:
            self.assertIsNone(row["image"])
            self.assertEqual(row["api_results"]["QueryFullProcessImageNameW"],
                             {"attempted": True, "returned": False, "error": 31})
            self.assertTrue(row["observation_gaps"])
            self.assertEqual(row["identity_reference"]["creation_filetime_100ns"], 100)
        self.assertNotIn("deferred_observer_errors", fixture.native.record)
    def test_exited_image31_preserves_original_passed_sample(self): self.check_original(True)
    def test_exited_image31_preserves_original_failed_desktop_verdict(self): self.check_original(False)


class RootObserverErrorTests(unittest.TestCase):
    def test_actual_case_copies_original_sample_before_observer_verifier(self):
        case = next(n for n in diagnostic_tree().body if isinstance(n, ast.FunctionDef) and n.name == "run_case")
        copy_sample = next(n for n in ast.walk(case) if isinstance(n, ast.Assign)
                           and any(isinstance(t, ast.Subscript) and isinstance(t.slice, ast.Constant)
                                   and t.slice.value == "original_sample" for t in n.targets))
        verifier = next(n for n in ast.walk(case) if isinstance(n, ast.Call)
                        and isinstance(n.func, ast.Name) and n.func.id == "require_observer_cleanup")
        self.assertLess(copy_sample.lineno, verifier.lineno)
    def test_other_root_query_error_is_failed_only_after_original_sample(self):
        fixture = root_drain_fixture(self, image_error=5)
        sample, caught = fixture.run()
        self.assertIsNone(caught)
        self.assertEqual(sample["status"], "passed")
        self.assertEqual(sample["exit_code"], 99)
        self.assertEqual(sample["job_after"]["active"], 0)
        self.assertNotIn("error", sample)
        self.assertEqual(len(fixture.native.record["deferred_observer_errors"]), 2)
        with self.assertRaises(OSError):
            functions({})["require_observer_cleanup"](fixture.native.record)
        self.assertTrue((fixture.output / "sample.json").exists())
    def test_all_drain_related_event_failures_preserve_original_fields(self):
        for fault in ("job_accounting", "original_job_terminate", "original_job_drain_begin", "original_job_drain_end", "terminate-accounting-api"):
            with self.subTest(fault=fault):
                fixture = root_drain_fixture(self, image_error=0, observer_fault=fault)
                sample, caught = fixture.run()
                self.assertIsNone(caught)
                self.assertEqual((sample["status"], sample["exit_code"], sample["job_after"]["active"]), ("passed", 99, 0))
                self.assertTrue(fixture.native.record["deferred_observer_errors"])
                with self.assertRaises(OSError):
                    functions({})["require_observer_cleanup"](fixture.native.record)
    def test_original_drain_error_remains_original_error(self):
        fixture = root_drain_fixture(self, image_error=0, drain_error=True)
        sample, caught = fixture.run()
        self.assertEqual(str(caught), "synthetic original drain API failure")
        self.assertEqual(sample["error"], str(caught))
        self.assertEqual(sample["status"], "failed")
        self.assertNotIn("deferred_observer_errors", fixture.native.record)
    def test_pre_go_image_and_creation_remain_strict(self):
        for field, value in (("live_image_error", 31), ("creation", 0), ("pid", 999)):
            with self.subTest(field=field):
                fixture = root_drain_fixture(self)
                fixture.native.process = fixture.process
                setattr(fixture.kernel, field, value)
                with self.assertRaises(OSError): fixture.native.root_state("created-before-assignment-and-GO")
                self.assertIsNone(fixture.native.root_identity)
    def test_retained_creation_change_is_not_accepted_after_exit(self):
        fixture = root_drain_fixture(self)
        fixture.native.process = fixture.process
        fixture.native.root_state("created-before-assignment-and-GO")
        fixture.kernel.creation += 1
        fixture.kernel.exited = True
        with self.assertRaises(OSError): fixture.native.root_state("after-original-drain", strict=False)
    def test_image31_requires_signaled_handle_and_prior_identity(self):
        fixture = root_drain_fixture(self)
        fixture.native.process = fixture.process
        fixture.kernel.exited = True
        with self.assertRaises(OSError): fixture.native.root_state("after-original-drain", strict=False)
        fixture.kernel.exited = False
        fixture.native.root_state("created-before-assignment-and-GO")
        fixture.kernel.live_image_error = 31
        with self.assertRaises(OSError): fixture.native.root_state("after-original-drain", strict=False)


class PostcheckTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.runtime = self.root / "runtime"
        self.runtime.mkdir()
        self.files = {}
        for name in ["driver.py", "config.json", "sources.hpp", "inventory.json", "archive.zip", "build.txt"]:
            path = self.root / name
            path.write_bytes(name.encode())
            self.files[str(path)] = expected(name.encode())
        exe = self.runtime / "termforge_shell.exe"
        exe.write_bytes(b"synthetic-exe")
        self.files[str(exe)] = expected(b"synthetic-exe")
        self.frozen = {"files": self.files, "source_commit": "b" * 40,
                       "runtime_directory": str(self.runtime),
                       "runtime_files": {exe.name: expected(b"synthetic-exe")}}
        self.scope = functions({"Path": Path, "hashlib": hashlib})
        self.scope["git_head"] = lambda: "b" * 40  # No process invocation.
        self.batch = {"cases": [{"status": "observations_complete"}] * 2}

    def check(self):
        self.scope["finalize_batch"](self.batch, self.frozen)
        return self.batch["diagnostic_complete"]

    def test_unchanged_inputs_allow_diagnostic_completion(self):
        self.assertTrue(self.check())

    def test_script_config_source_exe_and_other_bound_drift_fail(self):
        for name in self.files:
            with self.subTest(name=Path(name).name):
                path = Path(name)
                original = path.read_bytes()
                path.write_bytes(original + b"drift")
                self.assertFalse(self.check())
                self.assertFalse(self.batch["postcheck_inputs_unchanged"])
                path.write_bytes(original)

    def test_additional_runtime_file_fails(self):
        (self.runtime / "unexpected.dll").write_bytes(b"extra")
        self.assertFalse(self.check())

    def test_source_head_drift_fails(self):
        self.scope["git_head"] = lambda: "c" * 40
        self.assertFalse(self.check())

    def test_actual_main_wires_postcheck_before_batch_save(self):
        main = next(n for n in diagnostic_tree().body if isinstance(n, ast.FunctionDef) and n.name == "main")
        cleanup = next(n for n in main.body if isinstance(n, ast.Try)).finalbody
        self.assertEqual(cleanup[0].value.func.id, "finalize_batch")
        self.assertEqual(cleanup[1].value.func.id, "save")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-commit")
    parser.add_argument("--artifacts", type=Path)
    args = parser.parse_args()
    SOURCE_COMMIT = args.source_commit
    ARTIFACTS = args.artifacts
    if ARTIFACTS is not None:
        ARTIFACTS.mkdir(parents=True, exist_ok=False)
    classes = ([OutputBoundaryTests, JobCloseSeamTests, RootDrainPollutionTests] if SOURCE_COMMIT else
               [OutputBoundaryTests, JobCloseSeamTests, RootDrainPollutionTests, RootObserverErrorTests, PostcheckTests])
    suite = unittest.TestSuite(unittest.defaultTestLoader.loadTestsFromTestCase(case) for case in classes)
    raise SystemExit(0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1)
