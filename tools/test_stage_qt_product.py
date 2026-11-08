"""Offline contract tests for the Qt product staging tool; no build or GUI access."""
from __future__ import annotations

from contextlib import redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

if __package__:
    from . import stage_qt_product
    from .prepare_qt_build import tree_identity
else:
    import stage_qt_product
    from prepare_qt_build import tree_identity

REAL_ROOT = Path(__file__).resolve().parents[1]
CONTRACT = (REAL_ROOT / "packaging/qt-product/manifest.json").read_bytes()
PREPARE_TOOL = (REAL_ROOT / "tools/prepare_qt_build.py").read_bytes()
CRT_NAMES = ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")
FAKE_ICU = b"windows system icu forwarder fixture\n"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def pe_fixture(kind: str = "exe") -> bytes:
    """Small valid PE32+ AMD64 fixture; never executed as a program."""
    data = bytearray(1024)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 128)
    data[128:132] = b"PE\0\0"
    characteristics = 0x22 if kind == "exe" else 0x2022
    struct.pack_into("<HHIIIHH", data, 132, 0x8664, 1, 0, 0, 0, 240, characteristics)
    struct.pack_into("<H", data, 152, 0x20B)
    struct.pack_into("<I", data, 212, 512)
    return bytes(data)


def git(root: Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(root), "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
         *arguments], check=True, capture_output=True, text=True, timeout=30)
    return result.stdout.strip()


