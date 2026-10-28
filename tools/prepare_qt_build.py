"""Prepare the manifest-pinned Windows Qt/ConPTY/CRT build inputs locally.

Only untouched archives are cached. Each run extracts a fresh SDK, adds a
relative qt.conf, verifies the required files, and publishes a path-free summary.
The CRT installer is read as an archive, never executed. No global installation,
PATH modification, or product redistribution occurs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "termforge-qt-build-preparation-v1"


class PreparationError(Exception):
    """A fixed public code, never a raw process, network, or filesystem message."""


def sha256(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def acquire(cache, name, url, size, digest, offline):
    target = cache / name
    if target.exists():
        if not target.is_file() or target.stat().st_size != size or sha256(target) != digest:
            raise PreparationError("archive_identity")
        return target
    if offline:
        raise PreparationError("archive_missing")
    cache.mkdir(parents=True, exist_ok=True)
    # Never leave an interrupted or unverified response under a cache filename.
    with tempfile.TemporaryDirectory(prefix="download-", dir=cache) as temporary:
        candidate = Path(temporary) / "archive"
        received = 0
        try:
            with urllib.request.urlopen(url, timeout=45) as response, candidate.open("wb") as output:
                while chunk := response.read(1024 * 1024):
                    received += len(chunk)
                    if received > size:
                        raise PreparationError("archive_identity")
                    output.write(chunk)
        except OSError:
            raise PreparationError("download_failed") from None
        if received != size or sha256(candidate) != digest:
            raise PreparationError("archive_identity")
        candidate.replace(target)
    return target


def run_tool(arguments, cwd=None):
    try:
        result = subprocess.run(arguments, cwd=cwd, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, timeout=180, check=False)
    except (OSError, subprocess.TimeoutExpired):
        raise PreparationError("sdk_tool_failed") from None
    if result.returncode:
        raise PreparationError("sdk_tool_failed")
    return result.stdout.decode("utf-8", "replace").strip()


def tree_identity(directory):
    """Bind every SDK file after relocation without publishing a path inventory."""
    identity = hashlib.sha256()
    count = 0
    for path in sorted(directory.rglob("*")):
        if path.is_file():
            identity.update(path.relative_to(directory).as_posix().encode("utf-8") + b"\0")
            identity.update(sha256(path).encode("ascii") + b"\n")
            count += 1
    return {"files": count, "tree_sha256": identity.hexdigest()}


def prepare_runtime(cache, staging, offline):
    """Extract the fixed AMD64 CRT without executing its installer."""
    metadata = json.loads((ROOT / "third_party/msvc_runtime/manifest.json").read_text(encoding="utf-8"))
    package, license_metadata = metadata["package"], metadata["license"]
    installer = acquire(cache, package["archive"], package["url"], package["bytes"], package["sha256"], offline)
    license_path = acquire(cache, license_metadata["archive"], license_metadata["url"],
                           license_metadata["bytes"], license_metadata["sha256"], offline)
    notice = ROOT / "third_party/msvc_runtime/NOTICE.txt"
    if sha256(notice) != metadata["notice_sha256"]:
        raise PreparationError("crt_notice_identity")
    data = installer.read_bytes()
    container = metadata["attached_container"]
    offset, size = container["offset"], container["bytes"]
    if (type(offset) is not int or type(size) is not int or offset < 0 or size < 36 or
            offset + size > len(data) or hashlib.sha256(data).hexdigest() != package["sha256"]):
        raise PreparationError("crt_container_identity")
    cabinet = data[offset:offset + size]
    if (cabinet[:4] != b"MSCF" or struct.unpack_from("<I", cabinet, 8)[0] != size or
            hashlib.sha256(cabinet).hexdigest() != container["sha256"]):
        raise PreparationError("crt_container_identity")
    work = staging.parent / "crt-extraction"
    work.mkdir()
    (work / "attached.cab").write_bytes(cabinet)
    member = metadata["runtime_cabinet"]["member"]
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", member):
        raise PreparationError("crt_member_name")
    run_tool(["cmake", "-E", "tar", "xf", "attached.cab", member], cwd=work)
    runtime_cabinet = work / member
    if (runtime_cabinet.stat().st_size != metadata["runtime_cabinet"]["bytes"] or
            sha256(runtime_cabinet) != metadata["runtime_cabinet"]["sha256"]):
        raise PreparationError("crt_cabinet_identity")
    members = []
    for name, identity in metadata["files"].items():
        if (not re.fullmatch(r"[a-z0-9_]+\.dll", name) or
                not re.fullmatch(r"[a-z0-9_]+\.dll_amd64", identity["member"])):
            raise PreparationError("crt_member_name")
        members.append(identity["member"])
    run_tool(["cmake", "-E", "tar", "xf", member, *members], cwd=work)
    runtime = staging / "crt"
    runtime.mkdir()
    for name, identity in metadata["files"].items():
        contents = (work / identity["member"]).read_bytes()
        if len(contents) != identity["bytes"] or hashlib.sha256(contents).hexdigest() != identity["sha256"]:
            raise PreparationError("crt_file_identity")
        if len(contents) < 64 or contents[:2] != b"MZ":
            raise PreparationError("crt_architecture")
        pe = struct.unpack_from("<I", contents, 60)[0]
        if pe + 6 > len(contents) or contents[pe:pe + 4] != b"PE\0\0" or struct.unpack_from("<H", contents, pe + 4)[0] != 0x8664:
            raise PreparationError("crt_architecture")
        (runtime / name).write_bytes(contents)
    shutil.copyfile(license_path, runtime / "LICENSE-MICROSOFT-VC-REDIST.docx")
    shutil.copyfile(notice, runtime / "NOTICE.txt")
    return {"version": metadata["version"], "architecture": metadata["architecture"],
            "package_sha256": package["sha256"], "files": {name: value["sha256"] for name, value in metadata["files"].items()},
            "license_sha256": license_metadata["sha256"], "notice_sha256": metadata["notice_sha256"]}


def prepare(cache, out, offline, crt_only=False):
    if out.exists():
        raise PreparationError("output_exists")
    if crt_only:
        out.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="crt-build-", dir=out.parent) as temporary:
            staging = Path(temporary) / "dependencies"
            staging.mkdir()
            runtime = prepare_runtime(cache, staging, offline)
            staging.replace(out)
        return {"schema": SCHEMA, "status": "passed", "msvc_runtime": runtime,
                "input_manifests": {"msvc_runtime": sha256(ROOT / "third_party/msvc_runtime/manifest.json")},
                "preparation_script_sha256": sha256(Path(__file__)),
                "scope": "Fixed CRT build inputs only; no installer execution or product acceptance"}
    qt = json.loads((ROOT / "third_party/qt/manifest.json").read_text(encoding="utf-8"))
    conpty = json.loads((ROOT / "third_party/conpty/manifest.json").read_text(encoding="utf-8"))
    archive = qt["official_binary_archive"]
    qt_archive = acquire(cache, archive["archive"], archive["url"], archive["bytes"], archive["sha256"], offline)
    package = acquire(cache, conpty["package_url"].rsplit("/", 1)[1], conpty["package_url"],
                      conpty["package_bytes"], conpty["package_sha256"], offline)
    license_path = ROOT / "third_party/conpty/LICENSE"
    if sha256(license_path) != conpty["bundled_license_sha256"]:
        raise PreparationError("conpty_license_identity")
    out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="qt-build-", dir=out.parent) as temporary:
        staging = Path(temporary) / "dependencies"
        sdk, backend = staging / "qt", staging / "conpty"
        sdk.mkdir(parents=True)
        backend.mkdir()
        # CMake's libarchive reader handles the fixed 7z directly. The verified
        # archive contains the SDK prefix itself, not an outer version folder.
        run_tool(["cmake", "-E", "tar", "xf", str(qt_archive)], cwd=sdk)
        required = ["bin/qmake.exe", "bin/windeployqt.exe", "bin/Qt6Core.dll",
                    "bin/Qt6Gui.dll", "bin/Qt6Widgets.dll", "bin/Qt6Test.dll",
                    "plugins/platforms/qwindows.dll", "plugins/platforms/qoffscreen.dll",
                    "lib/cmake/Qt6/Qt6Config.cmake", "lib/cmake/Qt6Widgets/Qt6WidgetsConfig.cmake",
                    "lib/cmake/Qt6Test/Qt6TestConfig.cmake", "sbom/qtbase-6.10.3.spdx.json"]
        if any(not (sdk / name).is_file() for name in required):
            raise PreparationError("sdk_missing_file")
        if sha256(sdk / qt["license"]["evidence"]["sdk_sbom_relative_path"]) != qt["license"]["evidence"]["sdk_sbom_sha256"]:
            raise PreparationError("sdk_sbom_identity")
        # Relative relocation leaves official qmake/.prl bytes untouched. CMake
        # imports resolve their own prefix; Qt tools read this adjacent qt.conf.
        (sdk / "bin/qt.conf").write_text("[Paths]\nPrefix=..\n", encoding="utf-8", newline="\n")
        if run_tool([str(sdk / "bin/qmake.exe"), "-query", "QT_VERSION"]) != qt["version"]:
            raise PreparationError("sdk_version")
        with zipfile.ZipFile(package) as source:
            for name, member in conpty["package_members"].items():
                try:
                    contents = source.read(member)
                except KeyError:
                    raise PreparationError("conpty_missing_member") from None
                if hashlib.sha256(contents).hexdigest() != conpty["files"][name]:
                    raise PreparationError("conpty_file_identity")
                (backend / name).write_bytes(contents)
        sdk_identity = tree_identity(sdk)
        runtime = prepare_runtime(cache, staging, offline)
        staging.replace(out)
    return {"schema": SCHEMA, "status": "passed", "qt_version": qt["version"],
            "qt_architecture": qt["architecture"], "qt_archive_sha256": archive["sha256"],
            "input_manifests": {name: sha256(ROOT / "third_party" / name / "manifest.json")
                                for name in ("qt", "conpty", "msvc_runtime")},
            "preparation_script_sha256": sha256(Path(__file__)),
            "sdk_transformation": "bin/qt.conf contains [Paths] and Prefix=..; no binary or .prl patch",
            "sdk": sdk_identity, "conpty_version": conpty["version"],
            "conpty_package_sha256": conpty["package_sha256"], "conpty_files": conpty["files"],
            "msvc_runtime": runtime,
            "scope": "Windows local/CI build inputs only; no product package or platform acceptance"}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, default=ROOT / "local/qt-downloads")
    parser.add_argument("--out", type=Path, default=ROOT / "local/qt-build-deps")
    parser.add_argument("--summary", type=Path, default=ROOT / "local/qt-build-preparation.json")
    parser.add_argument("--offline", action="store_true", help="Require all selected pinned archives and license documents in cache")
    parser.add_argument("--crt-only", action="store_true", help="Prepare only the fixed Microsoft CRT and its license")
    arguments = parser.parse_args(argv)
    try:
        result = prepare(arguments.cache.resolve(), arguments.out.resolve(), arguments.offline, arguments.crt_only)
    except PreparationError as error:
        result = {"schema": SCHEMA, "status": "failed", "error": str(error)}
    except (OSError, ValueError, KeyError, zipfile.BadZipFile):
        result = {"schema": SCHEMA, "status": "failed", "error": "dependency_input_failed"}
    report = json.dumps(result, indent=2) + "\n"
    try:
        arguments.summary.parent.mkdir(parents=True, exist_ok=True)
        arguments.summary.write_text(report, encoding="utf-8", newline="\n")
    except (OSError, ValueError):
        result = {"schema": SCHEMA, "status": "failed", "error": "summary_write_failed",
                  "preparation_status": result["status"], "preparation_error": result.get("error")}
        report = json.dumps(result, indent=2) + "\n"
    print(report, end="")
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
