"""Verify, deploy and inventory the manifest-pinned app-local Microsoft CRT.

This build/packaging tool never downloads or installs software. Every source
file is checked before deployment; unrelated Qt and product files are retained.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / 'third_party/msvc_runtime/manifest.json'


class RuntimeErrorCode(Exception):
    """A public, path-free deployment failure code."""


def digest(data):
    return hashlib.sha256(data).hexdigest()


def toolset_version(value):
    # Compiler self versions (19.x) are deliberately not interpreted as the
    # selected VC/Tools/MSVC folder's redistributable toolset version (14.x).
    if not re.fullmatch(r'14\.[0-9]+\.[0-9]+', value):
        raise RuntimeErrorCode('crt_toolset_version')
    return tuple(map(int, value.split('.')))


def is_reparse_point(path):
    information = path.lstat()
    return (stat.S_ISLNK(information.st_mode) or
            bool(getattr(information, 'st_file_attributes', 0) &
                 stat.FILE_ATTRIBUTE_REPARSE_POINT))


def directory_argument(path):
    # Preserve the caller's path until every existing node has been checked;
    # resolve() would erase junctions and symbolic links from that identity.
    absolute = Path(os.path.abspath(path))
    for node in reversed((absolute, *absolute.parents)):
        try:
            if is_reparse_point(node):
                raise RuntimeErrorCode('runtime_reparse_point')
        except FileNotFoundError:
            # Deployment may create a missing destination below real parents.
            continue
    return absolute.resolve()


def checked_files(directory, metadata, strict=False):
    """Read and validate the entire source set before a destination is changed."""
    if not directory.is_dir() or is_reparse_point(directory):
        raise RuntimeErrorCode('crt_source_directory')
    expected = dict(metadata['files'])
    license_metadata = metadata['license']
    expected[license_metadata['deployed_name']] = license_metadata
    notice = ROOT / 'third_party/msvc_runtime/NOTICE.txt'
    expected['NOTICE.txt'] = {'bytes': notice.stat().st_size,
                              'sha256': metadata['notice_sha256']}
    if strict and {path.name for path in directory.iterdir()} != set(expected):
        raise RuntimeErrorCode('crt_source_files')
    contents = {}
    for name, identity in expected.items():
        if Path(name).name != name or '/' in name or '\\' in name:
            raise RuntimeErrorCode('crt_file_name')
        path = directory / name
        if not path.is_file() or is_reparse_point(path):
            raise RuntimeErrorCode('crt_file_missing')
        data = path.read_bytes()
        if len(data) != identity['bytes'] or digest(data) != identity['sha256']:
            raise RuntimeErrorCode('crt_file_identity')
        contents[name] = data
    return contents


def atomic_write(path, data):
    # Each file is replaced atomically. This is not a directory transaction;
    # packaging must only consume a successfully completed deployment.
    candidate = None
    try:
        with tempfile.NamedTemporaryFile(prefix='.crt-', dir=path.parent, delete=False) as temporary:
            candidate = Path(temporary.name)
            temporary.write(data)
        candidate.replace(path)
    finally:
        # Windows cannot unlink this file while its writing handle is open.
        if candidate is not None:
            candidate.unlink(missing_ok=True)


def deploy(source, destination, metadata, version):
    if source == destination or source in destination.parents or destination in source.parents:
        raise RuntimeErrorCode('crt_directory_overlap')
    if toolset_version(version) > toolset_version(metadata['maximum_toolset_version']):
        raise RuntimeErrorCode('crt_toolset_newer_than_runtime')
    contents = checked_files(source, metadata, strict=True)
    destination.mkdir(parents=True, exist_ok=True)
    if is_reparse_point(destination):
        raise RuntimeErrorCode('crt_destination_directory')
    for name, data in contents.items():
        atomic_write(destination / name, data)
    return {'files': len(contents), 'toolset_version': version}


def inventory(directory, executable, metadata):
    checked_files(directory, metadata)
    if Path(executable).name != executable or '/' in executable or '\\' in executable:
        raise RuntimeErrorCode('runtime_executable_name')
    if not (directory / executable).is_file():
        raise RuntimeErrorCode('runtime_executable_missing')
    files = {}
    pending = [directory]
    while pending:
        for path in sorted(pending.pop().iterdir()):
            # Check the node before deciding to descend. Windows junctions are
            # reparse points but are not reported by Path.is_symlink().
            if is_reparse_point(path):
                raise RuntimeErrorCode('runtime_reparse_point')
            if path.is_dir():
                pending.append(path)
            elif path.is_file() and path != directory / 'runtime-manifest.json':
                data = path.read_bytes()
                files[path.relative_to(directory).as_posix()] = {'bytes': len(data), 'sha256': digest(data)}
    files = dict(sorted(files.items()))
    result = {'schema': 'termforge-windows-runtime-v1', 'executable': executable,
              'architecture': metadata['architecture'], 'msvc_runtime_version': metadata['version'],
              'msvc_runtime_manifest_sha256': digest(MANIFEST.read_bytes()), 'files': files}
    atomic_write(directory / 'runtime-manifest.json', (json.dumps(result, indent=2) + '\n').encode('utf-8'))
    return {'files': len(files), 'runtime_manifest_sha256': digest((directory / 'runtime-manifest.json').read_bytes())}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    for command in ('verify', 'deploy'):
        operation = commands.add_parser(command)
        operation.add_argument('--source', required=True, type=Path)
        operation.add_argument('--toolset-version', required=True)
        if command == 'deploy':
            operation.add_argument('--destination', required=True, type=Path)
    operation = commands.add_parser('manifest')
    operation.add_argument('--directory', required=True, type=Path)
    operation.add_argument('--executable', required=True)
    arguments = parser.parse_args(argv)
    try:
        metadata = json.loads(MANIFEST.read_text(encoding='utf-8'))
        if arguments.command == 'manifest':
            detail = inventory(directory_argument(arguments.directory), arguments.executable, metadata)
        elif arguments.command == 'deploy':
            detail = deploy(directory_argument(arguments.source), directory_argument(arguments.destination), metadata,
                            arguments.toolset_version)
        else:
            if toolset_version(arguments.toolset_version) > toolset_version(metadata['maximum_toolset_version']):
                raise RuntimeErrorCode('crt_toolset_newer_than_runtime')
            detail = {'files': len(checked_files(directory_argument(arguments.source), metadata, strict=True)),
                      'toolset_version': arguments.toolset_version}
        result = {'schema': 'termforge-msvc-runtime-operation-v1', 'status': 'passed',
                  'operation': arguments.command, 'version': metadata['version'], **detail}
    except RuntimeErrorCode as error:
        result = {'schema': 'termforge-msvc-runtime-operation-v1', 'status': 'failed', 'error': str(error)}
    except (OSError, ValueError, KeyError, TypeError):
        result = {'schema': 'termforge-msvc-runtime-operation-v1', 'status': 'failed', 'error': 'runtime_input_failed'}
    print(json.dumps(result, indent=2))
    return 0 if result['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
