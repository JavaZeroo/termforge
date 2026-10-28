"""App-local deployment and complete package inventory through the public CLI."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RuntimeDeploymentCLI(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / 'tools').mkdir()
        shutil.copyfile(ROOT / 'tools/msvc_runtime.py', self.root / 'tools/msvc_runtime.py')
        metadata = self.root / 'third_party/msvc_runtime'
        metadata.mkdir(parents=True)
        self.source = self.root / 'source'
        self.source.mkdir()
        self.destination = self.root / 'runtime'
        self.destination.mkdir()
        self.contents = {'vcruntime140.dll': b'Synthetic pinned runtime\n',
                         'LICENSE-MICROSOFT-VC-REDIST.docx': b'Synthetic original license\n',
                         'NOTICE.txt': b'Synthetic notice\n'}
        digest = lambda data: hashlib.sha256(data).hexdigest()
        for name, data in self.contents.items():
            (self.source / name).write_bytes(data)
        self.manifest = {'schema': 'termforge-msvc-runtime-input-v1',
                         'version': '14.51.36247.0', 'architecture': 'windows-x64',
                         'maximum_toolset_version': '14.51.36247',
                         'files': {'vcruntime140.dll': {'bytes': len(self.contents['vcruntime140.dll']),
                                                       'sha256': digest(self.contents['vcruntime140.dll'])}},
                         'license': {'deployed_name': 'LICENSE-MICROSOFT-VC-REDIST.docx',
                                     'bytes': len(self.contents['LICENSE-MICROSOFT-VC-REDIST.docx']),
                                     'sha256': digest(self.contents['LICENSE-MICROSOFT-VC-REDIST.docx'])},
                         'notice_sha256': digest(self.contents['NOTICE.txt'])}
        (metadata / 'manifest.json').write_text(json.dumps(self.manifest), encoding='utf-8')
        (metadata / 'NOTICE.txt').write_bytes(self.contents['NOTICE.txt'])

    def cli(self, command, *arguments):
        return subprocess.run([sys.executable, '-B', *(['-O'] if sys.flags.optimize else []),
                               str(self.root / 'tools/msvc_runtime.py'), command, *map(str, arguments)],
                              cwd=self.root, capture_output=True, text=True, timeout=15)

    def deploy(self, version='14.51.36231'):
        return self.cli('deploy', '--source', self.source, '--destination', self.destination,
                        '--toolset-version', version)

    def junction(self, link, target):
        completed = subprocess.run(['cmd', '/c', 'mklink', '/J', str(link), str(target)],
                                   capture_output=True, timeout=10)
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)

    def test_deployment_repairs_files_without_changing_unrelated_qt_bytes(self):
        (self.destination / 'Qt6Core.dll').write_bytes(b'unchanged Qt')
        (self.destination / 'vcruntime140.dll').write_bytes(b'damaged output')
        completed = self.deploy()
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        for name, data in self.contents.items():
            self.assertEqual((self.destination / name).read_bytes(), data)
        self.assertEqual((self.destination / 'Qt6Core.dll').read_bytes(), b'unchanged Qt')
        self.assertNotIn(str(self.root), completed.stdout)

    def test_corrupt_source_rejects_before_any_destination_file_changes(self):
        (self.source / 'vcruntime140.dll').write_bytes(b'corrupt')
        sentinel = self.destination / 'vcruntime140.dll'
        sentinel.write_bytes(b'original destination')
        completed = self.deploy()
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(json.loads(completed.stdout)['error'], 'crt_file_identity')
        self.assertEqual(sentinel.read_bytes(), b'original destination')
        self.assertEqual(len(list(self.destination.iterdir())), 1)

    def test_missing_original_license_is_a_deployment_failure(self):
        (self.source / 'LICENSE-MICROSOFT-VC-REDIST.docx').unlink()
        completed = self.deploy()
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(list(self.destination.iterdir()), [])

    def test_newer_toolset_is_rejected_without_copy(self):
        completed = self.deploy('14.51.36248')
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(json.loads(completed.stdout)['error'], 'crt_toolset_newer_than_runtime')
        self.assertEqual(list(self.destination.iterdir()), [])

    def test_compiler_self_version_is_not_a_toolset_version(self):
        completed = self.deploy('19.51.36260')
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(json.loads(completed.stdout)['error'], 'crt_toolset_version')

    def test_unexpected_source_file_cannot_enter_runtime_bundle(self):
        (self.source / 'vcomp140.dll').write_bytes(b'unrequested OpenMP')
        completed = self.deploy()
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(json.loads(completed.stdout)['error'], 'crt_source_files')
        self.assertEqual(list(self.destination.iterdir()), [])

    def test_source_and_destination_cannot_overlap(self):
        completed = self.cli('deploy', '--source', self.source, '--destination', self.source / 'nested',
                             '--toolset-version', '14.51.36231')
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(json.loads(completed.stdout)['error'], 'crt_directory_overlap')

    def test_manifest_binds_the_complete_package_and_is_stable_on_repeat(self):
        self.assertEqual(self.deploy().returncode, 0)
        (self.destination / 'product.exe').write_bytes(b'synthetic product')
        backend = self.destination / 'conpty-backend'
        backend.mkdir()
        (backend / 'conpty.dll').write_bytes(b'synthetic backend')
        arguments = ('--directory', self.destination, '--executable', 'product.exe')
        completed = self.cli('manifest', *arguments)
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        report = json.loads((self.destination / 'runtime-manifest.json').read_text())
        expected = {**self.contents, 'product.exe': b'synthetic product',
                    'conpty-backend/conpty.dll': b'synthetic backend'}
        self.assertEqual(set(report['files']), set(expected))
        for name, data in expected.items():
            self.assertEqual(report['files'][name], {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
        self.assertNotIn(str(self.root), json.dumps(report))
        original = (self.destination / 'runtime-manifest.json').read_bytes()
        self.assertEqual(self.cli('manifest', *arguments).returncode, 0)
        self.assertEqual((self.destination / 'runtime-manifest.json').read_bytes(), original)

    def test_inventory_rejects_a_package_with_changed_runtime(self):
        self.assertEqual(self.deploy().returncode, 0)
        (self.destination / 'product.exe').write_bytes(b'synthetic product')
        (self.destination / 'vcruntime140.dll').write_bytes(b'changed deployed runtime')
        completed = self.cli('manifest', '--directory', self.destination, '--executable', 'product.exe')
        self.assertEqual(completed.returncode, 1)
        self.assertFalse((self.destination / 'runtime-manifest.json').exists())

    @unittest.skipUnless(sys.platform == 'win32', 'NTFS directory junction boundary')
    def test_inventory_rejects_child_junction_without_listing_outside_files(self):
        self.assertEqual(self.deploy().returncode, 0)
        (self.destination / 'product.exe').write_bytes(b'synthetic product')
        outside = self.root / 'outside'
        outside.mkdir()
        (outside / 'private-outside-name.txt').write_bytes(b'outside the runtime tree')
        self.junction(self.destination / 'outside-link', outside)
        completed = self.cli('manifest', '--directory', self.destination, '--executable', 'product.exe')
        self.assertEqual(completed.returncode, 1, completed.stdout + completed.stderr)
        self.assertEqual(json.loads(completed.stdout)['error'], 'runtime_reparse_point')
        self.assertFalse((self.destination / 'runtime-manifest.json').exists())
        self.assertNotIn('private-outside-name', completed.stdout)
        self.assertEqual((outside / 'private-outside-name.txt').read_bytes(), b'outside the runtime tree')

    @unittest.skipUnless(sys.platform == 'win32', 'NTFS directory junction boundary')
    def test_verify_and_deploy_reject_a_source_junction_before_resolving_it(self):
        link = self.root / 'source-link'
        self.junction(link, self.source)
        for command in ('verify', 'deploy'):
            with self.subTest(command=command):
                arguments = ['--source', link, '--toolset-version', '14.51.36231']
                if command == 'deploy':
                    arguments.extend(['--destination', self.destination])
                completed = self.cli(command, *arguments)
                self.assertEqual(completed.returncode, 1, completed.stdout + completed.stderr)
                self.assertEqual(json.loads(completed.stdout)['error'], 'runtime_reparse_point')
                self.assertEqual(list(self.destination.iterdir()), [])
                self.assertNotIn(str(self.root), completed.stdout)

    @unittest.skipUnless(sys.platform == 'win32', 'NTFS directory junction boundary')
    def test_deploy_rejects_destination_junction_without_changing_its_target(self):
        target = self.root / 'outside-destination'
        target.mkdir()
        link = self.root / 'destination-link'
        self.junction(link, target)
        completed = self.cli('deploy', '--source', self.source, '--destination', link,
                             '--toolset-version', '14.51.36231')
        self.assertEqual(completed.returncode, 1, completed.stdout + completed.stderr)
        self.assertEqual(json.loads(completed.stdout)['error'], 'runtime_reparse_point')
        self.assertEqual(list(target.iterdir()), [])
        self.assertNotIn(str(self.root), completed.stdout)

    @unittest.skipUnless(sys.platform == 'win32', 'NTFS directory junction boundary')
    def test_inventory_rejects_runtime_root_junction_without_writing_a_manifest(self):
        self.assertEqual(self.deploy().returncode, 0)
        (self.destination / 'product.exe').write_bytes(b'synthetic product')
        link = self.root / 'runtime-link'
        self.junction(link, self.destination)
        completed = self.cli('manifest', '--directory', link, '--executable', 'product.exe')
        self.assertEqual(completed.returncode, 1, completed.stdout + completed.stderr)
        self.assertEqual(json.loads(completed.stdout)['error'], 'runtime_reparse_point')
        self.assertFalse((self.destination / 'runtime-manifest.json').exists())
        self.assertNotIn(str(self.root), completed.stdout)

    @unittest.skipUnless(sys.platform == 'win32', 'NTFS directory junction boundary')
    def test_all_operations_reject_a_junction_ancestor_before_resolving_it(self):
        target = self.root / 'linked-target'
        target.mkdir()
        shutil.copytree(self.source, target / 'source')
        shutil.copytree(self.source, target / 'runtime')
        (target / 'runtime/product.exe').write_bytes(b'synthetic product')
        link = self.root / 'workspace-link'
        self.junction(link, target)
        operations = [
            ('verify', '--source', link / 'source', '--toolset-version', '14.51.36231'),
            ('deploy', '--source', link / 'source', '--destination', self.destination,
             '--toolset-version', '14.51.36231'),
            ('deploy', '--source', self.source, '--destination', link / 'runtime',
             '--toolset-version', '14.51.36231'),
            ('manifest', '--directory', link / 'runtime', '--executable', 'product.exe')]
        for operation in operations:
            with self.subTest(operation=operation[0], path_role=operation[1]):
                completed = self.cli(*operation)
                self.assertEqual(completed.returncode, 1, completed.stdout + completed.stderr)
                self.assertEqual(json.loads(completed.stdout)['error'], 'runtime_reparse_point')
                self.assertFalse((target / 'runtime/runtime-manifest.json').exists())
                self.assertEqual(list(self.destination.iterdir()), [])
                self.assertNotIn(str(self.root), completed.stdout)


if __name__ == '__main__':
    unittest.main()
