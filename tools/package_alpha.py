#!/usr/bin/env python3
"""Build the small, audited Windows Alpha distribution (Python stdlib only)."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import zipfile


VERSION = "0.1.0-alpha.1"
BINARIES = ("termforge_shell.exe", "shell_clipboard_reader.exe")
FONT_FILES = ("RobotoMono-Regular.ttf", "OFL.txt", "README.md")
SYSTEM_DLLS = frozenset((
    "advapi32.dll", "bcrypt.dll", "comctl32.dll", "comdlg32.dll", "d2d1.dll",
    "dwrite.dll", "gdi32.dll", "imm32.dll", "kernel32.dll", "ntdll.dll",
    "ole32.dll", "oleaut32.dll", "shell32.dll", "shlwapi.dll", "user32.dll",
    "uuid.dll", "version.dll", "winmm.dll", "ws2_32.dll",
))
EXPECTED_PATHS = frozenset((*BINARIES, "LICENSE", "README.txt", "build-info.json",
                            *(f"assets/fonts/{name}" for name in FONT_FILES)))


class PackageError(ValueError):
    """An input does not meet this release's distribution contract."""


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def inspect_pe(data: bytes) -> dict:
    """Inspect file-backed PE32+ imports, including delayed imports.

    Offsets follow Microsoft's PE format. No loading or execution is involved;
    unknown dependencies and malformed/unmapped pointers fail closed.
    """
    def unpack(fmt: str, offset: int):
        count = struct.calcsize(fmt)
        if offset < 0 or offset + count > len(data):
            raise PackageError("truncated PE structure")
        return struct.unpack_from(fmt, data, offset)

    if len(data) < 64 or data[:2] != b"MZ":
        raise PackageError("missing DOS/PE header")
    pe, = unpack("<I", 0x3c)
    if pe < 64 or data[pe:pe + 4] != b"PE\0\0":
        raise PackageError("invalid PE signature")
    machine, count, _, _, _, optional_size, characteristics = unpack("<HHIIIHH", pe + 4)
    if machine != 0x8664 or not 1 <= count <= 96:
        raise PackageError("expected Windows AMD64 executable")
    if not characteristics & 0x0002 or characteristics & 0x2000:
        raise PackageError("expected executable, not DLL")
    opt = pe + 24
    if optional_size < 112 or opt + optional_size > len(data):
        raise PackageError("truncated optional header")
    magic, = unpack("<H", opt)
    if magic != 0x20b:
        raise PackageError("expected PE32+ optional header")
    header_size, = unpack("<I", opt + 60)
    directory_count, = unpack("<I", opt + 108)
    if directory_count > 16 or optional_size < 112 + 8 * directory_count:
        raise PackageError("invalid data-directory table")
    section_table = opt + optional_size
    if not section_table + 40 * count <= header_size <= len(data):
        raise PackageError("invalid PE header size")
    sections = []
    for index in range(count):
        section = section_table + 40 * index
        _, address, raw_size, raw_offset = unpack("<IIII", section + 8)
        if raw_size and (raw_offset < header_size or raw_offset + raw_size > len(data)):
            raise PackageError("truncated or overlapping section data")
        if address + raw_size > 0x100000000:
            raise PackageError("section RVA overflow")
        sections.append((address, raw_size, raw_offset))

    def offset_for(rva: int, size: int = 1) -> int:
        if rva <= 0 or size < 1 or rva + size > 0x100000000:
            raise PackageError("invalid PE RVA")
        candidates = []
        if rva + size <= header_size:
            candidates.append(rva)
        for address, raw_size, raw_offset in sections:
            if address <= rva and rva + size <= address + raw_size:
                candidates.append(raw_offset + rva - address)
        if len(candidates) != 1:
            raise PackageError("unmapped or ambiguous PE RVA")
        return candidates[0]

    def dll_name(rva: int) -> str:
        name = bytearray()
        for index in range(260):
            byte = data[offset_for(rva + index)]
            if byte == 0:
                break
            name.append(byte)
        else:
            raise PackageError("unterminated DLL name")
        try:
            result = name.decode("ascii").lower()
        except UnicodeDecodeError as exc:
            raise PackageError("non-ASCII DLL name") from exc
        if not re.fullmatch(r"[a-z0-9_-]+\.dll", result):
            raise PackageError("invalid DLL name")
        if (result.startswith(("msvcp", "vcruntime", "api-ms-win-crt-"))
                or result == "ucrtbase.dll"):
            raise PackageError(f"dynamic C/C++ runtime dependency: {result}")
        if result not in SYSTEM_DLLS:
            raise PackageError(f"unapproved non-system dependency: {result}")
        return result

    def imports(directory: int, stride: int, name_offset: int) -> list[str]:
        if directory >= directory_count:
            return []
        rva, size = unpack("<II", opt + 112 + directory * 8)
        if not rva and not size:
            return []
        if not rva or size < stride:
            raise PackageError("invalid import directory")
        offset_for(rva, size)
        result = set()
        for index in range(min(size // stride, 128)):
            entry = offset_for(rva + index * stride, stride)
            if data[entry:entry + stride] == bytes(stride):
                return sorted(result)
            if directory == 13:
                attributes, = unpack("<I", entry)
                if attributes != 1:
                    raise PackageError("unsupported delayed-import attributes")
            name_rva, = unpack("<I", entry + name_offset)
            result.add(dll_name(name_rva))
        raise PackageError("unterminated or excessive import descriptors")

    normal = imports(1, 20, 12)
    delayed = imports(13, 32, 4)
    if not normal:
        raise PackageError("executable has no normal system imports")
    return {"machine": "AMD64", "imports": normal, "delay_imports": delayed}


def read_file(path: Path) -> bytes:
    if path.is_symlink() or not path.is_file():
        raise PackageError(f"missing regular file: {path.name}")
    return path.read_bytes()


def source_identity(root: Path, allow_dirty: bool) -> dict:
    def git(*args: str) -> str:
        result = subprocess.run(["git", "-C", str(root), *args], check=True,
                                capture_output=True, text=True, timeout=20)
        return result.stdout.strip()

    commit = git("rev-parse", "HEAD")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise PackageError("expected full Git source commit")
    dirty = bool(git("status", "--porcelain", "--untracked-files=all"))
    if dirty and not allow_dirty:
        raise PackageError("source tree is dirty; commit before release packaging")
    timestamp = int(git("show", "-s", "--format=%ct", "HEAD"))
    return {"commit": commit, "commit_timestamp": timestamp, "dirty": dirty,
            "development_override": allow_dirty,
            "release_eligible": not dirty and not allow_dirty}


def build_configuration(bin_dir: Path) -> dict:
    cache = next((parent / "CMakeCache.txt" for parent in (bin_dir, *bin_dir.parents[:3])
                  if (parent / "CMakeCache.txt").is_file()), None)
    if cache is None or bin_dir.name != "Release":
        raise PackageError("expected Release bin directory with parent CMakeCache.txt")
    values = {}
    for line in cache.read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"([^:#][^:]*):[^=]+=(.*)", line)
        if match:
            values[match[1]] = match[2]
    if values.get("CMAKE_MSVC_RUNTIME_LIBRARY") != "MultiThreaded":
        raise PackageError("CMake build must select MultiThreaded (/MT)")
    if values.get("CMAKE_GENERATOR_PLATFORM", "").lower() != "x64":
        raise PackageError("CMake build must select x64")
    return {"configuration": "Release", "architecture": "x64",
            "cmake_msvc_runtime_library": "MultiThreaded"}


def json_bytes(value: dict) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")


def make_package(root: Path, bin_dir: Path, out_dir: Path, version: str,
                 allow_dirty: bool = False) -> dict:
    if version != VERSION:
        raise PackageError(f"this release packager supports only {VERSION}")
    source = source_identity(root, allow_dirty)
    configuration = build_configuration(bin_dir)
    files = {name: read_file(bin_dir / name) for name in BINARIES}
    pe = {name: inspect_pe(files[name]) for name in BINARIES}
    for name in FONT_FILES:
        path = f"assets/fonts/{name}"
        files[path] = read_file(bin_dir / path)
        if files[path] != read_file(root / path):
            raise PackageError(f"bundled font/license does not match source: {name}")
    files["LICENSE"] = read_file(root / "LICENSE")
    template = read_file(root / "packaging/windows/README.txt").decode("utf-8")
    if template.count("@VERSION@") != 1 or template.count("@COMMIT@") != 1:
        raise PackageError("README template must contain each release placeholder once")
    files["README.txt"] = template.replace("@VERSION@", version).replace(
        "@COMMIT@", source["commit"]).encode("utf-8")
    info = {"schema_version": 1, "project": "TermForge", "version": version,
            "platform": "windows-x64", "source": source, "build": configuration,
            "executables": pe, "files": {
                name: {"bytes": len(data), "sha256": sha256(data)}
                for name, data in sorted(files.items())}}
    manifest = json_bytes(info)
    files["build-info.json"] = manifest
    if set(files) != EXPECTED_PATHS:
        raise PackageError("package allowlist mismatch")
    stem = f"TermForge-v{version}-windows-x64"
    buffer = io.BytesIO()
    stamp = datetime.fromtimestamp(source["commit_timestamp"], timezone.utc)
    if not 1980 <= stamp.year <= 2107:
        raise PackageError("source commit timestamp is outside ZIP range")
    date_time = (stamp.year, stamp.month, stamp.day, stamp.hour, stamp.minute, stamp.second)
    with zipfile.ZipFile(buffer, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(files.items()):
            entry = zipfile.ZipInfo(f"{stem}/{name}", date_time)
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.create_system = 3
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    archive_bytes = buffer.getvalue()
    checksums = (f"{sha256(archive_bytes)}  {stem}.zip\n"
                 f"{sha256(manifest)}  build-info.json\n").encode("ascii")
    outputs = {f"{stem}.zip": archive_bytes, "build-info.json": manifest,
               "SHA256SUMS.txt": checksums}
    out_dir.mkdir(parents=True, exist_ok=True)
    if any((out_dir / name).exists() for name in outputs):
        raise PackageError("release output already exists; choose a fresh directory")
    # Exclusive create prevents replacement even if another process wins the check.
    created = []
    try:
        for name, data in outputs.items():
            with (out_dir / name).open("xb") as handle:
                created.append(out_dir / name)
                handle.write(data)
    except BaseException:
        for path in created:
            path.unlink(missing_ok=True)
        raise
    return {"version": version, "commit": source["commit"],
            "release_eligible": source["release_eligible"],
            "zip": f"{stem}.zip", "zip_sha256": sha256(archive_bytes),
            "files": len(files)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-dir", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--version", required=True, choices=[VERSION])
    parser.add_argument("--allow-dirty-development", action="store_true",
                        help="local development only; marks package ineligible for release")
    args = parser.parse_args()
    try:
        result = make_package(Path(__file__).resolve().parents[1], args.bin_dir.resolve(),
                              args.out_dir.resolve(), args.version, args.allow_dirty_development)
    except (PackageError, OSError, UnicodeError, subprocess.SubprocessError) as exc:
        print(f"Alpha packaging failed: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
