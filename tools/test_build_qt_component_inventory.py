"""Offline contract tests for the deployed Qt component/license inventory builder."""
from __future__ import annotations

import argparse
import hashlib
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

if __package__:
    from . import build_qt_component_inventory as builder
else:
    import build_qt_component_inventory as builder

PREFIX = "qtbase-everywhere-src-6.10.3/"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha1(data: bytes) -> str:
    return hashlib.sha1(data).hexdigest()


def write_archive(path: Path, members: dict[str, bytes]) -> bytes:
    with tarfile.open(path, "w:xz") as archive:
        for name, data in sorted(members.items()):
            info = tarfile.TarInfo(name)
            info.size = len(data)
            archive.addfile(info, io.BytesIO(data))
    return path.read_bytes()


def attribution(member: str, entry: dict, data: bytes) -> bytes:
    return (json.dumps(entry, indent=2) + "\n").encode("utf-8")


class Fixture:
    def __init__(self, case: unittest.TestCase):
        temporary = tempfile.TemporaryDirectory(prefix="termforge qt inventory fixture ")
        case.addCleanup(temporary.cleanup)
        self.base = Path(temporary.name)
        self.root = self.base / "repo"
        self.sdk = self.base / "sdk"
        for directory in ("third_party/qt/LICENSES", "packaging/qt-product"):
            (self.root / directory).mkdir(parents=True, exist_ok=True)
        (self.sdk / "bin").mkdir(parents=True)
        (self.sdk / "plugins/platforms").mkdir(parents=True)
        (self.sdk / "bin/Qt6Core.dll").write_bytes(b"core fixture binary")
        (self.sdk / "plugins/platforms/qwindows.dll").write_bytes(b"windows plugin fixture")

        self.license_texts = {
            "Zlib": b"Zlib fixture license text\n",
            "MIT": b"MIT fixture license text\n",
            "LGPL-3.0-only": b"LGPL fixture license text\n",
        }
        self.members = {}
        for identifier, data in self.license_texts.items():
            self.members[PREFIX + f"LICENSES/{identifier}.txt"] = data
        self.members[PREFIX + "src/3rdparty/zlib/LICENSE"] = b"upstream zlib license text\n"
        self.members[PREFIX + "src/3rdparty/zlib/qt_attribution.json"] = attribution("zlib", {
            "Id": "zlib", "Name": "Data Compression Library (zlib)", "Version": "1.3.2",
            "QtUsage": "Used in Qt Core.", "Homepage": "https://zlib.net/",
            "License": "zlib License", "LicenseId": "Zlib", "LicenseFile": "LICENSE",
            "Copyright": "(C) 1995-2026 Jean-loup Gailly and Mark Adler",
        }, b"")
        self.members[PREFIX + "cmake/3rdparty/toolmod/qt_attribution.json"] = attribution("toolmod", {
            "Id": "toolmod", "Name": "Build Tool Module", "Version": "5.0.0",
            "QtUsage": "Used as part of the build system.", "QtParts": ["tools"],
            "License": "MIT License", "LicenseId": "MIT", "LicenseFile": "LICENSE",
            "Copyright": "Copyright fixture tool authors",
        }, b"")
        self.members[PREFIX + "cmake/3rdparty/toolmod/LICENSE"] = b"tool module license\n"
        self.members[PREFIX + "src/3rdparty/unused/qt_attribution.json"] = attribution("unused", {
            "Id": "unused-module", "Name": "Unused Module", "License": "MIT License",
            "LicenseId": "MIT", "Copyright": "Copyright unused",
        }, b"")
        self.members[PREFIX + "src/3rdparty/sourceonly/qt_attribution.json"] = attribution("sourceonly", {
            "Id": "source-only", "Name": "Source Only Declaration", "License": "MIT License",
            "LicenseId": "MIT", "Copyright": "Copyright source only",
        }, b"")
        self.archive_path = self.base / "qtbase.tar.xz"
        self.archive_bytes = write_archive(self.archive_path, self.members)

        self.sbom = {
            "spdxVersion": "SPDX-2.3",
            "creationInfo": {"created": "2026-01-01T00:00:00Z"},
            "packages": [
                {"SPDXID": "SPDXRef-Package-core", "name": "Core"},
                {"SPDXID": "SPDXRef-Package-platform", "name": "Platform"},
                {"SPDXID": "SPDXRef-Package-win", "name": "QWindowsIntegrationPlugin"},
                {"SPDXID": "SPDXRef-Package-zlib", "name": "BundledZLIB",
                 "versionInfo": "1.3.2", "downloadLocation": "https://zlib.example/zlib-1.3.2.tar.gz",
                 "licenseConcluded": "Zlib",
                 "comment": ("\nCMake target name: BundledZLIB\n    Information extracted from:\n"
                             "     /src_dir/qtbase/src/3rdparty/zlib/qt_attribution.json\n"
                             "    Entry index: 0\n    Id: zlib\n")},
                {"SPDXID": "SPDXRef-Package-toolmod", "name": "Platform_Attribution_toolmod",
                 "versionInfo": "5.0.0", "downloadLocation": "https://example.invalid/toolmod",
                 "licenseConcluded": "MIT",
                 "comment": ("\nCMake target name: Platform_Attribution_toolmod\n"
                             "    Information extracted from:\n"
                             "     /src_dir/qtbase/cmake/3rdparty/toolmod/qt_attribution.json\n"
                             "    Entry index: 0\n    Id: toolmod\n")},
                {"SPDXID": "SPDXRef-Package-unused", "name": "Unused_Attribution_unused-module",
                 "downloadLocation": "https://example.invalid/unused", "licenseConcluded": "MIT",
                 "comment": ("\n    Information extracted from:\n"
                             "     /src_dir/qtbase/src/3rdparty/unused/qt_attribution.json\n"
                             "    Entry index: 0\n    Id: unused-module\n")},
            ],
            "files": [
                {"SPDXID": "SPDXRef-File-core", "fileName": "./bin/Qt6Core.dll",
                 "checksums": [{"algorithm": "SHA1", "checksumValue": "0" * 40}]},
                {"SPDXID": "SPDXRef-File-win", "fileName": "./plugins/platforms/qwindows.dll",
                 "checksums": [{"algorithm": "SHA1", "checksumValue": "0" * 40}]},
            ],
            "relationships": [
                {"spdxElementId": "SPDXRef-Package-core", "relatedSpdxElement": "SPDXRef-File-core",
                 "relationshipType": "CONTAINS"},
                {"spdxElementId": "SPDXRef-Package-win", "relatedSpdxElement": "SPDXRef-File-win",
                 "relationshipType": "CONTAINS"},
                {"spdxElementId": "SPDXRef-Package-core", "relatedSpdxElement": "SPDXRef-Package-zlib",
                 "relationshipType": "DEPENDS_ON"},
                {"spdxElementId": "SPDXRef-Package-core", "relatedSpdxElement": "SPDXRef-Package-platform",
                 "relationshipType": "DEPENDS_ON"},
                {"spdxElementId": "SPDXRef-Package-platform",
                 "relatedSpdxElement": "SPDXRef-Package-toolmod", "relationshipType": "DEPENDS_ON"},
            ],
        }
        self.sbom_path = self.base / "qtbase.spdx.json"
        self.write_sbom()

        self.contract = {"qt_deployed": {"libraries": ["Qt6Core.dll"],
                                         "plugins": ["platforms/qwindows.dll"]}}
        (self.root / "packaging/qt-product/manifest.json").write_text(
            json.dumps(self.contract, indent=2) + "\n", encoding="utf-8")
        self.qt_manifest = {
            "version": "6.10.3",
            "source": {
                "source_archive_url": "https://download.qt.io/qtbase-everywhere-src-6.10.3.tar.xz",
                "source_archive_bytes": len(self.archive_bytes),
                "source_archive_sha256": digest(self.archive_bytes),
            },
            "license": {"evidence": {"sdk_sbom_sha256": digest(self.sbom_path.read_bytes())}},
        }
        (self.root / "third_party/qt/manifest.json").write_text(
            json.dumps(self.qt_manifest, indent=2) + "\n", encoding="utf-8")
        self.inventory_path = self.root / "third_party/qt/components.json"
        self.licenses_dir = self.root / "third_party/qt/LICENSES"

    def write_sbom(self):
        self.sbom_path.write_text(json.dumps(self.sbom, indent=2) + "\n", encoding="utf-8")

    def arguments(self, command: str) -> argparse.Namespace:
        return argparse.Namespace(
            command=command, source_archive=self.archive_path, cache=self.base / "cache",
            offline=True, sbom=self.sbom_path, sdk_dir=self.sdk,
            inventory=self.inventory_path, licenses_dir=self.licenses_dir)

    def run(self, command: str):
        with patch.object(builder, "ROOT", self.root):
            return builder.run(self.arguments(command), command == "build")


