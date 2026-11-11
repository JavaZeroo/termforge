"""Stage the fixed Windows x64 C++/Qt/ConPTY product from a verified build.

Only files frozen by ``packaging/qt-product/manifest.json`` and the pinned
third-party manifests enter the staging directory. Each staged file must be
byte-identical to its prepared dependency or source-tree origin, license
evidence is carried next to the runtime files, and the resulting manifest is
path-free. This tool never downloads, executes, signs, publishes or accepts a
product package; clean-machine and R2 workflow validation remain separate.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "termforge-qt-product-manifest-v1"
OPERATION_SCHEMA = "termforge-qt-product-operation-v1"
CONTRACT_PATH = "packaging/qt-product/manifest.json"
PREPARATION_SCHEMA = "termforge-qt-build-preparation-v1"
COMPONENT_SCHEMA = "termforge-qt-deployed-components-v1"
ENTRY = "termforge_qt_shell.exe"
NOTICES_PATH = "THIRD-PARTY-NOTICES.txt"
QT_NOTICES_PATH = "QT-THIRD-PARTY-NOTICES.txt"
QT_INVENTORY_STAGED = "licenses/qt/components.json"
QT_LICENSE_DIR = "licenses/qt"
MANIFEST_PATH = "product-manifest.json"
MAX_DETAIL = 8

if __package__:
    from tools.prepare_qt_build import tree_identity
    from tools.build_qt_component_inventory import (derive_deployed_files, license_ids,
                                                    sbom_component_map)
else:  # pragma: no cover - direct script execution is diagnostic only
    from prepare_qt_build import tree_identity
    from build_qt_component_inventory import (derive_deployed_files, license_ids,
                                              sbom_component_map)


class StagingError(Exception):
    """A fixed public code; never a raw process, network or filesystem message."""


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_path(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def read_json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        raise StagingError("input_unreadable") from None
    if not isinstance(value, dict):
        raise StagingError("input_schema")
    return value


def relative_files(directory: Path) -> list[str]:
    """Sorted relative POSIX file paths, rejecting reparse points."""
    if not directory.is_dir():
        raise StagingError("input_directory")
    found = []
    pending = [directory]
    while pending:
        for path in sorted(pending.pop().iterdir()):
            try:
                information = path.lstat()
            except OSError:
                raise StagingError("input_unreadable") from None
            if stat.S_ISLNK(information.st_mode) or bool(
                    getattr(information, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT):
                raise StagingError("reparse_point")
            if path.is_dir():
                pending.append(path)
            elif path.is_file():
                found.append(path.relative_to(directory).as_posix())
    return sorted(found)


def pe_identity(data: bytes, executable: bool) -> dict:
    """Read a file-backed PE32+ AMD64 identity without loading or executing it."""
    if len(data) < 64 or data[:2] != b"MZ":
        raise StagingError("pe_header")
    offset = struct.unpack_from("<I", data, 0x3C)[0]
    if offset < 64 or offset + 24 > len(data) or data[offset:offset + 4] != b"PE\0\0":
        raise StagingError("pe_header")
    machine, sections, _, _, _, optional_size, characteristics = struct.unpack_from("<HHIIIHH", data, offset + 4)
    if machine != 0x8664 or not 1 <= sections <= 96 or offset + 24 + optional_size > len(data):
        raise StagingError("pe_architecture")
    magic = struct.unpack_from("<H", data, offset + 24)[0]
    if magic != 0x20B:
        raise StagingError("pe_architecture")
    is_dll = bool(characteristics & 0x2000)
    is_executable = bool(characteristics & 0x0002)
    if executable and (not is_executable or is_dll):
        raise StagingError("pe_entry")
    if not executable and not is_dll:
        raise StagingError("pe_architecture")
    return {"machine": "AMD64", "kind": "executable" if executable else "dll"}


def source_identity(root: Path, allow_dirty: bool) -> dict:
    def git(*arguments: str) -> str:
        try:
            result = subprocess.run(["git", "-C", str(root), *arguments], check=False,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
        except (OSError, subprocess.TimeoutExpired):
            raise StagingError("source_identity") from None
        if result.returncode:
            raise StagingError("source_identity")
        return result.stdout.decode("utf-8", "replace").strip()

    commit = git("rev-parse", "HEAD")
    tree = git("rev-parse", "HEAD^{tree}")
    if not re.fullmatch(r"[0-9a-f]{40}", commit) or not re.fullmatch(r"[0-9a-f]{40}", tree):
        raise StagingError("source_identity")
    dirty = bool(git("status", "--porcelain", "--untracked-files=all"))
    if dirty and not allow_dirty:
        raise StagingError("source_dirty")
    try:
        timestamp = int(git("show", "-s", "--format=%ct", "HEAD"))
    except ValueError:
        raise StagingError("source_identity") from None
    return {"commit": commit, "tree": tree, "commit_timestamp": timestamp,
            "dirty": dirty, "development_override": allow_dirty}


def build_configuration(bin_dir: Path) -> dict:
    if bin_dir.name != "Release":
        raise StagingError("build_directory")
    cache = next((parent / "CMakeCache.txt" for parent in (bin_dir, *bin_dir.parents[:3])
                  if (parent / "CMakeCache.txt").is_file()), None)
    if cache is None:
        raise StagingError("build_cache_missing")
    values = {}
    try:
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            match = re.fullmatch(r"([^:#][^:]*):[^=]+=(.*)", line)
            if match:
                values[match[1]] = match[2]
    except OSError:
        raise StagingError("build_cache_missing") from None
    if values.get("CMAKE_GENERATOR_PLATFORM", "").lower() != "x64":
        raise StagingError("build_configuration")
    if values.get("TERMFORGE_BUILD_QT_SHELL", "OFF").upper() != "ON":
        raise StagingError("build_configuration")
    if values.get("TERMFORGE_BUILD_CANDIDATE_ADAPTER", "ON").upper() != "OFF":
        raise StagingError("build_configuration")
    if "Release" not in values.get("CMAKE_CONFIGURATION_TYPES", "Release").split(";"):
        raise StagingError("build_configuration")
    toolset = None
    linker = values.get("CMAKE_LINKER", "")
    match = re.search(r"/VC/Tools/MSVC/(14[.][0-9]+[.][0-9]+)/bin/", linker.replace("\\", "/"))
    if match:
        toolset = match.group(1)
    return {"generator": values.get("CMAKE_GENERATOR", ""), "architecture": "x64",
            "configuration": "Release", "target": "termforge_qt_shell",
            "qt_shell_enabled": True, "candidate_adapter_enabled": False,
            "msvc_toolset_version": toolset}


def verify_dependency_summary(root: Path, dependencies: Path, summary_path: Path) -> dict:
    summary = read_json(summary_path)
    if summary.get("schema") != PREPARATION_SCHEMA or summary.get("status") != "passed":
        raise StagingError("dependency_summary_invalid")
    input_manifests = summary.get("input_manifests")
    if not isinstance(input_manifests, dict):
        raise StagingError("dependency_summary_invalid")
    for name in ("qt", "conpty", "msvc_runtime"):
        manifest = root / "third_party" / name / "manifest.json"
        if not manifest.is_file() or input_manifests.get(name) != sha256_path(manifest):
            raise StagingError("dependency_manifest_mismatch")
    script = root / "tools" / "prepare_qt_build.py"
    if not script.is_file() or summary.get("preparation_script_sha256") != sha256_path(script):
        raise StagingError("dependency_script_mismatch")

    sdk = summary.get("sdk") or {}
    if (dependencies / "qt").is_dir():
        identity = tree_identity(dependencies / "qt")
        if (identity["files"] != sdk.get("files") or
                identity["tree_sha256"] != sdk.get("tree_sha256")):
            raise StagingError("dependency_tree_identity")
    else:
        raise StagingError("dependency_directory")

    conpty_manifest = read_json(root / "third_party" / "conpty" / "manifest.json")
    if summary.get("qt_archive_sha256") != read_json(
            root / "third_party" / "qt" / "manifest.json").get("official_binary_archive", {}).get("sha256"):
        raise StagingError("dependency_manifest_identity")
    if summary.get("conpty_package_sha256") != conpty_manifest.get("package_sha256"):
        raise StagingError("dependency_manifest_identity")
    for name, digest in (summary.get("conpty_files") or {}).items():
        path = dependencies / "conpty" / name
        if not path.is_file() or sha256_path(path) != digest:
            raise StagingError("dependency_conpty_identity")
        if conpty_manifest.get("files", {}).get(name) != digest:
            raise StagingError("dependency_manifest_identity")

    runtime = summary.get("msvc_runtime") or {}
    runtime_manifest = read_json(root / "third_party" / "msvc_runtime" / "manifest.json")
    if runtime.get("version") != runtime_manifest.get("version"):
        raise StagingError("dependency_manifest_identity")
    for name, digest in (runtime.get("files") or {}).items():
        path = dependencies / "crt" / name
        if not path.is_file() or sha256_path(path) != digest:
            raise StagingError("dependency_crt_identity")
        if runtime_manifest.get("files", {}).get(name, {}).get("sha256") != digest:
            raise StagingError("dependency_manifest_identity")
    license_metadata = runtime_manifest.get("license") or {}
    license_name = license_metadata.get("deployed_name")
    if (not isinstance(license_name, str) or Path(license_name).name != license_name or
            runtime.get("license_sha256") != license_metadata.get("sha256") or
            runtime.get("notice_sha256") != runtime_manifest.get("notice_sha256")):
        raise StagingError("dependency_manifest_identity")
    for name, digest in (("NOTICE.txt", runtime_manifest.get("notice_sha256")),
                         (license_name, license_metadata.get("sha256"))):
        path = dependencies / "crt" / name
        if not path.is_file() or sha256_path(path) != digest:
            raise StagingError("dependency_crt_identity")
    return summary


def load_contract(root: Path) -> dict:
    contract = read_json(root / CONTRACT_PATH)
    if contract.get("schema") != "termforge-qt-product-staging-contract-v1":
        raise StagingError("staging_contract_schema")
    if contract.get("entry_executable") != ENTRY:
        raise StagingError("staging_contract_entry")
    for key in ("qt_deployed", "os_provided", "license_evidence", "claims"):
        if key not in contract:
            raise StagingError("staging_contract_schema")
    return contract


def safe_relative(root: Path, relative: str, code: str) -> Path:
    if not isinstance(relative, str):
        raise StagingError(code)
    candidate = Path(relative)
    if candidate.is_absolute() or ".." in candidate.parts or len(candidate.parts) < 2:
        raise StagingError(code)
    path = root / candidate
    if not path.is_file():
        raise StagingError(code)
    return path


def load_component_inventory(root: Path, dependencies: Path, contract: dict) -> tuple[dict, str]:
    """Load and cross-check the committed Qt third-party inventory against the SBOM."""
    evidence = contract["license_evidence"].get("qt") or {}
    inventory = read_json(safe_relative(root, evidence.get("component_inventory"),
                                        "component_inventory_contract"))
    if (inventory.get("schema") != COMPONENT_SCHEMA or
            inventory.get("review_status") != "materials_prepared_user_review_pending"):
        raise StagingError("component_inventory_schema")
    deployed = derive_deployed_files(contract)
    deployed_files = inventory.get("deployed_files")
    components = inventory.get("components")
    if not isinstance(deployed_files, dict) or sorted(deployed_files) != deployed:
        raise StagingError("component_inventory_deployed_files")
    if not isinstance(components, dict) or not components:
        raise StagingError("component_inventory_schema")

    sbom_path = dependencies / "qt" / "sbom" / "qtbase-6.10.3.spdx.json"
    if not sbom_path.is_file():
        raise StagingError("component_inventory_sbom_missing")
    try:
        sbom = json.loads(sbom_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        raise StagingError("component_inventory_sbom_missing") from None
    if inventory.get("sbom", {}).get("sha256") != sha256_path(sbom_path):
        raise StagingError("component_inventory_sbom_identity")
    try:
        recomputed = sbom_component_map(sbom, deployed)
    except Exception:  # builder raises code-carrying errors; keep staging summary path-free
        raise StagingError("component_inventory_sbom_mismatch") from None
    for name in deployed:
        declared = deployed_files[name]
        actual = recomputed[name]
        if declared.get("module_package") != actual["module"]:
            raise StagingError("component_inventory_sbom_mismatch")
        if sorted(declared.get("component_ids") or []) != sorted(actual["components"]):
            raise StagingError("component_inventory_sbom_mismatch")
        for identifier in declared.get("component_ids") or []:
            if identifier not in components:
                raise StagingError("component_inventory_sbom_mismatch")

    required = set()
    for identifier, record in components.items():
        expression = record.get("license_expression")
        identifiers = record.get("license_ids")
        if license_ids(expression) != identifiers:
            raise StagingError("component_inventory_license_ids")
        required.update(identifiers)
        if record.get("id") != identifier or not record.get("deployed_files"):
            raise StagingError("component_inventory_schema")
    module_option = (inventory.get("qt_module_license") or {}).get("selected_option")
    if not isinstance(module_option, str):
        raise StagingError("component_inventory_license_ids")
    required.add(module_option)
    texts = inventory.get("license_texts")
    if not isinstance(texts, dict) or set(texts) != required:
        raise StagingError("component_inventory_license_texts")
    for identifier, identity in texts.items():
        source = safe_relative(root, identity.get("path"), f"component_license_identity:{identifier}")
        if (source.stat().st_size != identity.get("bytes") or
                sha256_path(source) != identity.get("sha256")):
            raise StagingError(f"component_license_identity:{identifier}")

    qt_source = read_json(root / "third_party" / "qt" / "manifest.json").get("source") or {}
    if inventory.get("source", {}).get("sha256") != qt_source.get("source_archive_sha256"):
        raise StagingError("component_inventory_source_identity")
    if inventory.get("qt_version") != summary_qt_version(root):
        raise StagingError("component_inventory_schema")
    return inventory, sha256_path(root / evidence["component_inventory"])


def summary_qt_version(root: Path) -> str:
    return read_json(root / "third_party" / "qt" / "manifest.json").get("version")


def expected_files(root: Path, dependencies: Path, contract: dict, bin_dir: Path,
                   inventory: dict, inventory_sha256: str) -> dict:
    """Map staged relative path to its verified origin and evidence class."""
    expected = {}

    def add(path: str, source: Path, component: str, role: str, from_build: bool,
            pinned: str | None = None):
        if path in expected:
            raise StagingError("staging_contract_duplicate")
        expected[path] = {"source": source, "component": component, "role": role,
                          "from_build": from_build, "pinned_sha256": pinned}

    # The built entry binary has no external origin file to compare against; its
    # identity is the recorded SHA-256 plus the frozen source commit.
    add(ENTRY, bin_dir / ENTRY, "product", "runtime", True)

    # Original project license. The staged copy is generated from the source.
    license_path = root / "LICENSE"
    if not license_path.is_file():
        raise StagingError("project_license_missing")
    add("LICENSE", license_path, "termforge", "license", False)

    # Font artifacts are pinned by their own machine-readable manifest; the
    # build copies them through the font bundle target.
    font_manifest_path = root / "assets" / "fonts" / "manifest.json"
    font_manifest = read_json(font_manifest_path)
    if font_manifest.get("schema") != "termforge-font-resource-v1":
        raise StagingError("font_manifest_schema")
    fonts = font_manifest.get("files")
    if not isinstance(fonts, dict) or not fonts:
        raise StagingError("font_manifest_schema")
    for name, identity in fonts.items():
        if Path(name).name != name:
            raise StagingError("font_manifest_schema")
        source = root / "assets" / "fonts" / name
        if (not source.is_file() or source.stat().st_size != identity.get("bytes") or
                sha256_path(source) != identity.get("sha256")):
            raise StagingError("font_identity")
        add(f"assets/fonts/{name}", source, "font", "license" if name == "OFL.txt" else "runtime", True,
            identity.get("sha256"))
    readme = root / "assets" / "fonts" / "README.md"
    if not readme.is_file():
        raise StagingError("font_identity")
    add("assets/fonts/README.md", readme, "font", "documentation", True)

    qt_manifest_path = root / "third_party" / "qt" / "manifest.json"
    qt_manifest = read_json(qt_manifest_path)
    qt_libraries = contract["qt_deployed"].get("libraries") or []
    qt_plugins = contract["qt_deployed"].get("plugins") or []
    for name in qt_libraries:
        if Path(name).name != name or not name.endswith(".dll"):
            raise StagingError("staging_contract_qt")
        add(name, dependencies / "qt" / "bin" / name, "qt", "runtime", True)
    for name in qt_plugins:
        relative = Path(name)
        if len(relative.parts) < 2 or ".." in relative.parts or relative.suffix != ".dll":
            raise StagingError("staging_contract_qt")
        add(name, dependencies / "qt" / "plugins" / relative, "qt", "runtime", True)

    for identifier, identity in inventory["license_texts"].items():
        source = safe_relative(root, identity.get("path"), f"component_license_identity:{identifier}")
        add(f"{QT_LICENSE_DIR}/{identifier}.txt", source, "qt", "license", False,
            identity.get("sha256"))
    add(QT_INVENTORY_STAGED, root / contract["license_evidence"]["qt"]["component_inventory"],
        "qt", "evidence", False, inventory_sha256)

    conpty_manifest_path = root / "third_party" / "conpty" / "manifest.json"
    conpty_manifest = read_json(conpty_manifest_path)
    for name, digest in (conpty_manifest.get("files") or {}).items():
        if Path(name).name != name:
            raise StagingError("conpty_manifest")
        add(f"conpty-backend/{name}", dependencies / "conpty" / name, "conpty", "runtime", True, digest)
    conpty_license = root / "third_party" / "conpty" / "LICENSE"
    if (not conpty_license.is_file() or
            sha256_path(conpty_license) != conpty_manifest.get("bundled_license_sha256")):
        raise StagingError("conpty_identity")
    add("conpty-backend/LICENSE", conpty_license, "conpty", "license", True,
        conpty_manifest.get("bundled_license_sha256"))
    add("conpty-backend/manifest.json", conpty_manifest_path, "conpty", "evidence", True)

    runtime_manifest_path = root / "third_party" / "msvc_runtime" / "manifest.json"
    runtime_manifest = read_json(runtime_manifest_path)
    for name, identity in (runtime_manifest.get("files") or {}).items():
        if Path(name).name != name or not name.endswith(".dll"):
            raise StagingError("msvc_runtime_manifest")
        add(name, dependencies / "crt" / name, "msvc_runtime", "runtime", True, identity.get("sha256"))
    license_metadata = runtime_manifest.get("license") or {}
    license_name = license_metadata.get("deployed_name")
    if not isinstance(license_name, str) or Path(license_name).name != license_name:
        raise StagingError("msvc_runtime_manifest")
    add(license_name, dependencies / "crt" / license_name, "msvc_runtime", "license", True,
        license_metadata.get("sha256"))
    add("NOTICE.txt", dependencies / "crt" / "NOTICE.txt", "msvc_runtime", "license", True,
        runtime_manifest.get("notice_sha256"))

    add(NOTICES_PATH, root / CONTRACT_PATH, "termforge", "documentation", False)
    add(QT_NOTICES_PATH, root / CONTRACT_PATH, "qt", "documentation", False)
    return expected


def verify_build(bin_dir: Path, expected: dict, contract: dict, system_root: Path) -> dict:
    """Require the build directory to contain exactly the frozen product set."""
    actual = set(relative_files(bin_dir))
    ignored = contract.get("ignored_build_artifacts", {})
    if not isinstance(ignored, dict) or any(Path(name).name != name for name in ignored):
        raise StagingError("staging_contract_schema")
    os_provided = {}
    for entry in contract["os_provided"]:
        name = entry.get("name")
        if not isinstance(name, str) or Path(name).name != name:
            raise StagingError("staging_contract_schema")
        os_provided[name] = entry
    build_expected = {path for path, spec in expected.items() if spec["from_build"]}
    missing = sorted(build_expected - actual)
    if missing:
        raise StagingError(f"missing_product_file:{','.join(missing[:MAX_DETAIL])}")
    unexpected = sorted(actual - build_expected - set(os_provided) - set(ignored))
    if unexpected:
        fixtures = [name for name in unexpected
                    if re.search(r"(_test|_smoke|smoke|client)\.exe$", name, re.IGNORECASE) or
                    name.lower().endswith((".pdb", ".ilk", ".exp", ".lib", ".obj", ".log"))]
        if fixtures:
            raise StagingError(f"test_fixture_in_product:{','.join(fixtures[:MAX_DETAIL])}")
        raise StagingError(f"unexpected_product_file:{','.join(unexpected[:MAX_DETAIL])}")

    for path, spec in expected.items():
        if not spec["from_build"]:
            continue
        deployed = bin_dir / path
        data = deployed.read_bytes()
        source = spec["source"].read_bytes()
        if data != source:
            raise StagingError(f"{spec['component']}_identity:{path}")
        if spec["pinned_sha256"] is not None and sha256_bytes(data) != spec["pinned_sha256"]:
            raise StagingError(f"{spec['component']}_identity:{path}")

    observed_os = []
    for name, entry in sorted(os_provided.items()):
        deployed = bin_dir / name
        if not deployed.is_file():
            observed_os.append({"name": name, "present_in_build": False, **entry})
            continue
        system = system_root / "System32" / name
        if not system.is_file() or deployed.read_bytes() != system.read_bytes():
            raise StagingError(f"os_component_identity:{name}")
        observed_os.append({"name": name, "present_in_build": True,
                            "observed_sha256": sha256_path(deployed), **entry})

    # Test executables live in their own qt-*-bin directories; only the frozen
    # product files enter staging. The ConPTY host is the only other executable.
    if ENTRY not in expected:
        raise StagingError("entry_set")
    for path in sorted(expected):
        if path.lower().endswith(".exe"):
            pe_identity((bin_dir / path).read_bytes(), executable=True)
        elif path.lower().endswith(".dll"):
            pe_identity((bin_dir / path).read_bytes(), executable=False)
    return observed_os


def qt_component_notices(inventory: dict) -> bytes:
    """Generate the Qt deployed-component notice index from the committed inventory."""
    lines = [
        "Qt 6.10.3 deployed third-party components - generated evidence index",
        "",
        "This file lists the source components that the pinned Qt SDK SBOM package graph",
        "connects to the deployed Qt binaries, with their attribution and bundled license",
        "texts. It is not a legal review or a redistribution clearance. Canonical license",
        "texts are under licenses/qt/; the machine-readable inventory is",
        "licenses/qt/components.json.",
        "",
        "Deployed Qt files: " + ", ".join(sorted(inventory["deployed_files"])),
        "",
    ]
    for identifier, record in sorted(inventory["components"].items()):
        attribution = record["attribution"]
        lines.append(f"- {record['name']} {record['version']} [{identifier}]")
        lines.append(f"  license: {record['license_expression']}")
        lines.append(f"  deployed in: {', '.join(record['deployed_files'])}")
        lines.append(f"  attribution: {attribution['member']} entry {attribution['entry_index']}"
                     f" sha256 {attribution['sha256']}")
        for license_file in attribution["license_files"]:
            lines.append(f"  upstream license file: {license_file['member']} sha256 {license_file['sha256']}")
        if attribution.get("copyright"):
            for line in attribution["copyright"].splitlines():
                lines.append(f"  copyright: {line}")
        lines.append("  bundled license texts: "
                     + ", ".join(f"{QT_LICENSE_DIR}/{item}.txt" for item in record["license_ids"]))
        lines.append("")
    lines.append("Recorded as not established for the deployed binaries:")
    for record in sorted(inventory.get("excluded_attributions", []), key=lambda item: item["id"] or ""):
        lines.append(f"- {record.get('name')} [{record.get('id')}]: {record.get('category')}")
    for record in sorted(inventory.get("declared_not_deployed", []), key=lambda item: item["id"] or ""):
        lines.append(f"- {record.get('name')} [{record.get('id')}]: {record.get('category')}")
    lines.append("")
    lines.append("Corresponding-source delivery, replacement conditions and a clean-machine")
    lines.append("run remain pending user review.")
    lines.append("")
    return "\n".join(lines).encode("utf-8")


def notices_text(contract: dict, source: dict, summary: dict, observed_os: list[dict],
                 inventory: dict) -> bytes:
    qt = summary.get("qt_version", "unknown")
    conpty = summary.get("conpty_version", "unknown")
    runtime = (summary.get("msvc_runtime") or {}).get("version", "unknown")
    lines = [
        "TermForge product staging - third-party components and bundled license evidence",
        "",
        "This file is a generated evidence index for the Windows x64 C++/Qt/ConPTY",
        "engineering staging. It is not a legal review or a redistribution clearance.",
        "",
        f"TermForge original code: MIT; bundled at LICENSE (source commit {source['commit']}).",
        f"Qt {qt}: dynamic-link LGPL-3.0-only option; component inventory and notices at",
        f"  {QT_INVENTORY_STAGED} and {QT_NOTICES_PATH}; canonical Qt license texts under",
        f"  {QT_LICENSE_DIR}/. {len(inventory['components'])} deployed third-party components",
        "  are traced from the pinned SDK SBOM and source attributions.",
        f"Microsoft Windows Console ConPTY {conpty}: MIT; bundled at conpty-backend/LICENSE.",
        f"Microsoft Visual C++ v14 Runtime {runtime}: bundled with its original license",
        "  and NOTICE at LICENSE-MICROSOFT-VC-REDIST.docx and NOTICE.txt.",
        "Roboto Mono (Regular, static): SIL Open Font License 1.1; bundled at",
        "  assets/fonts/OFL.txt with its provenance notes at assets/fonts/README.md.",
        "Provided by the Windows 11 operating system, not bundled: "
        + ", ".join(entry["name"] for entry in contract["os_provided"]) + ".",
        "Deliberately not deployed because they are neither pinned nor linked by this",
        "  entry: " + ", ".join(entry["name"] for entry in contract["excluded_system_redistributables"]) + ".",
        "Redistribution clearance: pending user review (corresponding-source conditions,",
        "  replacement relinking conditions and a clean-machine run).",
        "",
    ]
    return "\n".join(lines).encode("utf-8")


def product_manifest(root: Path, contract: dict, source: dict, build: dict, summary: dict,
                     expected: dict, observed_os: list[dict], tool_sha256: str,
                     contract_sha256: str, inventory: dict, inventory_sha256: str) -> dict:
    files = {}
    for path, spec in sorted(expected.items()):
        if path in (MANIFEST_PATH, NOTICES_PATH, QT_NOTICES_PATH):
            continue
        data = spec["source"].read_bytes()
        files[path] = {"bytes": len(data), "sha256": sha256_bytes(data),
                       "component": spec["component"], "role": spec["role"]}
    notices = notices_text(contract, source, summary, observed_os, inventory)
    files[NOTICES_PATH] = {"bytes": len(notices), "sha256": sha256_bytes(notices),
                           "component": "termforge", "role": "documentation"}
    qt_notices = qt_component_notices(inventory)
    files[QT_NOTICES_PATH] = {"bytes": len(qt_notices), "sha256": sha256_bytes(qt_notices),
                              "component": "qt", "role": "documentation"}

    def manifest_sha(relative: str) -> str:
        return sha256_path(root / relative)

    runtime = summary.get("msvc_runtime") or {}
    manifest = {
        "schema": SCHEMA,
        "status": "engineering_candidate",
        "generated_by": {"tool": "tools/stage_qt_product.py", "tool_sha256": tool_sha256},
        "contract": {"path": CONTRACT_PATH, "sha256": contract_sha256},
        "identity": {
            "product": "TermForge C++/Qt local terminal entry",
            "entry_executable": ENTRY,
            "platform": contract["platform"],
            "configuration": build["configuration"],
            "distribution_state": "engineering_candidate_not_released",
            "r2_acceptance": False,
        },
        "source": source,
        "build": build,
        "dependencies": {
            "preparation_schema": PREPARATION_SCHEMA,
            "preparation_script_sha256": summary.get("preparation_script_sha256"),
            "input_manifests": summary.get("input_manifests"),
            "qt": {
                "version": summary.get("qt_version"),
                "architecture": summary.get("qt_architecture"),
                "archive_sha256": summary.get("qt_archive_sha256"),
                "prepared_sdk": summary.get("sdk"),
                "license_manifest": "third_party/qt/manifest.json",
                "license_manifest_sha256": manifest_sha("third_party/qt/manifest.json"),
                "component_inventory": {
                    "path": "third_party/qt/components.json",
                    "sha256": inventory_sha256,
                    "sbom_sha256": inventory.get("sbom", {}).get("sha256"),
                    "deployed_files": len(inventory["deployed_files"]),
                    "third_party_components": len(inventory["components"]),
                    "excluded_attributions": len(inventory.get("excluded_attributions", [])),
                    "declared_not_deployed": len(inventory.get("declared_not_deployed", [])),
                    "review_status": inventory.get("review_status"),
                    "staged_copy": QT_INVENTORY_STAGED,
                    "notices": QT_NOTICES_PATH,
                },
            },
            "conpty": {
                "version": summary.get("conpty_version"),
                "package_sha256": summary.get("conpty_package_sha256"),
                "files": summary.get("conpty_files"),
                "license_manifest": "third_party/conpty/manifest.json",
                "license_manifest_sha256": manifest_sha("third_party/conpty/manifest.json"),
            },
            "msvc_runtime": {
                "version": runtime.get("version"),
                "architecture": runtime.get("architecture"),
                "package_sha256": runtime.get("package_sha256"),
                "files": runtime.get("files"),
                "license_sha256": runtime.get("license_sha256"),
                "notice_sha256": runtime.get("notice_sha256"),
                "license_manifest": "third_party/msvc_runtime/manifest.json",
                "license_manifest_sha256": manifest_sha("third_party/msvc_runtime/manifest.json"),
            },
            "font": {
                "manifest": "assets/fonts/manifest.json",
                "manifest_sha256": manifest_sha("assets/fonts/manifest.json"),
                "files": {name: {"sha256": spec["pinned_sha256"]}
                          for name, spec in sorted(expected.items())
                          if spec["component"] == "font" and spec["pinned_sha256"]},
            },
        },
        "os_provided": observed_os,
        "excluded_system_redistributables": contract["excluded_system_redistributables"],
        "licenses": {
            "termforge": {"spdx": "MIT", "bundled_paths": ["LICENSE"],
                          "sha256": files["LICENSE"]["sha256"],
                          "scope": "original TermForge code and documentation"},
            "qt": {
                "selected_option": "LGPL-3.0-only (dynamic link)",
                "bundled_paths": sorted(f"{QT_LICENSE_DIR}/{identifier}.txt"
                                        for identifier in inventory["license_texts"]),
                "component_inventory": QT_INVENTORY_STAGED,
                "third_party_notices": QT_NOTICES_PATH,
                "evidence_manifest": "third_party/qt/components.json",
                "sbom_sha256": inventory.get("sbom", {}).get("sha256"),
                "deployed_files": len(inventory["deployed_files"]),
                "third_party_components": len(inventory["components"]),
                "status": inventory.get("review_status"),
            },
            "conpty": {"spdx": "MIT", "bundled_paths": ["conpty-backend/LICENSE"],
                       "evidence_manifest": "conpty-backend/manifest.json", "status": "bundled"},
            "msvc_runtime": {"bundled_paths": ["LICENSE-MICROSOFT-VC-REDIST.docx", "NOTICE.txt"],
                             "evidence_manifest": "third_party/msvc_runtime/manifest.json",
                             "status": "bundled"},
            "font": {"spdx": "OFL-1.1", "bundled_paths": ["assets/fonts/OFL.txt"],
                     "evidence_manifest": "assets/fonts/manifest.json", "status": "bundled"},
        },
        "redistribution_clearance": {
            "status": "pending_user_review",
            "reason": "The Qt deployed-component inventory and canonical license texts are prepared, but corresponding-source/replacement conditions, a clean-machine run and user review are not closed. This staging is an engineering candidate, not a redistribution clearance.",
        },
        "claims": contract["claims"],
        "files": dict(sorted(files.items())),
        "test_fixtures": {
            "included": [],
            "policy": contract["test_fixture_policy"],
            "separate_directories": "Qt test executables are built into their own qt-*-bin directories and are rejected by staging.",
        },
        "limitations": [
            "No clean Windows machine extraction/run has been performed for this staging.",
            "Single-screen 96/144 DPI, IME, resize/history and reliability acceptance are outside this build-preparation slice.",
            "Qt's deployed-component provenance and license texts are recorded, but corresponding-source/replacement conditions and clean-machine runtime completeness remain open.",
            "The pinned SBOM's per-file SHA-1 values do not match the archived SDK binaries; byte identity is bound by the pinned archive/SDK tree identity instead.",
            "icuuc.dll is provided by the Windows 11 system and is not bundled; this staging is not validated on other Windows versions.",
            "No tag, release or published binary is created by this tool.",
        ],
    }
    return manifest


def verify_staging(staging: Path) -> dict:
    manifest_path = staging / MANIFEST_PATH
    manifest = read_json(manifest_path)
    if manifest.get("schema") != SCHEMA or manifest.get("status") != "engineering_candidate":
        raise StagingError("manifest_schema")
    files = manifest.get("files")
    if not isinstance(files, dict) or not files:
        raise StagingError("manifest_files")
    actual = set(relative_files(staging))
    declared = set(files) | {MANIFEST_PATH}
    if actual != declared:
        missing = sorted(declared - actual)
        extra = sorted(actual - declared)
        if missing:
            raise StagingError(f"missing_product_file:{','.join(missing[:MAX_DETAIL])}")
        raise StagingError(f"unexpected_product_file:{','.join(extra[:MAX_DETAIL])}")
    for path, identity in files.items():
        data = (staging / path).read_bytes()
        if len(data) != identity.get("bytes") or sha256_bytes(data) != identity.get("sha256"):
            raise StagingError(f"file_identity:{path}")
    entry = manifest.get("identity", {}).get("entry_executable")
    if entry not in files:
        raise StagingError("entry_set")
    pe_identity((staging / entry).read_bytes(), executable=True)
    for path in sorted(files):
        if path.lower().endswith(".dll"):
            pe_identity((staging / path).read_bytes(), executable=False)
    for license_entry in manifest.get("licenses", {}).values():
        for path in license_entry.get("bundled_paths", []):
            if path not in files:
                raise StagingError("license_evidence_missing")
    return {"schema": OPERATION_SCHEMA, "status": "passed", "operation": "verify",
            "manifest_sha256": sha256_path(manifest_path), "staging": tree_identity(staging),
            "claims": manifest.get("claims")}


def write_json(path: Path, value: dict) -> None:
    report = (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(prefix=".staging-", dir=path.parent, delete=False) as handle:
        candidate = Path(handle.name)
        handle.write(report)
    try:
        candidate.replace(path)
    finally:
        candidate.unlink(missing_ok=True)


def stage(source_root: Path, bin_dir: Path, dependencies: Path, dependency_summary: Path,
          out: Path, summary_path: Path, allow_dirty: bool, system_root: Path | None = None) -> dict:
    if out.exists():
        raise StagingError("output_exists")
    if not bin_dir.is_dir():
        raise StagingError("build_directory")
    contract = load_contract(source_root)
    summary = verify_dependency_summary(source_root, dependencies, dependency_summary)
    inventory, inventory_sha256 = load_component_inventory(source_root, dependencies, contract)
    source = source_identity(source_root, allow_dirty)
    build = build_configuration(bin_dir)
    expected = expected_files(source_root, dependencies, contract, bin_dir,
                              inventory, inventory_sha256)

    system = Path(system_root or os.environ.get("SystemRoot", r"C:\Windows"))
    observed_os = verify_build(bin_dir, expected, contract, system)

    out.parent.mkdir(parents=True, exist_ok=True)
    notices = notices_text(contract, source, summary, observed_os, inventory)
    qt_notices = qt_component_notices(inventory)
    with tempfile.TemporaryDirectory(prefix="qt-product-", dir=out.parent) as temporary:
        staging = Path(temporary) / "product"
        staging.mkdir()
        for path, spec in sorted(expected.items()):
            if path == NOTICES_PATH:
                data = notices
            elif path == QT_NOTICES_PATH:
                data = qt_notices
            else:
                data = spec["source"].read_bytes()
                if spec["pinned_sha256"] is not None and sha256_bytes(data) != spec["pinned_sha256"]:
                    raise StagingError(f"{spec['component']}_identity:{path}")
            target = staging / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        manifest = product_manifest(source_root, contract, source, build, summary, expected,
                                    observed_os, sha256_path(Path(__file__).resolve()),
                                    sha256_path(source_root / CONTRACT_PATH),
                                    inventory, inventory_sha256)
        manifest_bytes = (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
        (staging / MANIFEST_PATH).write_bytes(manifest_bytes)
        identity = tree_identity(staging)
        result = {"schema": OPERATION_SCHEMA, "status": "passed", "operation": "stage",
                  "manifest_sha256": sha256_bytes(manifest_bytes), "staging": identity,
                  "source": {key: source[key] for key in ("commit", "tree", "dirty", "development_override")},
                  "product": {"entry_executable": ENTRY, "files": len(manifest["files"]),
                              "distribution_state": "engineering_candidate_not_released"},
                  "qt_components": len(inventory["components"]),
                  "qt_license_texts": len(inventory["license_texts"]),
                  "os_provided": [entry["name"] for entry in observed_os],
                  "claims": contract["claims"],
                  "scope": "Engineering product staging only; no release, clean-machine acceptance or R2 acceptance."}
        staging.replace(out)
    try:
        write_json(summary_path, result)
    except OSError:
        result = {"schema": OPERATION_SCHEMA, "status": "failed", "operation": "stage",
                  "error": "summary_write_failed", "staging_published": True}
    return result


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    stage_command = commands.add_parser("stage", help="Create a frozen engineering staging directory")
    stage_command.add_argument("--bin-dir", type=Path, required=True,
                               help="Built Release product directory, normally build/qt-cpp/qt-bin/Release")
    stage_command.add_argument("--dependencies", type=Path, required=True,
                               help="Verified directory produced by tools.prepare_qt_build")
    stage_command.add_argument("--dependency-summary", type=Path, required=True,
                               help="Path-free preparation summary from tools.prepare_qt_build")
    stage_command.add_argument("--out", type=Path, required=True, help="New staging directory")
    stage_command.add_argument("--summary", type=Path, required=True,
                               help="Path-free operation summary to write")
    stage_command.add_argument("--source-root", type=Path, default=ROOT)
    stage_command.add_argument("--allow-dirty-development", action="store_true",
                               help="Allow a dirty source tree and record the development override")
    verify_command = commands.add_parser("verify", help="Re-verify an existing staging directory")
    verify_command.add_argument("--staging", type=Path, required=True)
    arguments = parser.parse_args(argv)
    if arguments.command == "verify":
        try:
            result = verify_staging(arguments.staging.resolve())
        except StagingError as error:
            result = {"schema": OPERATION_SCHEMA, "status": "failed", "operation": "verify",
                      "error": str(error)}
        except (OSError, ValueError, KeyError, TypeError):
            result = {"schema": OPERATION_SCHEMA, "status": "failed", "operation": "verify",
                      "error": "staging_input_failed"}
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0 if result.get("status") == "passed" else 1
    try:
        result = stage(arguments.source_root.resolve(), arguments.bin_dir.resolve(),
                       arguments.dependencies.resolve(), arguments.dependency_summary.resolve(),
                       arguments.out.resolve(), arguments.summary.resolve(),
                       arguments.allow_dirty_development)
        if result.get("status") == "passed":
            print(json.dumps(result, ensure_ascii=False, indent=2))
            return 0
    except StagingError as error:
        result = {"schema": OPERATION_SCHEMA, "status": "failed", "operation": "stage",
                  "error": str(error)}
    except (OSError, ValueError, KeyError, TypeError):
        result = {"schema": OPERATION_SCHEMA, "status": "failed", "operation": "stage",
                  "error": "staging_input_failed"}
    try:
        write_json(arguments.summary.resolve(), result)
    except (OSError, ValueError):
        result = {"schema": OPERATION_SCHEMA, "status": "failed", "operation": "stage",
                  "error": "summary_write_failed", "staging_error": result.get("error")}
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result.get("status") == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
