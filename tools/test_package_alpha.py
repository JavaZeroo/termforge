"""Contract tests for the release archive and malformed/dependent PE rejection."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zipfile

if __package__:
    from .package_alpha import (BINARIES, EXPECTED_PATHS, FONT_FILES, PackageError,
                                VERSION, inspect_pe, make_package)
else:
    from package_alpha import (BINARIES, EXPECTED_PATHS, FONT_FILES, PackageError,
                               VERSION, inspect_pe, make_package)


def pe_fixture(dll: str = "KERNEL32.dll", delay: str | None = None) -> bytes:
    """Small file-backed PE fixture; never executed as a program."""
    data = bytearray(1024)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3c, 128)
    data[128:132] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", data, 132, 0x8664, 1, 0, 0, 0, 240, 0x22)
    opt = 152
    struct.pack_into("<H", data, opt, 0x20b)
    struct.pack_into("<I", data, opt + 60, 512)
    struct.pack_into("<I", data, opt + 108, 16)
    struct.pack_into("<II", data, opt + 120, 0x1000, 40)
    section = opt + 240
    data[section:section + 8] = b".rdata\0\0"
    struct.pack_into("<IIII", data, section + 8, 512, 0x1000, 512, 512)
    struct.pack_into("<IIIII", data, 512, 0, 0, 0, 0x1030, 0x1040)
    name = dll.encode("ascii") + b"\0"
    data[560:560 + len(name)] = name
    if delay:
        struct.pack_into("<II", data, opt + 112 + 13 * 8, 0x1080, 64)
        struct.pack_into("<II", data, 640, 1, 0x10c0)
        name = delay.encode("ascii") + b"\0"
        data[704:704 + len(name)] = name
    return bytes(data)


class InspectPeTests(unittest.TestCase):
    def test_system_dependencies_normal_and_delay(self):
        self.assertEqual(inspect_pe(pe_fixture("USER32.dll", "DWrite.dll")), {
            "machine": "AMD64", "imports": ["user32.dll"],
            "delay_imports": ["dwrite.dll"],
        })

    def test_rejects_wrong_architecture_and_pe32(self):
        for offset, value in ((132, 0x14c), (152, 0x10b)):
            with self.subTest(offset=offset):
                data = bytearray(pe_fixture())
                struct.pack_into("<H", data, offset, value)
                with self.assertRaises(PackageError):
                    inspect_pe(bytes(data))

    def test_rejects_truncated_headers_and_section(self):
        data = pe_fixture()
        for length in (0, 63, 150, 400, 900):
            with self.subTest(length=length), self.assertRaises(PackageError):
                inspect_pe(data[:length])

    def test_rejects_dynamic_runtime_normal_and_delayed(self):
        for name in ("MSVCP140.dll", "VCRUNTIME140_1.dll", "ucrtbase.dll",
                     "api-ms-win-crt-runtime-l1-1-0.dll"):
            for delayed in (False, True):
                with self.subTest(name=name, delayed=delayed):
                    data = pe_fixture(delay=name) if delayed else pe_fixture(name)
                    with self.assertRaisesRegex(PackageError, "runtime dependency"):
                        inspect_pe(data)

    def test_rejects_unknown_and_path_dependencies(self):
        for name in ("custom.dll", "../user32.dll", "C:\\user32.dll"):
            with self.subTest(name=name), self.assertRaises(PackageError):
                inspect_pe(pe_fixture(name))

    def test_rejects_unmapped_name_and_unterminated_directory(self):
        data = bytearray(pe_fixture())
        struct.pack_into("<I", data, 512 + 12, 0x2000)
        with self.assertRaisesRegex(PackageError, "unmapped"):
            inspect_pe(bytes(data))
        data = bytearray(pe_fixture())
        struct.pack_into("<I", data, 152 + 124, 20)
        with self.assertRaisesRegex(PackageError, "unterminated"):
            inspect_pe(bytes(data))

    def test_rejects_old_va_delay_descriptor(self):
        data = bytearray(pe_fixture(delay="USER32.dll"))
        struct.pack_into("<I", data, 640, 0)
        with self.assertRaisesRegex(PackageError, "delayed-import attributes"):
            inspect_pe(bytes(data))


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="termforge alpha 测试 ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.bin = self.root / "build/bin/Release"
        self.bin.mkdir(parents=True)
        (self.root / ".gitignore").write_text("/build/\n/dist*/\n", encoding="utf-8")
        (self.root / "LICENSE").write_bytes(b"MIT fixture\n")
        (self.root / "packaging/windows").mkdir(parents=True)
        (self.root / "packaging/windows/README.txt").write_text(
            "TermForge @VERSION@\nsource @COMMIT@\n", encoding="utf-8")
        for name in FONT_FILES:
            for parent in (self.root, self.bin):
                path = parent / "assets/fonts" / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(f"fixture {name}\n".encode())
        for name in BINARIES:
            (self.bin / name).write_bytes(pe_fixture())
        (self.root / "build/CMakeCache.txt").write_text(
            "CMAKE_MSVC_RUNTIME_LIBRARY:UNINITIALIZED=MultiThreaded\n"
            "CMAKE_GENERATOR_PLATFORM:INTERNAL=x64\n", encoding="utf-8")
        self.git("init", "-q")
        self.git("add", ".")
        self.git("-c", "user.name=Package Test", "-c", "user.email=test@example.invalid",
                 "commit", "-qm", "fixture source")

    def git(self, *args):
        return subprocess.run(["git", "-C", str(self.root), *args], check=True,
                              capture_output=True, text=True, timeout=15).stdout.strip()

    def package(self, directory="dist", allow_dirty=False):
        return make_package(self.root, self.bin, self.root / directory, VERSION, allow_dirty)

    def test_exact_archive_identity_checksums_and_determinism(self):
        # The build directory contains tests/PDBs; none may leak into the package.
        (self.bin / "private.pdb").write_bytes(b"not distributable")
        (self.bin / "unrelated.exe").write_bytes(b"not distributable")
        first = self.package()
        second = self.package("dist2")
        self.assertTrue(first["release_eligible"])
        self.assertEqual(first, second)
        out = self.root / "dist"
        archive_bytes = (out / first["zip"]).read_bytes()
        self.assertEqual(archive_bytes, (self.root / "dist2" / second["zip"]).read_bytes())
        prefix = f"TermForge-v{VERSION}-windows-x64/"
        with zipfile.ZipFile(out / first["zip"]) as archive:
            names = archive.namelist()
            self.assertEqual(names, sorted(names))
            self.assertEqual({name.removeprefix(prefix) for name in names}, EXPECTED_PATHS)
            self.assertTrue(all(name.startswith(prefix) and ".." not in name.split("/")
                                and not name.startswith("/") for name in names))
            manifest = archive.read(prefix + "build-info.json")
            self.assertEqual(manifest, (out / "build-info.json").read_bytes())
            info = json.loads(manifest)
            self.assertNotIn(str(self.root), manifest.decode())
            self.assertEqual(info["source"]["commit"], self.git("rev-parse", "HEAD"))
            self.assertEqual(set(info["files"]), EXPECTED_PATHS - {"build-info.json"})
            for name, identity in info["files"].items():
                data = archive.read(prefix + name)
                self.assertEqual(identity, {"bytes": len(data),
                                           "sha256": hashlib.sha256(data).hexdigest()})
            readme = archive.read(prefix + "README.txt").decode()
            self.assertIn(VERSION, readme)
            self.assertNotIn("@COMMIT@", readme)
        for line in (out / "SHA256SUMS.txt").read_text().splitlines():
            digest, name = line.split("  ")
            self.assertEqual(digest, hashlib.sha256((out / name).read_bytes()).hexdigest())

    def test_refuses_overwrite_without_altering_existing_outputs(self):
        first = self.package()
        path = self.root / "dist" / first["zip"]
        before = path.read_bytes()
        with self.assertRaisesRegex(PackageError, "already exists"):
            self.package()
        self.assertEqual(path.read_bytes(), before)

    def test_dirty_or_untracked_source_rejected_and_development_marked(self):
        for path in (self.root / "LICENSE", self.root / "new-source.cpp"):
            with self.subTest(path=path.name):
                original = path.read_bytes() if path.exists() else None
                path.write_bytes(b"changed\n")
                with self.assertRaisesRegex(PackageError, "dirty"):
                    self.package()
                if original is None:
                    path.unlink()
                else:
                    path.write_bytes(original)
        (self.root / "LICENSE").write_bytes(b"changed\n")
        result = self.package(allow_dirty=True)
        self.assertFalse(result["release_eligible"])
        info = json.loads((self.root / "dist/build-info.json").read_bytes())
        self.assertTrue(info["source"]["dirty"])

    def test_development_override_is_ineligible_even_for_clean_tree(self):
        result = self.package(allow_dirty=True)
        self.assertFalse(result["release_eligible"])
        info = json.loads((self.root / "dist/build-info.json").read_bytes())
        self.assertFalse(info["source"]["dirty"])
        self.assertTrue(info["source"]["development_override"])

    def test_missing_helper_and_font_rejected(self):
        for name in ("shell_clipboard_reader.exe", "assets/fonts/OFL.txt"):
            path = self.bin / name
            data = path.read_bytes()
            path.unlink()
            with self.subTest(name=name), self.assertRaisesRegex(PackageError, "missing"):
                self.package()
            path.write_bytes(data)

    def test_stale_font_and_dynamic_runtime_rejected(self):
        path = self.bin / "assets/fonts/RobotoMono-Regular.ttf"
        original = path.read_bytes()
        path.write_bytes(b"stale font")
        with self.assertRaisesRegex(PackageError, "does not match source"):
            self.package()
        path.write_bytes(original)
        (self.bin / "termforge_shell.exe").write_bytes(pe_fixture("MSVCP140.dll"))
        with self.assertRaisesRegex(PackageError, "runtime dependency"):
            self.package()

    def test_wrong_configuration_and_template_rejected(self):
        cache = self.root / "build/CMakeCache.txt"
        original = cache.read_text()
        cache.write_text(original.replace("MultiThreaded\n", "MultiThreadedDLL\n"))
        with self.assertRaisesRegex(PackageError, "MultiThreaded"):
            self.package()
        cache.write_text(original)
        template = self.root / "packaging/windows/README.txt"
        template.write_text("@VERSION@ @VERSION@ @COMMIT@")
        with self.assertRaisesRegex(PackageError, "placeholder"):
            self.package(allow_dirty=True)

    def test_other_versions_rejected(self):
        with self.assertRaisesRegex(PackageError, "supports only"):
            make_package(self.root, self.bin, self.root / "dist", "0.1.0")


if __name__ == "__main__":
    unittest.main()