class Fixture:
    """A synthetic repository, dependency preparation and build output."""

    def __init__(self, case: unittest.TestCase):
        temporary = tempfile.TemporaryDirectory(prefix="termforge staging fixture ")
        case.addCleanup(temporary.cleanup)
        base = Path(temporary.name)
        self.root = base / "repo"
        self.deps = base / "deps"
        self.build = base / "build"
        self.system = base / "system"
        self.summary = base / "preparation.json"
        self.contract = json.loads(CONTRACT.decode("utf-8"))
        (self.root / "tools").mkdir(parents=True)
        (self.root / "tools/prepare_qt_build.py").write_bytes(PREPARE_TOOL)
        for directory in ("assets/fonts", "third_party/qt/LICENSES", "third_party/conpty",
                          "third_party/msvc_runtime", "packaging/qt-product"):
            (self.root / directory).mkdir(parents=True, exist_ok=True)
        (self.root / "packaging/qt-product/manifest.json").write_bytes(CONTRACT)
        (self.root / "LICENSE").write_text("MIT fixture\n", encoding="utf-8")
        self._fonts()
        self._manifests()
        self._dependencies()
        self._build_output()
        self._commit()

    def _fonts(self):
        fonts = self.root / "assets/fonts"
        (fonts / "RobotoMono-Regular.ttf").write_bytes(b"font fixture bytes\n")
        (fonts / "OFL.txt").write_text("OFL fixture text\n", encoding="utf-8")
        (fonts / "README.md").write_text("# font fixture notes\n", encoding="utf-8")
        manifest = {
            "schema": "termforge-font-resource-v1",
            "name": "Roboto Mono", "style": "Regular (static)",
            "upstream": {"repository": "fixture", "fixed_commit": "a" * 40, "acquired": "2026-01-01"},
            "license": {"spdx": "OFL-1.1", "bundled_path": "OFL.txt"},
            "files": {},
            "deployment": {"source_directory": "assets/fonts", "staged_directory": "assets/fonts",
                           "additional_documentation": "README.md", "required": True},
        }
        for name, upstream in (("RobotoMono-Regular.ttf", "fonts/ttf/RobotoMono-Regular.ttf"),
                               ("OFL.txt", "OFL.txt")):
            data = (fonts / name).read_bytes()
            manifest["files"][name] = {"bytes": len(data), "sha256": digest(data), "upstream_path": upstream}
        (fonts / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    def _manifests(self):
        license_data = b"LGPL fixture text\n"
        (self.root / "third_party/qt/LICENSES/LGPL-3.0-only.txt").write_bytes(license_data)
        qt_manifest = {
            "version": "6.10.3", "architecture": "win64_msvc2022_64",
            "official_binary_archive": {"sha256": "a" * 64},
            "license": {"bundled_text_files": [{
                "spdx": "LGPL-3.0-only", "path": "LICENSES/LGPL-3.0-only.txt",
                "bytes": len(license_data), "sha256": digest(license_data)}]},
        }
        (self.root / "third_party/qt/manifest.json").write_text(
            json.dumps(qt_manifest, indent=2) + "\n", encoding="utf-8")
        self.qt_manifest = qt_manifest

        conpty_license = b"MIT fixture conpty\n"
        (self.root / "third_party/conpty/LICENSE").write_bytes(conpty_license)
        conpty_manifest = {
            "version": "1.25.260930003", "package_sha256": "b" * 64,
            "bundled_license_sha256": digest(conpty_license),
            "files": {"conpty.dll": "c" * 64, "OpenConsole.exe": "d" * 64},
        }
        (self.root / "third_party/conpty/manifest.json").write_text(
            json.dumps(conpty_manifest, indent=2) + "\n", encoding="utf-8")
        self.conpty_manifest = conpty_manifest

        runtime_license = b"Microsoft redistributable license fixture\n"
        runtime_files = {}
        for name in CRT_NAMES:
            runtime_files[name] = {"sha256": digest(f"crt fixture {name}".encode())}
        runtime_manifest = {
            "version": "14.51.36247.0", "architecture": "windows-x64",
            "package_sha256": "e" * 64,
            "files": runtime_files,
            "license": {"deployed_name": "LICENSE-MICROSOFT-VC-REDIST.docx",
                        "sha256": digest(runtime_license)},
            "notice_sha256": digest(b"crt notice fixture\n"),
        }
        (self.root / "third_party/msvc_runtime/manifest.json").write_text(
            json.dumps(runtime_manifest, indent=2) + "\n", encoding="utf-8")
        self.runtime_manifest = runtime_manifest

    def _dependencies(self):
        qt = self.deps / "qt"
        for name in self.contract["qt_deployed"]["libraries"]:
            path = qt / "bin" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(pe_fixture("dll") + name.encode())
        for name in self.contract["qt_deployed"]["plugins"]:
            path = qt / "plugins" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(pe_fixture("dll") + name.encode())
        conpty = self.deps / "conpty"
        conpty.mkdir(parents=True)
        (conpty / "conpty.dll").write_bytes(pe_fixture("dll") + b"conpty")
        (conpty / "OpenConsole.exe").write_bytes(pe_fixture("exe") + b"openconsole")
        self.conpty_manifest["files"] = {
            "conpty.dll": digest((conpty / "conpty.dll").read_bytes()),
            "OpenConsole.exe": digest((conpty / "OpenConsole.exe").read_bytes()),
        }
        (self.root / "third_party/conpty/manifest.json").write_text(
            json.dumps(self.conpty_manifest, indent=2) + "\n", encoding="utf-8")
        crt = self.deps / "crt"
        crt.mkdir(parents=True)
        for name in CRT_NAMES:
            (crt / name).write_bytes(pe_fixture("dll") + name.encode())
        (crt / "LICENSE-MICROSOFT-VC-REDIST.docx").write_bytes(b"Microsoft redistributable license fixture\n")
        (crt / "NOTICE.txt").write_bytes(b"crt notice fixture\n")
        runtime_files = {name: {"sha256": digest((crt / name).read_bytes())} for name in CRT_NAMES}
        self.runtime_manifest["files"] = runtime_files
        self.runtime_manifest["license"]["sha256"] = digest(
            (crt / "LICENSE-MICROSOFT-VC-REDIST.docx").read_bytes())
        self.runtime_manifest["notice_sha256"] = digest((crt / "NOTICE.txt").read_bytes())
        (self.root / "third_party/msvc_runtime/manifest.json").write_text(
            json.dumps(self.runtime_manifest, indent=2) + "\n", encoding="utf-8")
        qt_manifest = json.loads((self.root / "third_party/qt/manifest.json").read_text(encoding="utf-8"))
        self.write_preparation_summary(qt_manifest)

    def write_preparation_summary(self, qt_manifest: dict):
        summary = {
            "schema": stage_qt_product.PREPARATION_SCHEMA, "status": "passed",
            "qt_version": "6.10.3", "qt_architecture": "win64_msvc2022_64",
            "qt_archive_sha256": qt_manifest["official_binary_archive"]["sha256"],
            "input_manifests": {
                name: digest((self.root / "third_party" / name / "manifest.json").read_bytes())
                for name in ("qt", "conpty", "msvc_runtime")
            },
            "preparation_script_sha256": digest(PREPARE_TOOL),
            "sdk": tree_identity(self.deps / "qt"),
            "conpty_version": "1.25.260930003",
            "conpty_package_sha256": self.conpty_manifest["package_sha256"],
            "conpty_files": dict(self.conpty_manifest["files"]),
            "msvc_runtime": {
                "version": "14.51.36247.0", "architecture": "windows-x64",
                "package_sha256": self.runtime_manifest["package_sha256"],
                "files": {name: identity["sha256"] for name, identity in self.runtime_manifest["files"].items()},
                "license_sha256": self.runtime_manifest["license"]["sha256"],
                "notice_sha256": self.runtime_manifest["notice_sha256"],
            },
        }
        self.summary.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    def _build_output(self):
        self.bin = self.build / "qt-bin/Release"
        self.bin.mkdir(parents=True)
        self.build.joinpath("CMakeCache.txt").write_text(
            "CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022\n"
            "CMAKE_GENERATOR_PLATFORM:INTERNAL=x64\n"
            "CMAKE_CONFIGURATION_TYPES:STRING=Debug;Release\n"
            "TERMFORGE_BUILD_QT_SHELL:BOOL=ON\n"
            "TERMFORGE_BUILD_CANDIDATE_ADAPTER:BOOL=OFF\n"
            "CMAKE_LINKER:FILEPATH=C:/Program Files/Microsoft Visual Studio/2022/Community/"
            "VC/Tools/MSVC/14.42.34433/bin/Hostx64/x64/link.exe\n", encoding="utf-8")
        (self.bin / "termforge_qt_shell.exe").write_bytes(pe_fixture("exe"))
        for name in self.contract["qt_deployed"]["libraries"]:
            (self.bin / name).write_bytes((self.deps / "qt/bin" / name).read_bytes())
        for name in self.contract["qt_deployed"]["plugins"]:
            target = self.bin / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((self.deps / "qt/plugins" / name).read_bytes())
        (self.bin / "conpty-backend").mkdir()
        for name in ("conpty.dll", "OpenConsole.exe"):
            (self.bin / "conpty-backend" / name).write_bytes((self.deps / "conpty" / name).read_bytes())
        (self.bin / "conpty-backend/manifest.json").write_bytes(
            (self.root / "third_party/conpty/manifest.json").read_bytes())
        (self.bin / "conpty-backend/LICENSE").write_bytes(
            (self.root / "third_party/conpty/LICENSE").read_bytes())
        for name in CRT_NAMES:
            (self.bin / name).write_bytes((self.deps / "crt" / name).read_bytes())
        (self.bin / "LICENSE-MICROSOFT-VC-REDIST.docx").write_bytes(
            (self.deps / "crt/LICENSE-MICROSOFT-VC-REDIST.docx").read_bytes())
        (self.bin / "NOTICE.txt").write_bytes((self.deps / "crt/NOTICE.txt").read_bytes())
        (self.bin / "assets/fonts").mkdir(parents=True)
        for name in ("RobotoMono-Regular.ttf", "OFL.txt", "README.md"):
            (self.bin / "assets/fonts" / name).write_bytes(
                (self.root / "assets/fonts" / name).read_bytes())
        self.system.joinpath("System32").mkdir(parents=True)
        (self.bin / "icuuc.dll").write_bytes(FAKE_ICU)
        (self.system / "System32/icuuc.dll").write_bytes(FAKE_ICU)

    def _commit(self):
        git(self.root, "init", "-q")
        git(self.root, "add", "-A")
        git(self.root, "commit", "-q", "-m", "fixture")

    def stage(self, out=None, summary=None, allow_dirty=False):
        out = out or self.build.parent / "staging"
        summary = summary or self.build.parent / "staging-summary.json"
        result = stage_qt_product.stage(
            self.root, self.bin, self.deps, self.summary, out, summary, allow_dirty, self.system)
        return result, out, summary


class StagingTests(unittest.TestCase):
    def setUp(self):
        self.fixture = Fixture(self)

    def assert_failure(self, code, *, allow_dirty=False):
        with self.assertRaises(stage_qt_product.StagingError) as raised:
            self.fixture.stage(allow_dirty=allow_dirty)
        self.assertIn(code, str(raised.exception))
        self.assertFalse((self.fixture.build.parent / "staging").exists())

    def test_stage_produces_a_frozen_path_free_manifest(self):
        result, out, summary = self.fixture.stage()
        self.assertEqual(result["status"], "passed")
        self.assertTrue(out.is_dir())
        manifest = json.loads((out / "product-manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["schema"], stage_qt_product.SCHEMA)
        self.assertEqual(manifest["status"], "engineering_candidate")
        self.assertEqual(manifest["identity"]["entry_executable"], "termforge_qt_shell.exe")
        self.assertFalse(manifest["identity"]["r2_acceptance"])
        self.assertFalse(manifest["source"]["dirty"])
        self.assertEqual(manifest["source"]["commit"], git(self.fixture.root, "rev-parse", "HEAD"))
        self.assertEqual(manifest["dependencies"]["qt"]["version"], "6.10.3")
        self.assertIn("licenses/LGPL-3.0-only.txt", manifest["files"])
        self.assertIn("THIRD-PARTY-NOTICES.txt", manifest["files"])
        self.assertIn("conpty-backend/LICENSE", manifest["files"])
        self.assertEqual(manifest["test_fixtures"]["included"], [])
        self.assertEqual(manifest["os_provided"][0]["name"], "icuuc.dll")
        self.assertTrue(manifest["os_provided"][0]["present_in_build"])
        self.assertEqual(manifest["os_provided"][0]["observed_sha256"], digest(FAKE_ICU))
        self.assertEqual(manifest["redistribution_clearance"]["status"], "pending_user_review")
        self.assertNotIn("icuuc.dll", manifest["files"])
        for name, identity in manifest["files"].items():
            data = (out / name).read_bytes()
            self.assertEqual(len(data), identity["bytes"], name)
            self.assertEqual(digest(data), identity["sha256"], name)
        notices = (out / "THIRD-PARTY-NOTICES.txt").read_text(encoding="utf-8")
        self.assertIn("pending user review", notices)
        self.assertIn("D3Dcompiler_47.dll", notices)
        staged = {path.relative_to(out).as_posix() for path in out.rglob("*") if path.is_file()}
        self.assertEqual(staged, set(manifest["files"]) | {"product-manifest.json"})
        self.assertNotIn(str(self.fixture.root), json.dumps(manifest))
        self.assertNotIn(str(self.fixture.root), json.dumps(result))
        self.assertEqual(stage_qt_product.verify_staging(out)["status"], "passed")

        repeated_result, repeated_out, _ = self.fixture.stage(
            out=self.fixture.build.parent / "staging-again",
            summary=self.fixture.build.parent / "staging-summary-again.json")
        self.assertEqual(repeated_result["manifest_sha256"], result["manifest_sha256"])
        self.assertEqual(repeated_result["staging"], result["staging"])
        self.assertEqual((repeated_out / "product-manifest.json").read_bytes(),
                         (out / "product-manifest.json").read_bytes())
        self.assertTrue(summary.is_file())

    def test_verify_detects_content_change(self):
        _, out, _ = self.fixture.stage()
        target = out / "Qt6Core.dll"
        target.write_bytes(target.read_bytes() + b"tampered")
        with self.assertRaises(stage_qt_product.StagingError) as raised:
            stage_qt_product.verify_staging(out)
        self.assertIn("file_identity:Qt6Core.dll", str(raised.exception))

    def test_verify_detects_missing_and_extra_files(self):
        _, out, _ = self.fixture.stage()
        (out / "LICENSE").unlink()
        with self.assertRaises(stage_qt_product.StagingError) as raised:
            stage_qt_product.verify_staging(out)
        self.assertIn("missing_product_file:LICENSE", str(raised.exception))
        _, second, _ = self.fixture.stage(
            out=self.fixture.build.parent / "second",
            summary=self.fixture.build.parent / "second.json")
        (second / "unexpected.dll").write_bytes(b"unexpected")
        with self.assertRaises(stage_qt_product.StagingError) as raised:
            stage_qt_product.verify_staging(second)
        self.assertIn("unexpected_product_file:unexpected.dll", str(raised.exception))

    def test_missing_entry_and_test_fixture_are_rejected(self):
        (self.fixture.bin / "termforge_qt_shell.exe").unlink()
        self.assert_failure("missing_product_file:termforge_qt_shell.exe")

    def test_extra_test_executable_is_rejected_by_name(self):
        (self.fixture.bin / "qt_cpp_shell_smoke.exe").write_bytes(pe_fixture("exe"))
        self.assert_failure("test_fixture_in_product:qt_cpp_shell_smoke.exe")

    def test_other_unlisted_file_is_rejected(self):
        (self.fixture.bin / "notes.txt").write_text("not a product file\n", encoding="utf-8")
        self.assert_failure("unexpected_product_file:notes.txt")

    def test_derived_runtime_manifest_is_ignored_and_never_staged(self):
        (self.fixture.bin / "runtime-manifest.json").write_text(
            '{"schema": "termforge-windows-runtime-v1"}\n', encoding="utf-8")
        result, out, _ = self.fixture.stage()
        self.assertEqual(result["status"], "passed")
        self.assertFalse((out / "runtime-manifest.json").exists())
        self.assertNotIn("runtime-manifest.json", json.loads(
            (out / "product-manifest.json").read_text(encoding="utf-8"))["files"])

    def test_tampered_build_components_are_rejected(self):
        for relative, code in (("Qt6Core.dll", "qt_identity"),
                               ("msvcp140.dll", "msvc_runtime_identity"),
                               ("conpty-backend/conpty.dll", "conpty_identity"),
                               ("assets/fonts/RobotoMono-Regular.ttf", "font_identity")):
            with self.subTest(relative=relative):
                fixture = Fixture(self)
                target = fixture.bin / relative
                target.write_bytes(target.read_bytes() + b"tampered")
                with self.assertRaises(stage_qt_product.StagingError) as raised:
                    fixture.stage()
                self.assertIn(code, str(raised.exception))
                self.assertFalse((fixture.build.parent / "staging").exists())

    def test_system_component_must_match_the_os_copy(self):
        (self.fixture.bin / "icuuc.dll").write_bytes(b"another icu source")
        self.assert_failure("os_component_identity:icuuc.dll")

    def test_dependency_summary_identity_is_enforced(self):
        summary = json.loads(self.fixture.summary.read_text(encoding="utf-8"))
        summary["sdk"]["tree_sha256"] = "f" * 64
        self.fixture.summary.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
        self.assert_failure("dependency_tree_identity")

    def test_dependency_manifest_binding_is_enforced(self):
        summary = json.loads(self.fixture.summary.read_text(encoding="utf-8"))
        summary["input_manifests"]["qt"] = "0" * 64
        self.fixture.summary.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
        self.assert_failure("dependency_manifest_mismatch")

    def test_dirty_source_requires_the_explicit_development_override(self):
        (self.fixture.root / "untracked.txt").write_text("dirty\n", encoding="utf-8")
        self.assert_failure("source_dirty")
        result, out, _ = self.fixture.stage(allow_dirty=True)
        self.assertEqual(result["status"], "passed")
        manifest = json.loads((out / "product-manifest.json").read_text(encoding="utf-8"))
        self.assertTrue(manifest["source"]["dirty"])
        self.assertTrue(manifest["source"]["development_override"])

    def test_existing_output_is_never_overlaid(self):
        out = self.fixture.build.parent / "staging"
        out.mkdir()
        marker = out / "keep.txt"
        marker.write_text("keep\n", encoding="utf-8")
        with self.assertRaises(stage_qt_product.StagingError) as raised:
            self.fixture.stage()
        self.assertIn("output_exists", str(raised.exception))
        self.assertEqual(marker.read_text(encoding="utf-8"), "keep\n")

    def test_build_configuration_is_frozen(self):
        cache = self.fixture.build / "CMakeCache.txt"
        text = cache.read_text(encoding="utf-8").replace(
            "TERMFORGE_BUILD_CANDIDATE_ADAPTER:BOOL=OFF",
            "TERMFORGE_BUILD_CANDIDATE_ADAPTER:BOOL=ON")
        cache.write_text(text, encoding="utf-8")
        self.assert_failure("build_configuration")

    def test_pinned_font_and_license_text_tampering_fails(self):
        (self.fixture.root / "assets/fonts/RobotoMono-Regular.ttf").write_bytes(b"tampered font\n")
        self.assert_failure("font_identity", allow_dirty=True)
        second = Fixture(self)
        (second.root / "third_party/qt/LICENSES/LGPL-3.0-only.txt").write_bytes(b"tampered license\n")
        with self.assertRaises(stage_qt_product.StagingError) as raised:
            second.stage(allow_dirty=True)
        self.assertIn("qt_license_identity", str(raised.exception))

    def test_pe_identity_rejects_wrong_architectures(self):
        with self.assertRaises(stage_qt_product.StagingError):
            stage_qt_product.pe_identity(pe_fixture("dll"), executable=True)
        wrong = bytearray(pe_fixture("exe"))
        struct.pack_into("<H", wrong, 132, 0x14C)
        with self.assertRaises(stage_qt_product.StagingError) as raised:
            stage_qt_product.pe_identity(bytes(wrong), executable=True)
        self.assertIn("pe_architecture", str(raised.exception))

    def test_cli_reports_a_path_free_failure(self):
        (self.fixture.bin / "termforge_qt_shell.exe").unlink()
        out = self.fixture.build.parent / "cli-staging"
        summary = self.fixture.build.parent / "cli-summary.json"
        stdout = io.StringIO()
        with redirect_stdout(stdout):
            code = stage_qt_product.main([
                "stage", "--bin-dir", str(self.fixture.bin),
                "--dependencies", str(self.fixture.deps),
                "--dependency-summary", str(self.fixture.summary),
                "--out", str(out), "--summary", str(summary),
                "--source-root", str(self.fixture.root)])
        result = json.loads(stdout.getvalue())
        self.assertEqual(code, 1)
        self.assertEqual(result["status"], "failed")
        self.assertIn("missing_product_file", result["error"])
        self.assertNotIn(str(self.fixture.root), stdout.getvalue())
        self.assertEqual(json.loads(summary.read_text(encoding="utf-8"))["status"], "failed")
        self.assertFalse(out.exists())

    def test_cli_verify_reports_a_path_free_failure(self):
        _, out, _ = self.fixture.stage()
        (out / "LICENSE").unlink()
        stdout = io.StringIO()
        with redirect_stdout(stdout):
            code = stage_qt_product.main(["verify", "--staging", str(out)])
        result = json.loads(stdout.getvalue())
        self.assertEqual(code, 1)
        self.assertEqual(result["operation"], "verify")
        self.assertIn("missing_product_file:LICENSE", result["error"])
        self.assertNotIn(str(self.fixture.root), stdout.getvalue())


if __name__ == "__main__":
    unittest.main()
