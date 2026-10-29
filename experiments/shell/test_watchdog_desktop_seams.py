"""AST-only #160 seams: no diagnostic imports/main, Win32, desktop or target."""
import argparse
import ast
import copy
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
    args = parser.parse_args()
    SOURCE_COMMIT = args.source_commit
    classes = [OutputBoundaryTests, JobCloseSeamTests] if SOURCE_COMMIT else [OutputBoundaryTests, JobCloseSeamTests, PostcheckTests]
    suite = unittest.TestSuite(unittest.defaultTestLoader.loadTestsFromTestCase(case) for case in classes)
    raise SystemExit(0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1)