class InventoryTests(unittest.TestCase):
    def setUp(self):
        self.fixture = Fixture(self)

    def test_build_writes_components_exclusions_and_license_texts(self):
        result = self.fixture.run("build")
        self.assertEqual(result["status"], "passed")
        inventory = json.loads(self.fixture.inventory_path.read_text(encoding="utf-8"))
        self.assertEqual(inventory["schema"], builder.SCHEMA)
        self.assertEqual(inventory["deployed_files"]["Qt6Core.dll"]["module_package"], "Core")
        self.assertEqual(inventory["deployed_files"]["Qt6Core.dll"]["component_ids"], ["zlib"])
        self.assertEqual(inventory["deployed_files"]["platforms/qwindows.dll"]["component_ids"], [])
        self.assertEqual(list(inventory["components"]), ["zlib"])
        zlib = inventory["components"]["zlib"]
        self.assertEqual(zlib["license_expression"], "Zlib")
        self.assertEqual(zlib["deployed_files"], ["Qt6Core.dll"])
        self.assertTrue(zlib["attribution"]["member"].endswith("src/3rdparty/zlib/qt_attribution.json"))
        self.assertEqual(zlib["attribution"]["license_files"][0]["sha256"],
                         digest(self.fixture.members[PREFIX + "src/3rdparty/zlib/LICENSE"]))
        self.assertEqual(sorted(inventory["license_texts"]), ["LGPL-3.0-only", "Zlib"])
        self.assertEqual(inventory["license_texts"]["Zlib"]["sha256"],
                         digest(self.fixture.license_texts["Zlib"]))
        self.assertTrue((self.fixture.licenses_dir / "Zlib.txt").is_file())
        self.assertEqual([item["category"] for item in inventory["excluded_attributions"]],
                         ["internal_build_platform_attribution"])
        categories = {(item["id"], item["category"]) for item in inventory["declared_not_deployed"]}
        self.assertIn(("unused-module", "owning_package_not_deployed"), categories)
        self.assertIn(("source-only", "source_declaration_not_in_sbom"), categories)
        self.assertEqual(inventory["sbom"]["binary_file_checksums"]["matching_files"], 0)
        self.assertEqual(len(inventory["sbom"]["binary_file_checksums"]["mismatched_files"]), 2)
        self.assertEqual(self.fixture.run("check")["status"], "passed")

        first = self.fixture.inventory_path.read_bytes()
        first_texts = {path.name: path.read_bytes() for path in self.fixture.licenses_dir.glob("*.txt")}
        self.fixture.run("build")
        self.assertEqual(self.fixture.inventory_path.read_bytes(), first)
        self.assertEqual({path.name: path.read_bytes()
                          for path in self.fixture.licenses_dir.glob("*.txt")}, first_texts)

    def test_check_detects_inventory_and_license_drift(self):
        self.fixture.run("build")
        inventory = json.loads(self.fixture.inventory_path.read_text(encoding="utf-8"))
        inventory["components"]["zlib"]["name"] = "tampered"
        self.fixture.inventory_path.write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8")
        with self.assertRaises(builder.InventoryError) as raised:
            self.fixture.run("check")
        self.assertIn("inventory_mismatch", str(raised.exception))

        self.fixture.run("build")
        (self.fixture.licenses_dir / "Zlib.txt").write_bytes(b"tampered license\n")
        with self.assertRaises(builder.InventoryError) as raised:
            self.fixture.run("check")
        self.assertIn("license_text_mismatch:Zlib", str(raised.exception))

    def test_archive_and_sbom_identity_are_enforced(self):
        fixture = Fixture(self)
        fixture.qt_manifest["source"]["source_archive_sha256"] = "f" * 64
        (fixture.root / "third_party/qt/manifest.json").write_text(
            json.dumps(fixture.qt_manifest, indent=2) + "\n", encoding="utf-8")
        with self.assertRaises(builder.InventoryError) as raised:
            fixture.run("build")
        self.assertIn("source_archive_identity", str(raised.exception))

        fixture = Fixture(self)
        fixture.qt_manifest["license"]["evidence"]["sdk_sbom_sha256"] = "f" * 64
        (fixture.root / "third_party/qt/manifest.json").write_text(
            json.dumps(fixture.qt_manifest, indent=2) + "\n", encoding="utf-8")
        with self.assertRaises(builder.InventoryError) as raised:
            fixture.run("build")
        self.assertIn("sbom_manifest_identity", str(raised.exception))

    def test_missing_deployed_file_or_license_text_fails_closed(self):
        fixture = Fixture(self)
        fixture.sbom["files"] = [entry for entry in fixture.sbom["files"]
                                 if entry["fileName"] != "./bin/Qt6Core.dll"]
        fixture.write_sbom()
        fixture.qt_manifest["license"]["evidence"]["sdk_sbom_sha256"] = digest(
            fixture.sbom_path.read_bytes())
        (fixture.root / "third_party/qt/manifest.json").write_text(
            json.dumps(fixture.qt_manifest, indent=2) + "\n", encoding="utf-8")
        with self.assertRaises(builder.InventoryError) as raised:
            fixture.run("build")
        self.assertIn("sbom_file:Qt6Core.dll", str(raised.exception))

        fixture = Fixture(self)
        members = dict(fixture.members)
        del members[PREFIX + "LICENSES/Zlib.txt"]
        fixture.members = members
        fixture.archive_bytes = write_archive(fixture.archive_path, members)
        fixture.qt_manifest["source"]["source_archive_bytes"] = len(fixture.archive_bytes)
        fixture.qt_manifest["source"]["source_archive_sha256"] = digest(fixture.archive_bytes)
        (fixture.root / "third_party/qt/manifest.json").write_text(
            json.dumps(fixture.qt_manifest, indent=2) + "\n", encoding="utf-8")
        with self.assertRaises(builder.InventoryError) as raised:
            fixture.run("build")
        self.assertIn("license_text_missing:Zlib", str(raised.exception))

    def test_license_expression_parsing(self):
        self.assertEqual(builder.license_ids("FTL OR GPL-2.0-only"),
                         ["FTL", "GPL-2.0-only"])
        self.assertEqual(builder.license_ids("MIT AND MIT-open-group"),
                         ["MIT", "MIT-open-group"])
        self.assertEqual(builder.license_ids("GPL-3.0-only WITH Qt-GPL-exception-1.0"),
                         ["GPL-3.0-only"])
        self.assertEqual(builder.license_ids(None), [])

    def test_sbom_checksum_comparison_records_matches(self):
        fixture = Fixture(self)
        for entry in fixture.sbom["files"]:
            name = entry["fileName"].split("/")[-1]
            local = (fixture.sdk / "bin" / name) if entry["fileName"].startswith("./bin") else (
                fixture.sdk / "plugins/platforms" / name)
            entry["checksums"] = [{"algorithm": "SHA1", "checksumValue": sha1(local.read_bytes())}]
        fixture.write_sbom()
        fixture.qt_manifest["license"]["evidence"]["sdk_sbom_sha256"] = digest(
            fixture.sbom_path.read_bytes())
        (fixture.root / "third_party/qt/manifest.json").write_text(
            json.dumps(fixture.qt_manifest, indent=2) + "\n", encoding="utf-8")
        fixture.run("build")
        inventory = json.loads(fixture.inventory_path.read_text(encoding="utf-8"))
        checks = inventory["sbom"]["binary_file_checksums"]
        self.assertEqual(checks["matching_files"], 2)
        self.assertEqual(checks["mismatched_files"], [])


if __name__ == "__main__":
    unittest.main()
