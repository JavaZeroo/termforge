"""Fixed-runtime preparation through the real CLI and synthetic CAB inputs."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DLL = bytearray(128)
DLL[:2] = b'MZ'
struct.pack_into('<I', DLL, 60, 64)
DLL[64:70] = b'PE\0\0\x64\x86'
DLL[90:111] = b'TF_SYNTHETIC_CRT_ONLY!'
DLL = bytes(DLL)


def cabinet(files):
    """One uncompressed CAB folder; decoded by CMake's independent reader."""
    entries, contents = bytearray(), bytearray()
    for name, data in files:
        entries += struct.pack('<IIHHHH', len(data), len(contents), 0, 0, 0, 0x20)
        entries += name.encode('ascii') + b'\0'
        contents += data
    offset = 44 + len(entries)
    size = offset + 8 + len(contents)
    header = struct.pack('<4s5IBB5H', b'MSCF', 0, size, 0, 44, 0, 3, 1, 1, len(files), 0, 1, 0)
    return header + struct.pack('<IHH', offset, 1, 0) + entries + struct.pack('<IHH', 0, len(contents), len(contents)) + contents


class RuntimePreparationCLI(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root/'tools').mkdir()
        shutil.copyfile(ROOT/'tools/prepare_qt_build.py', self.root/'tools/prepare_qt_build.py')
        self.metadata = self.root/'third_party/msvc_runtime'
        self.metadata.mkdir(parents=True)
        self.cache = self.root/'cache'
        self.cache.mkdir()
        self.out = self.root/'dependencies'
        self.summary = self.root/'summary.json'
        inner = cabinet([('vcruntime140.dll_amd64', DLL)])
        attached = cabinet([('a4', inner)])
        prefix = b'TF synthetic installer\n'
        package = prefix + attached
        notice, license_document = b'Synthetic runtime notice\n', b'Synthetic license document\n'
        digest = lambda data: hashlib.sha256(data).hexdigest()
        manifest = {
            'schema': 'termforge-msvc-runtime-input-v1', 'version': '14.51.36247.0',
            'architecture': 'windows-x64', 'pe_machine': '0x8664',
            'package': {'archive': 'runtime.exe', 'url': 'https://example.invalid/runtime.exe', 'bytes': len(package), 'sha256': digest(package)},
            'attached_container': {'offset': len(prefix), 'bytes': len(attached), 'sha256': digest(attached)},
            'runtime_cabinet': {'member': 'a4', 'bytes': len(inner), 'sha256': digest(inner)},
            'files': {'vcruntime140.dll': {'member': 'vcruntime140.dll_amd64', 'bytes': len(DLL), 'sha256': digest(DLL)}},
            'license': {'archive': 'license.docx', 'url': 'https://example.invalid/license.docx', 'bytes': len(license_document), 'sha256': digest(license_document), 'deployed_name': 'LICENSE-MICROSOFT-VC-REDIST.docx'},
            'notice_sha256': digest(notice)}
        (self.metadata/'manifest.json').write_text(json.dumps(manifest), encoding='utf-8')
        (self.metadata/'NOTICE.txt').write_bytes(notice)
        (self.cache/'runtime.exe').write_bytes(package)
        (self.cache/'license.docx').write_bytes(license_document)

    def run_cli(self):
        return subprocess.run([sys.executable, '-B', *(['-O'] if sys.flags.optimize else []), str(self.root/'tools/prepare_qt_build.py'),
                               '--crt-only', '--offline', '--cache', str(self.cache),
                               '--out', str(self.out), '--summary', str(self.summary)],
                              cwd=self.root, capture_output=True, text=True, timeout=30)

    def test_fixed_runtime_can_be_prepared_without_a_qt_sdk_or_installer_execution(self):
        completed = self.run_cli()
        self.assertEqual(completed.returncode, 0, completed.stderr)
        report = json.loads(completed.stdout)
        self.assertEqual(report['status'], 'passed')
        self.assertEqual((self.out/'crt/vcruntime140.dll').read_bytes(), DLL)
        self.assertEqual((self.out/'crt/LICENSE-MICROSOFT-VC-REDIST.docx').read_bytes(), b'Synthetic license document\n')
        self.assertEqual((self.out/'crt/NOTICE.txt').read_bytes(), b'Synthetic runtime notice\n')
        self.assertEqual({item.name for item in self.out.iterdir()}, {'crt'})
        self.assertNotIn(str(self.root), completed.stdout)
        self.assertEqual(report, json.loads(self.summary.read_text()))

    def reject(self, expected):
        completed = self.run_cli()
        self.assertEqual(completed.returncode, 1, completed.stderr)
        report = json.loads(completed.stdout)
        self.assertEqual(report['error'], expected)
        self.assertFalse(self.out.exists())
        self.assertNotIn(str(self.root), completed.stdout)

    def change_manifest(self, update):
        path = self.metadata/'manifest.json'
        manifest = json.loads(path.read_text())
        update(manifest)
        path.write_text(json.dumps(manifest))

    def test_container_slice_identity_is_required_before_extracting(self):
        self.change_manifest(lambda manifest: manifest['attached_container'].update(sha256='0'*64))
        self.reject('crt_container_identity')

    def test_inner_cabinet_identity_is_required_before_runtime_extraction(self):
        self.change_manifest(lambda manifest: manifest['runtime_cabinet'].update(sha256='0'*64))
        self.reject('crt_cabinet_identity')

    def test_runtime_member_identity_is_required_before_publishing(self):
        self.change_manifest(lambda manifest: manifest['files']['vcruntime140.dll'].update(sha256='0'*64))
        self.reject('crt_file_identity')

    def test_license_identity_is_required_and_corrupt_cache_is_preserved(self):
        path = self.cache/'license.docx'
        path.write_bytes(b'changed license')
        self.reject('archive_identity')
        self.assertEqual(path.read_bytes(), b'changed license')

    def test_notice_identity_is_required(self):
        (self.metadata/'NOTICE.txt').write_bytes(b'changed notice')
        self.reject('crt_notice_identity')

    def test_archive_member_cannot_name_a_parent_directory(self):
        self.change_manifest(lambda manifest: manifest['runtime_cabinet'].update(member='../a4'))
        self.reject('crt_member_name')


if __name__ == '__main__':
    unittest.main()
