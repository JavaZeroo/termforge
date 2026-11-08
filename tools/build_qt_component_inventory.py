"""Derive the deployed Qt third-party component and license inventory.

The product ships a fixed set of Qt binaries from the manifest-pinned qtbase
6.10.3 archive. This tool reads that archive's own ``qt_attribution.json``
files and the SPDX SBOM shipped in the prepared SDK, traces each deployed
library/plugin to its module package and the third-party packages that module
declares, and writes a machine-readable inventory plus the canonical license
texts needed for the shipped components.

Evidence levels are kept explicit: a component is listed as deployed only when
the SBOM package graph connects it to the deployed module/plugin package; the
source archive attribution is then cross-checked for identity and license.
Source-tree-only declarations, build-tool attributions and packages owned by
modules that are not deployed are recorded as excluded instead of being
silently treated as shipped.

The tool never authenticates, installs, executes, publishes or accepts the
product. It records facts and the remaining gaps; it does not produce a legal
redistribution conclusion.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import posixpath
import re
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "termforge-qt-deployed-components-v1"
OPERATION_SCHEMA = "termforge-qt-component-operation-v1"
INVENTORY_PATH = "third_party/qt/components.json"
LICENSES_PATH = "third_party/qt/LICENSES"
CONTRACT_PATH = "packaging/qt-product/manifest.json"
QT_MANIFEST_PATH = "third_party/qt/manifest.json"
SOURCE_PREFIX = "qtbase-everywhere-src-6.10.3/"
QT_MODULE_LICENSE = "LGPL-3.0-only"

if __package__:
    from tools.prepare_qt_build import PreparationError, acquire
else:  # pragma: no cover - direct script execution is diagnostic only
    from prepare_qt_build import PreparationError, acquire


class InventoryError(Exception):
    """A fixed public code; never a raw process, network or filesystem message."""


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        raise InventoryError("input_unreadable") from None
    if not isinstance(value, dict):
        raise InventoryError("input_schema")
    return value


def read_json_bytes(data: bytes, *, tolerant: bool = False) -> dict | list:
    try:
        return json.loads(data.decode("utf-8"), strict=not tolerant)
    except (UnicodeError, json.JSONDecodeError):
        raise InventoryError("attribution_json") from None


def license_ids(expression: str | None) -> list[str]:
    """SPDX expression license ids, ignoring operators and WITH exceptions."""
    tokens = re.findall(r"[A-Za-z0-9.:_+\-]+", expression or "")
    identifiers = []
    skip_exception = False
    for token in tokens:
        if token in ("OR", "AND"):
            skip_exception = False
            continue
        if token == "WITH":
            skip_exception = True
            continue
        if skip_exception:
            skip_exception = False
            continue
        if token not in identifiers:
            identifiers.append(token)
    return sorted(identifiers)


def attribution_reference(package: dict) -> tuple[str, int] | None:
    """Read the attribution file member and entry index from an SBOM package comment."""
    comment = package.get("comment") or ""
    match = re.search(r"/src_dir/qtbase/(.+?qt_attribution\.json)", comment)
    if not match:
        return None
    index = re.search(r"Entry index:\s*(\d+)", comment)
    return match.group(1), int(index.group(1)) if index else 0


def attribution_id(package: dict) -> str | None:
    match = re.search(r"^\s*Id:\s*(\S+)\s*$", package.get("comment") or "", re.MULTILINE)
    return match.group(1) if match else None


def is_third_party(package: dict) -> bool:
    name = package.get("name", "")
    if "_Attribution_" in name or name.startswith("Bundled"):
        return True
    location = package.get("downloadLocation") or ""
    if not location or location == "NOASSERTION":
        return False
    if location.startswith("git://code.qt.io/qt/") or "qt.io" in location:
        return False
    return True


def sbom_indexes(sbom: dict):
    packages = {entry["SPDXID"]: entry for entry in sbom.get("packages", []) if "SPDXID" in entry}
    files = {entry["SPDXID"]: entry for entry in sbom.get("files", []) if "SPDXID" in entry}
    contains = {}
    contained_by = {}
    depends = {}
    for relationship in sbom.get("relationships", []):
        kind = relationship.get("relationshipType")
        source = relationship.get("spdxElementId")
        target = relationship.get("relatedSpdxElement")
        if kind == "CONTAINS":
            contains.setdefault(source, []).append(target)
            contained_by.setdefault(target, []).append(source)
        elif kind == "DEPENDS_ON":
            depends.setdefault(source, []).append(target)
    return packages, files, contains, contained_by, depends


def sbom_file_for_deployed_name(sbom_files: dict, deployed_name: str) -> dict:
    """Map a staged relative Qt file to its non-debug SBOM packaged-file entry."""
    candidates = [entry for entry in sbom_files.values()
                  if entry.get("fileName", "").endswith("/" + deployed_name)
                  and "Debug" not in entry.get("SPDXID", "")]
    if len(candidates) != 1:
        raise InventoryError(f"sbom_file:{deployed_name}")
    return candidates[0]


def component_packages(depends: dict, packages: dict, module_id: str) -> dict[str, dict]:
    """Third-party packages embedded in a module, following third-party edges only."""
    found: dict[str, dict] = {}

    def walk(package_id: str, depth: int):
        for child in depends.get(package_id, []):
            package = packages.get(child)
            if package is None or not is_third_party(package):
                continue
            if child not in found or found[child]["depth"] > depth:
                found[child] = {"package": package, "depth": depth}
                walk(child, depth + 1)

    walk(module_id, 1)
    return found


def sbom_component_map(sbom: dict, deployed_names: list[str]) -> dict[str, dict]:
    """Per deployed file, the SBOM-proven third-party package set.

    Returns ``{deployed_file: {component_id: {...}}}`` where ``component_id``
    is the attribution id parsed from the SBOM package comment. The map is
    derived purely from the SBOM, so staging and the inventory builder can
    independently recompute and compare it without the source archive.
    """
    packages, files, _, contained_by, depends = sbom_indexes(sbom)
    result = {}
    for name in deployed_names:
        file_entry = sbom_file_for_deployed_name(files, name)
        owners = sorted({owner for owner in contained_by.get(file_entry["SPDXID"], [])
                         if owner in packages})
        if len(owners) != 1:
            raise InventoryError(f"sbom_owner:{name}")
        embedded = component_packages(depends, packages, owners[0])
        components = {}
        for package_id, detail in embedded.items():
            package = detail["package"]
            identifier = attribution_id(package)
            if identifier is None:
                raise InventoryError(f"sbom_attribution_id:{package.get('name')}")
            previous = components.get(identifier)
            if previous is not None:
                if (license_ids(previous["license_expression"]) !=
                        license_ids(package.get("licenseConcluded"))):
                    raise InventoryError(f"sbom_license_conflict:{identifier}")
                continue
            components[identifier] = {
                "license_expression": package.get("licenseConcluded"),
                "depth": detail["depth"],
                "package": package,
            }
        result[name] = {"module": packages[owners[0]].get("name"), "components": components}
    return result


def load_source(archive_path: Path) -> tuple[dict[str, bytes], dict[tuple[str, int], dict], set[str]]:
    """Read attribution files, license texts and member names from the pinned archive."""
    attributions: dict[tuple[str, int], dict] = {}
    license_texts: dict[str, bytes] = {}
    try:
        with tarfile.open(archive_path, "r:xz") as archive:
            members = {member.name: member for member in archive.getmembers() if member.isfile()}
            archive_names = set(members)
            for name in sorted(archive_names):
                if not name.endswith("qt_attribution.json"):
                    continue
                raw = archive.extractfile(members[name]).read()
                data = read_json_bytes(raw, tolerant=True)
                entries = data if isinstance(data, list) else [data]
                relative = name[len(SOURCE_PREFIX):] if name.startswith(SOURCE_PREFIX) else name
                for index, entry in enumerate(entries):
                    if not isinstance(entry, dict):
                        raise InventoryError("attribution_json")
                    attributions[(relative, index)] = {
                        "data": entry,
                        "bytes": len(raw),
                        "sha256": sha256_bytes(raw),
                    }
            for name in sorted(archive_names):
                match = re.fullmatch(re.escape(SOURCE_PREFIX) + r"LICENSES/([^/]+)\.txt", name)
                if match:
                    license_texts[match.group(1)] = archive.extractfile(members[name]).read()
    except (OSError, tarfile.TarError, EOFError):
        raise InventoryError("source_archive_unreadable") from None
    return license_texts, attributions, archive_names


def read_members(archive_path: Path, wanted: set[str]) -> dict[str, bytes]:
    """Read selected archive members in a single pass."""
    result = {}
    try:
        with tarfile.open(archive_path, "r:xz") as archive:
            for member in archive:
                if member.isfile() and member.name in wanted:
                    result[member.name] = archive.extractfile(member).read()
                    if len(result) == len(wanted):
                        break
    except (OSError, tarfile.TarError, EOFError):
        raise InventoryError("source_archive_unreadable") from None
    if set(result) != wanted:
        raise InventoryError("source_member:mixed")
    return result


def normalize_copyright(value) -> str:
    if isinstance(value, list):
        return "\n".join(str(item) for item in value)
    return "" if value is None else str(value)


def license_file_members(entry: dict, attribution_relative: str) -> list[str]:
    """Resolve declared license/copyright files to full source-archive members."""
    directory = posixpath.dirname(SOURCE_PREFIX + attribution_relative)
    declared = entry.get("LicenseFile")
    if isinstance(declared, str):
        declared = [declared]
    declared = list(declared or []) + list(entry.get("LicenseFiles") or [])
    copyright_file = entry.get("CopyrightFile")
    if isinstance(copyright_file, str):
        declared.append(copyright_file)
    resolved = []
    for relative in declared:
        if not isinstance(relative, str) or not relative or relative.startswith("/"):
            raise InventoryError("attribution_license_file")
        member = posixpath.normpath(posixpath.join(directory, relative))
        if member not in resolved:
            resolved.append(member)
    return resolved


def derive_deployed_files(contract: dict) -> list[str]:
    deployed = contract.get("qt_deployed") or {}
    libraries = deployed.get("libraries") or []
    plugins = deployed.get("plugins") or []
    files = []
    for name in libraries:
        if Path(name).name != name or not name.endswith(".dll"):
            raise InventoryError("contract_deployed_files")
        files.append(name)
    for name in plugins:
        relative = Path(name)
        if len(relative.parts) < 2 or ".." in relative.parts or relative.suffix != ".dll":
            raise InventoryError("contract_deployed_files")
        files.append(name)
    return sorted(files)


def derive_inventory(source_archive: Path, sbom: dict, sbom_sha256: str, sdk_dir: Path,
                     contract: dict, qt_manifest: dict) -> tuple[dict, dict[str, bytes]]:
    deployed_names = derive_deployed_files(contract)
    sbom_map = sbom_component_map(sbom, deployed_names)
    license_texts, attributions, archive_names = load_source(source_archive)

    packages, files, _, contained_by, depends = sbom_indexes(sbom)
    file_sha1_checks = {"algorithm": "SHA1", "compared_files": 0, "matching_files": 0,
                        "mismatched_files": []}
    for name in deployed_names:
        file_entry = sbom_file_for_deployed_name(files, name)
        expected = next((checksum["checksumValue"] for checksum in file_entry.get("checksums", [])
                         if checksum.get("algorithm") == "SHA1"), None)
        local = (sdk_dir / "bin" / name) if "/" not in name else (sdk_dir / "plugins" / name)
        if not local.is_file():
            raise InventoryError(f"sdk_file:{name}")
        actual = hashlib.sha1(local.read_bytes()).hexdigest()
        file_sha1_checks["compared_files"] += 1
        if expected == actual:
            file_sha1_checks["matching_files"] += 1
        else:
            file_sha1_checks["mismatched_files"].append(name)

    components: dict[str, dict] = {}
    expressions: dict[str, str] = {}
    license_members_by_component: dict[str, list[str]] = {}
    deployed_references: set[tuple[str, int]] = set()
    for name in deployed_names:
        for identifier, detail in sbom_map[name]["components"].items():
            package = detail["package"]
            reference = attribution_reference(package)
            if reference is None:
                raise InventoryError(f"attribution_reference:{identifier}")
            attribution = attributions.get(reference)
            if attribution is None:
                raise InventoryError(f"attribution_missing:{identifier}")
            deployed_references.add(reference)
            entry = attribution["data"]
            if entry.get("Id") != identifier:
                raise InventoryError(f"attribution_id:{identifier}")
            expression = package.get("licenseConcluded") or ""
            if license_ids(expression) != license_ids(entry.get("LicenseId")):
                raise InventoryError(f"license_id:{identifier}")
            if identifier in expressions and expressions[identifier] != expression:
                raise InventoryError(f"license_expression_conflict:{identifier}")
            expressions[identifier] = expression
            attribution_version = entry.get("Version")
            sbom_version = package.get("versionInfo")
            if (attribution_version and sbom_version not in (None, "unknown")
                    and str(attribution_version) != str(sbom_version)):
                raise InventoryError(f"version:{identifier}")
            license_members = license_file_members(entry, reference[0])
            for member in license_members:
                if member not in archive_names:
                    raise InventoryError(f"attribution_license_missing:{identifier}")
            record = components.setdefault(identifier, {
                "id": identifier,
                "name": entry.get("Name", identifier),
                "version": str(attribution_version or sbom_version or "unknown"),
                "license_expression": expression,
                "license_ids": license_ids(expression),
                "deployed_files": set(),
                "evidence": {"level": "sbom_module_dependency", "depth": detail["depth"],
                             "sbom_packages": []},
                "attribution": {
                    "member": SOURCE_PREFIX + reference[0],
                    "entry_index": reference[1],
                    "bytes": attribution["bytes"],
                    "sha256": attribution["sha256"],
                    "qt_usage": entry.get("QtUsage"),
                    "qt_parts": entry.get("QtParts"),
                    "license_files": [],
                    "copyright": normalize_copyright(entry.get("Copyright")),
                },
                "source": {
                    "homepage": entry.get("Homepage"),
                    "download_location": entry.get("DownloadLocation") or package.get("downloadLocation"),
                    "purl": entry.get("PURL"),
                },
            })
            record["deployed_files"].add(name)
            record["evidence"]["depth"] = min(record["evidence"]["depth"], detail["depth"])
            sbom_name = package.get("name")
            if sbom_name not in record["evidence"]["sbom_packages"]:
                record["evidence"]["sbom_packages"].append(sbom_name)
            license_members_by_component[identifier] = license_members

    needed_members = {member for members in license_members_by_component.values() for member in members}
    member_bytes = read_members(source_archive, needed_members) if needed_members else {}
    for identifier, members in license_members_by_component.items():
        components[identifier]["attribution"]["license_files"] = [
            {"member": member, "bytes": len(member_bytes[member]),
             "sha256": sha256_bytes(member_bytes[member])}
            for member in members
        ]

    for record in components.values():
        if license_ids(record["license_expression"]) != record["license_ids"]:
            raise InventoryError("license_expression")
        record["deployed_files"] = sorted(record["deployed_files"])
        record["evidence"]["sbom_packages"] = sorted(record["evidence"]["sbom_packages"])

    required_ids = sorted({identifier for record in components.values()
                           for identifier in record["license_ids"]} | {QT_MODULE_LICENSE})
    missing_texts = [identifier for identifier in required_ids if identifier not in license_texts]
    if missing_texts:
        raise InventoryError("license_text_missing:" + ",".join(missing_texts))

    # Source-tree declarations that are not established as shipped. Two kinds
    # are kept separate: attributions reachable only through internal build
    # packages, and declarations owned by modules outside the deployed set.
    sbom_attribution = {}
    for package_id, package in packages.items():
        reference = attribution_reference(package)
        if reference is not None:
            sbom_attribution[reference] = package_id
    reachable = set()
    for name in deployed_names:
        file_entry = sbom_file_for_deployed_name(files, name)
        owners = sorted({owner for owner in contained_by.get(file_entry["SPDXID"], [])
                         if owner in packages})
        pending = [owners[0]]
        while pending:
            current = pending.pop()
            for child in depends.get(current, []):
                if child not in reachable:
                    reachable.add(child)
                    pending.append(child)

    excluded = []
    declared_not_deployed = []
    for (member, index), attribution in sorted(attributions.items()):
        if (member, index) in deployed_references:
            continue
        entry = attribution["data"]
        package_id = sbom_attribution.get((member, index))
        record = {"id": entry.get("Id"), "name": entry.get("Name"),
                  "attribution": {"member": SOURCE_PREFIX + member, "entry_index": index,
                                  "sha256": attribution["sha256"]}}
        if package_id is None:
            record["category"] = "source_declaration_not_in_sbom"
            record["reason"] = ("The pinned source archive declares this attribution, but the shipped "
                                "SBOM has no package for it; it is not established as compiled into a deployed binary.")
            declared_not_deployed.append(record)
        elif package_id in reachable:
            record["category"] = "internal_build_platform_attribution"
            record["qt_usage"] = entry.get("QtUsage")
            record["qt_parts"] = entry.get("QtParts")
            record["sbom_package"] = packages[package_id].get("name")
            record["reason"] = ("Reachable from a deployed module only through internal Qt build/platform "
                                "packages; the attribution declares build-system/tool use, not a runtime "
                                "component of the packaged binary.")
            excluded.append(record)
        else:
            owning = packages[package_id].get("name")
            record["category"] = "owning_package_not_deployed"
            record["sbom_package"] = owning
            record["reason"] = ("The owning SBOM package is not part of the deployed product files "
                                "(no dependency path from a deployed Qt module/plugin).")
            declared_not_deployed.append(record)

    qt_manifest_source = qt_manifest.get("source") or {}
    inventory = {
        "schema": SCHEMA,
        "scope": ("Qt 6.10.3 third-party component and license evidence for the fixed Windows x64 "
                  "product staging. Materials for review; not a legal redistribution conclusion."),
        "review_status": "materials_prepared_user_review_pending",
        "qt_version": qt_manifest.get("version"),
        "source": {
            "url": qt_manifest_source.get("source_archive_url"),
            "bytes": qt_manifest_source.get("source_archive_bytes"),
            "sha256": qt_manifest_source.get("source_archive_sha256"),
        },
        "sbom": {
            "path": "sbom/qtbase-6.10.3.spdx.json",
            "sha256": sbom_sha256,
            "spdx_version": sbom.get("spdxVersion"),
            "created": (sbom.get("creationInfo") or {}).get("created"),
            "binary_file_checksums": {
                "algorithm": file_sha1_checks["algorithm"],
                "checked_files": file_sha1_checks["compared_files"],
                "matching_files": file_sha1_checks["matching_files"],
                "mismatched_files": file_sha1_checks["mismatched_files"],
                "note": ("The SBOM ships in the pinned binary archive, but its packaged-file SHA-1 values "
                         "do not match the archived SDK binaries. Per-file byte identity therefore comes "
                         "from the pinned archive/SDK tree identity, not from SBOM file checksums."),
            },
        },
        "method": {
            "deployed_file_mapping": ("Each staged Qt library/plugin maps to the single non-Debug SBOM "
                                      "packaged file; its owning SBOM package supplies the module/plugin class."),
            "component_mapping": ("Third-party packages reached from the owning package through DEPENDS_ON "
                                  "edges are included; traversal follows only third-party packages and stops at "
                                  "internal Qt packages. Existence in the source tree alone is not treated as shipped."),
            "attribution_cross_check": ("Each component's SBOM package comment is resolved to a "
                                        "qt_attribution.json member and entry index in the pinned source "
                                        "archive; Id, license ids, version and license files are verified."),
            "license_texts": ("Canonical LICENSES/<id>.txt files come from the same pinned source archive; "
                              "component-specific upstream license files are recorded by member and SHA-256."),
        },
        "deployed_files": {
            name: {
                "module_package": sbom_map[name]["module"],
                "component_ids": sorted(sbom_map[name]["components"]),
            }
            for name in deployed_names
        },
        "components": {identifier: record for identifier, record in sorted(components.items())},
        "excluded_attributions": excluded,
        "declared_not_deployed": declared_not_deployed,
        "license_texts": {
            identifier: {
                "path": f"{LICENSES_PATH}/{identifier}.txt",
                "source_archive_member": SOURCE_PREFIX + f"LICENSES/{identifier}.txt",
                "bytes": len(license_texts[identifier]),
                "sha256": sha256_bytes(license_texts[identifier]),
            }
            for identifier in required_ids
        },
        "qt_module_license": {
            "expression": "LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only",
            "selected_option": QT_MODULE_LICENSE,
            "bundled_text": f"{LICENSES_PATH}/{QT_MODULE_LICENSE}.txt",
            "alternatives_not_bundled": ["LicenseRef-Qt-Commercial", "GPL-2.0-only", "GPL-3.0-only"],
            "note": ("The product takes the dynamic-link LGPL-3.0-only option. Other alternative options "
                     "are not bundled; corresponding-source and replacement conditions remain part of "
                     "the pending review."),
        },
        "unresolved": [
            ("SBOM packaged-file SHA-1 values do not match the pinned SDK binaries; SBOM is evidence for "
             "package relationships, while deployed-file byte identity is bound by the pinned archive and "
             "prepared SDK tree digest."),
            ("Two build-system attributions (extra-cmake-modules, KWin CMake modules) are excluded as "
             "tool-only declarations; they are listed under excluded_attributions."),
            ("Corresponding-source delivery, relinking/replacement conditions and a clean-machine run are "
             "not evaluated by this inventory and remain pending user review."),
        ],
    }
    return inventory, {identifier: license_texts[identifier] for identifier in required_ids}


def resolve_archive(arguments, qt_manifest: dict) -> Path:
    source = qt_manifest.get("source") or {}
    name = Path(source.get("source_archive_url", "")).name
    size = source.get("source_archive_bytes")
    digest = source.get("source_archive_sha256")
    if not name or not isinstance(size, int) or not isinstance(digest, str):
        raise InventoryError("source_pin")
    if arguments.source_archive is not None:
        path = arguments.source_archive.resolve()
        if not path.is_file() or path.stat().st_size != size or sha256_path(path) != digest:
            raise InventoryError("source_archive_identity")
        return path
    cache = arguments.cache.resolve()
    try:
        return acquire(cache, name, source["source_archive_url"], size, digest, arguments.offline).resolve()
    except PreparationError as error:
        raise InventoryError(str(error)) from None


def sha256_path(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def json_bytes(value: dict) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def write_outputs(inventory: dict, texts: dict[str, bytes], inventory_path: Path, licenses_dir: Path) -> None:
    inventory_path.parent.mkdir(parents=True, exist_ok=True)
    licenses_dir.mkdir(parents=True, exist_ok=True)
    existing = {path.stem for path in licenses_dir.glob("*.txt")}
    stale = sorted(existing - set(texts))
    if stale:
        raise InventoryError("license_text_stale:" + ",".join(stale[:5]))
    with tempfile.NamedTemporaryFile(prefix=".components-", dir=inventory_path.parent, delete=False) as handle:
        candidate = Path(handle.name)
        handle.write(json_bytes(inventory))
    try:
        candidate.replace(inventory_path)
    finally:
        candidate.unlink(missing_ok=True)
    for identifier, data in texts.items():
        target = licenses_dir / f"{identifier}.txt"
        target.write_bytes(data)


def compare(existing_path: Path, derived: dict, texts: dict[str, bytes], licenses_dir: Path) -> dict:
    existing = read_json(existing_path)
    if existing != derived:
        missing = sorted(set(derived) - set(existing))
        extra = sorted(set(existing) - set(derived))
        detail = []
        if missing:
            detail.append("missing_sections:" + ",".join(missing[:5]))
        if extra:
            detail.append("extra_sections:" + ",".join(extra[:5]))
        if not detail:
            for key in sorted(set(existing) & set(derived)):
                if existing[key] != derived[key]:
                    detail.append("section:" + key)
                    break
        raise InventoryError("inventory_mismatch:" + ";".join(detail))
    for identifier, data in texts.items():
        target = licenses_dir / f"{identifier}.txt"
        if not target.is_file() or target.read_bytes() != data:
            raise InventoryError(f"license_text_mismatch:{identifier}")
    return existing


def run(arguments, write: bool) -> dict:
    qt_manifest = read_json(ROOT / QT_MANIFEST_PATH)
    contract = read_json(ROOT / CONTRACT_PATH)
    sbom_path = arguments.sbom.resolve()
    try:
        sbom_bytes = sbom_path.read_bytes()
    except OSError:
        raise InventoryError("sbom_unreadable") from None
    try:
        sbom = json.loads(sbom_bytes.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError):
        raise InventoryError("sbom_unreadable") from None
    archive = resolve_archive(arguments, qt_manifest)
    pinned_sbom = (qt_manifest.get("license") or {}).get("evidence", {}).get("sdk_sbom_sha256")
    if pinned_sbom != sha256_bytes(sbom_bytes):
        raise InventoryError("sbom_manifest_identity")
    inventory, texts = derive_inventory(archive, sbom, sha256_bytes(sbom_bytes),
                                        arguments.sdk_dir.resolve(), contract, qt_manifest)
    inventory_path = arguments.inventory.resolve()
    licenses_dir = arguments.licenses_dir.resolve()
    if write:
        write_outputs(inventory, texts, inventory_path, licenses_dir)
    else:
        compare(inventory_path, inventory, texts, licenses_dir)
    return {"schema": OPERATION_SCHEMA, "status": "passed",
            "operation": "build" if write else "check",
            "components": len(inventory["components"]),
            "license_texts": len(inventory["license_texts"]),
            "deployed_files": len(inventory["deployed_files"]),
            "sbom_sha256": inventory["sbom"]["sha256"],
            "source_archive_sha256": inventory["source"]["sha256"],
            "scope": "Inventory/license evidence only; no redistribution or platform acceptance."}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for command, description in (("build", "Write the inventory and license texts"),
                                 ("check", "Re-derive from the pinned inputs and require exact equality")):
        subparser = commands.add_parser(command, help=description)
        subparser.add_argument("--source-archive", type=Path, default=None,
                               help="Pinned qtbase source archive; acquired from --cache when omitted")
        subparser.add_argument("--cache", type=Path, default=ROOT / "local/qt-downloads")
        subparser.add_argument("--offline", action="store_true",
                               help="Require the pinned source archive to be already cached")
        subparser.add_argument("--sbom", type=Path, required=True,
                               help="SPDX JSON from the prepared Qt SDK sbom directory")
        subparser.add_argument("--sdk-dir", type=Path, required=True,
                               help="Prepared Qt SDK prefix used to compare SBOM SHA-1 file records")
        subparser.add_argument("--inventory", type=Path, default=ROOT / INVENTORY_PATH)
        subparser.add_argument("--licenses-dir", type=Path, default=ROOT / LICENSES_PATH)
    arguments = parser.parse_args(argv)
    try:
        result = run(arguments, arguments.command == "build")
    except InventoryError as error:
        result = {"schema": OPERATION_SCHEMA, "status": "failed",
                  "operation": arguments.command, "error": str(error)}
    except (OSError, ValueError, KeyError, TypeError):
        result = {"schema": OPERATION_SCHEMA, "status": "failed",
                  "operation": arguments.command, "error": "inventory_input_failed"}
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
