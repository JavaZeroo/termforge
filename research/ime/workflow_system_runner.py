"""Two fresh bounded E3 workflows on the designated disposable Windows VM.

This entry never creates, switches or deletes a desktop. Ordinary local use is
rejected before path resolution, imports of the sampler, or directory creation.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
from research.ime.inventory_hosted import claim_directory, require_hosted, write_json
from research.ime.product_runner import _run_gated_command, file_identity

MODE = 'ui-workflow-system'
TIMEOUT = 90
DESKTOP_POLICY = 'existing-input-desktop-no-create-switch-or-delete'
BINARIES = ('shell_test_launcher.exe', 'termforge_shell.exe', 'shell_text_client.exe', 'shell_input_client.exe',
            'shell_clipboard_reader.exe')
# Explicit source identity; this is a hosted build record, not independent
# source-to-binary attestation. Worktree CRLF normalization remains observable.
SOURCES = (
    'CMakeLists.txt', '.github/workflows/r1-ime.yml',
    'experiments/shell/shell.cpp', 'experiments/shell/workspace.hpp',
    'experiments/shell/workspace_interactions.hpp', 'experiments/shell/workspace_workflow_scenario.hpp',
    'experiments/shell/workflow_system_state.hpp', 'experiments/shell/workspace_workflow_system.hpp',
    'experiments/shell/workflow_scenario_state.hpp', 'experiments/shell/workflow_controller.hpp',
    'experiments/shell/workflow_fixture.hpp', 'experiments/shell/runtime_observation.hpp',
    'experiments/shell/operation_observation.hpp', 'experiments/shell/workflow_resources.py',
    'experiments/shell/workflow_resource_phases.py', 'experiments/shell/workflow_sessions_checks.py',
    'experiments/shell/workflow_checks.py', 'experiments/shell/queues.hpp', 'experiments/shell/queue_probe.cpp',
    'experiments/shell/resize.hpp',
    'experiments/shell/raw_console_receiver.hpp', 'experiments/shell/text_client.cpp',
    'experiments/shell/input_client.cpp', 'experiments/shell/test_launcher.cpp',
    'experiments/shell/hosted_ime_input.hpp', 'experiments/shell/ime.hpp',
    'experiments/shell/ime_input_expectation.hpp', 'experiments/shell/clipboard.hpp',
    'experiments/shell/clipboard_worker.hpp', 'experiments/shell/clipboard_worker.cpp',
    'experiments/shell/paste.hpp', 'experiments/shell/paste_prompt.hpp',
    'experiments/shell/find_bar.hpp', 'experiments/shell/text_interactions.hpp',
    'experiments/shell/search_interactions.hpp', 'experiments/shell/text_actions.hpp',
    'experiments/conpty/session.hpp', 'experiments/conpty/run.py',
    'experiments/presentation/render.hpp', 'experiments/presentation/desktop.hpp',
    'experiments/core/history_view.hpp', 'experiments/core/text_projection.hpp',
    'experiments/core/text_selection.hpp', 'experiments/core/text_search.hpp',
    'experiments/core/terminal.hpp', 'experiments/core/screen.hpp', 'experiments/core/unicode_width.hpp',
    'benchmarks/tools/sample.py', 'benchmarks/tools/summarize_resources.py', 'benchmarks/requirements.txt',
    'research/ime/product_runner.py', 'research/ime/product_checks.py', 'research/ime/inventory_hosted.py',
    'research/ime/workflow_system_runner.py', 'research/ime/workflow_system_checks.py',
    'research/ime/test_workflow_system.py', 'research/ime/test_workflow_system_runner.py',
    'assets/fonts/RobotoMono-Regular.ttf',
)


def _require(value, detail):
    if not value:
        raise ValueError(detail)


def _read(path, maximum):
    with Path(path).open('rb') as stream:
        raw = stream.read(maximum + 1)
    _require(len(raw) <= maximum, 'workflow evidence exceeds its bound')
    return raw


def _identities(launcher):
    return ({name: file_identity(ROOT / name) for name in SOURCES},
            {name: file_identity(launcher.with_name(name)) for name in BINARIES})


def _verify_resources(directory):
    from experiments.shell.workflow_resources import summarize_resource_observations
    from experiments.shell.workflow_resource_phases import summarize_resource_phases
    summary = json.loads(_read(directory / 'resource-summary.json', 4 * 1024 * 1024))
    derived = summarize_resource_observations(directory / 'resource-observations.jsonl')
    _require(summary.get('schema') == 'termforge-workflow-resources-v1' and summary.get('status') == 'recorded' and
             summary.get('scope') == 'launcher-tree' and summary.get('root_identity_verified') is True and
             summary.get('sampler_started') is True and summary.get('sampler_joined') is True and
             summary.get('performance_gate') == 'not_evaluated' and
             summary.get('observations') == {'file': 'resource-observations.jsonl', 'summary': derived},
             'actual resource sampler/derived identity did not pass')
    phases = summary.get('phase_windows')
    _require(type(phases) is dict and phases.get('status') == 'mapped', 'resource clock/phase mapping is unknown')
    checked = summarize_resource_phases(directory / 'resource-observations.jsonl', directory / 'resource-phases.jsonl',
                                        calibration=phases.get('calibration'), marks=summary.get('marks'))
    _require(checked == phases and checked.get('status') == 'mapped', 'resource phase replay differs')
    return {'scope': 'launcher-tree', 'phase_status': 'mapped', 'performance_gate': 'not_evaluated',
            'sample_count': derived['sample_count']}


def _safe_native_output(raw):
    """Accept only the two fixed native JSON records; return no arbitrary text."""
    def unique_object(pairs):
        value = {}
        for key,item in pairs:
            _require(key not in value, 'duplicate native JSON key cannot be publicly exported')
            value[key] = item
        return value
    _require(len(raw) <= 1024 * 1024 and raw.endswith(b'\n'), 'native output bound/truncation')
    lines = raw.splitlines()
    _require(len(lines) == 2 and all(lines), 'native output must contain exactly two records')
    shell, launcher = (json.loads(line.decode('utf-8'),object_pairs_hook=unique_object) for line in lines)
    _require(type(shell) is dict and shell == dict(valid=True, mode=MODE, sessions_closed=18,
             active_children_after=0, presented_done=None) and shell['valid'] is True and
             type(shell['sessions_closed']) is int and type(shell['active_children_after']) is int,
             'native shell cleanup output is not the fixed safe schema')
    _require(type(launcher) is dict and set(launcher) == {'schema','mode','child_exit_code','child_signaled',
             'timed_out','inner_job_after'} and launcher['schema'] == 'termforge-hosted-ime-launcher-v1' and
             launcher['mode'] == MODE and type(launcher['child_exit_code']) is int and launcher['child_exit_code'] == 0 and
             launcher['child_signaled'] is True and launcher['timed_out'] is False,
             'native launcher cleanup output is not the fixed safe schema')
    job = launcher['inner_job_after']
    _require(type(job) is dict and set(job) == {'active','total','terminated'} and
             all(type(job[key]) is int and job[key] >= 0 for key in job) and
             job['active'] == 0 and job['total'] >= 1 and job['terminated'] <= job['total'], 'native inner Job counts')
    return shell, launcher


def _export_streams(directory, record):
    """Copy validated original bytes only. Failed/unrecognized output stays private."""
    result = {'files': {'stdout.jsonl': None, 'stderr.txt': None}, 'native_shell': None, 'native_launcher': None}
    streams = record.get('streams', {})
    stdout = directory / 'stdout.jsonl'
    try:
        raw = _read(stdout, 1024 * 1024)
        _require(streams.get('stdout.jsonl') == {'bytes':len(raw), 'sha256':hashlib.sha256(raw).hexdigest()},
                 'original stdout changed before safe export')
        shell, launcher = _safe_native_output(raw)
    except (OSError, ValueError, TypeError, KeyError):
        pass
    else:
        public = directory / 'public'; public.mkdir(exist_ok=True)
        with (public / 'stdout.jsonl').open('xb') as stream:
            stream.write(raw)
        result.update(native_shell=shell, native_launcher=launcher)
        result['files']['stdout.jsonl'] = 'public/stdout.jsonl'
    stderr = directory / 'stderr.txt'
    if streams.get('stderr.txt') == {'bytes':0, 'sha256':hashlib.sha256(b'').hexdigest()} and stderr.stat().st_size == 0:
        public = directory / 'public'; public.mkdir(exist_ok=True)
        with (public / 'stderr.txt').open('xb'):
            pass
        result['files']['stderr.txt'] = 'public/stderr.txt'
    return result


def _public_case(record, index):
    """Construct public facts; never copy a private record or raw error message."""
    result = dict(schema='termforge-workflow-system-case-v1', mode=MODE, round=index,
                  status='passed' if record.get('status') == 'passed' else 'failed',
                  content_verified=record.get('content_verified') is True,
                  resources_verified=record.get('resources_verified') is True,
                  real_system_workflow_verified=record.get('status') == 'passed',
                  desktop_policy=DESKTOP_POLICY, presented_done=None, complete_r1_verified=False)
    for name in ('assigned', 'gate_released', 'process_reaped', 'timed_out', 'forced_job_termination'):
        value = record.get(name)
        _require(value is None or type(value) is bool, 'unsafe public process flag')
        result[name] = value
    code = record.get('returncode')
    _require(code is None or type(code) is int, 'unsafe public return code')
    result['returncode'] = code
    for name in ('outer_job_before_cleanup', 'outer_job_after'):
        group = record.get(name)
        if group is not None:
            _require(type(group) is dict and all(type(group.get(key)) is int and group[key] >= 0
                     for key in ('active', 'total', 'terminated')), 'unsafe public Job counts')
            group = {key: group[key] for key in ('active', 'total', 'terminated')}
        result[name] = group
    result['cleanup_error_count'] = len(record.get('cleanup_errors', []))
    result['evidence_error_count'] = len(record.get('evidence_errors', []))
    stages = ('starting','awaiting-go','job-assigned','releasing-go','finished')
    result['lifecycle'] = []
    elapsed = 0
    for point in record.get('lifecycle', []):
        time = point.get('elapsed_seconds')
        _require(point.get('stage') in stages and type(time) in (int,float) and math.isfinite(time) and
                 time >= elapsed, 'unsafe public GO lifecycle')
        elapsed = time
        result['lifecycle'].append({'stage':point['stage'], 'elapsed_seconds':time})
    _require(len(result['lifecycle']) <= 5, 'public lifecycle budget')
    result['streams'] = {}
    for name, identity in record.get('streams', {}).items():
        _require(name in ('stdout.jsonl','stderr.txt') and type(identity) is dict and
                 set(identity) == {'bytes','sha256'} and type(identity['bytes']) is int and identity['bytes'] >= 0 and
                 type(identity['sha256']) is str and re.fullmatch('[0-9a-f]{64}',identity['sha256']) is not None,
                 'unsafe public stream identity')
        result['streams'][name] = dict(identity)
    exported = record.get('public_stream_export', {'files':{'stdout.jsonl':None,'stderr.txt':None},
                                                  'native_shell':None,'native_launcher':None})
    result['public_streams'] = {}
    for name in ('stdout.jsonl','stderr.txt'):
        path = exported['files'][name]
        _require(path is None or path == 'public/'+name, 'unsafe public stream path')
        result['public_streams'][name] = path
    result['stderr_error_bytes'] = result['streams'].get('stderr.txt',{}).get('bytes')
    result['native_shell'], result['native_launcher'] = exported['native_shell'], exported['native_launcher']
    if result['native_shell'] is not None or result['native_launcher'] is not None:
        # Recheck every copied object's complete schema before JSON export.
        _safe_native_output((json.dumps(result['native_shell'])+'\n'+json.dumps(result['native_launcher'])+'\n').encode())
    if result['status'] == 'passed':
        _require(result['content_verified'] and result['resources_verified'] and
                 all(result[key] is True for key in ('assigned', 'gate_released', 'process_reaped')) and
                 result['timed_out'] is False and result['forced_job_termination'] is False and code == 0 and
                 all(result[key] is not None and result[key]['active'] == 0 for key in
                     ('outer_job_before_cleanup', 'outer_job_after')) and
                 result['cleanup_error_count'] == result['evidence_error_count'] == 0,
                 'incomplete workflow cannot be exported as passed')
        _require([p['stage'] for p in result['lifecycle']] == list(stages) and
                 result['public_streams'] == {'stdout.jsonl':'public/stdout.jsonl','stderr.txt':'public/stderr.txt'} and
                 result['native_shell'] is not None and result['stderr_error_bytes'] == 0,
                 'public native cleanup/GO evidence is incomplete')
    # Error text, exception-provided names, environment, PID, paths, stdout and
    # private process identities are deliberately outside this schema.
    reason = record.get('failure_stage')
    result['error_kind'] = (None if result['status'] == 'passed' else
        'NativeDeadlineExceeded' if record.get('timed_out') is True else
        'CleanupFailed' if result['cleanup_error_count'] else
        'EvidenceCollectionFailed' if result['evidence_error_count'] else
        'NativeProcessFailed' if type(code) is int and code != 0 else
        {'content':'ContentOracleRejected','resources':'ResourceEvidenceRejected',
         'identity':'SourceOrBinaryIdentityChanged','public':'PublicEvidenceRejected'}.get(reason,'WorkflowEvidenceIncomplete'))
    result['verification'] = None
    if result['content_verified']:
        checked = record['verification']
        result['verification'] = {key: checked[key] for key in ('snapshots', 'ordered_events', 'guarded_key_batches',
                                    'native_shortcuts', 'companion_observations', 'combined_observations')}
        _require(all(type(value) is int and value >= 0 for value in result['verification'].values()),
                 'unsafe public verification count')
    result['resources'] = record.get('resources_verification') if result['resources_verified'] else None
    if result['resources'] is not None:
        value = result['resources']
        _require(type(value) is dict and set(value) == {'scope', 'phase_status', 'performance_gate', 'sample_count'} and
                 value['scope'] == 'launcher-tree' and value['phase_status'] == 'mapped' and
                 value['performance_gate'] == 'not_evaluated' and type(value['sample_count']) is int and
                 value['sample_count'] > 0, 'unsafe public resource summary')
    return result


def execute_hosted_workflow(launcher, out):
    # Never test the effective path by forging these values locally.
    require_hosted(os.environ)
    _require(os.name == 'nt', 'hosted workflow requires Windows')
    launcher = Path(launcher).resolve(strict=True)
    _require(launcher.name == 'shell_test_launcher.exe', 'workflow requires the fixed native launcher')
    out = Path(out).resolve()
    workspace = Path(os.environ.get('GITHUB_WORKSPACE', ROOT)).resolve()
    _require(out != workspace and out.is_relative_to(workspace), 'workflow output must be a fresh workspace directory')
    head = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
    _require(re.fullmatch('[0-9a-f]{40}', head) is not None and head == os.environ.get('GITHUB_SHA'),
             'workflow source commit differs from the hosted checkout')
    sources, binaries = _identities(launcher)
    claim_directory(out)
    write_json(out / 'source-manifest.json', dict(schema='termforge-workflow-source-identity-v1',
               commit=head, files=sources, hash_policy='sha256-raw-worktree-bytes-checkout-may-use-CRLF',
               source_to_binary_binding='hosted-build-record-not-independent-attestation'))
    write_json(out / 'binary-manifest.json', dict(schema='termforge-workflow-binary-identity-v1', files=binaries))
    report = dict(schema='termforge-workflow-system-run-v1', mode=MODE, status='failed', commit=head,
                  rounds_required=2, timeout_seconds_per_case=TIMEOUT, native_timeout_seconds=70,
                  launcher_timeout_seconds=80, desktop_policy=DESKTOP_POLICY, cases=[],
                  real_system_workflow_verified=False, complete_r1_verified=False, presented_done=None)
    write_json(out / 'result.public.json', report)
    from research.ime.workflow_system_checks import validate_workflow_system
    for index in range(2):
        directory = out / f'case-{index}'
        record = _run_gated_command(MODE, directory,
                    [str(launcher), '--hosted-ime', MODE, str(directory / 'frame.bmp')], TIMEOUT,
                    resource_observation=True)
        record['desktop_policy'] = DESKTOP_POLICY
        record['content_verified'] = record['resources_verified'] = False
        if record['status'] == 'collected':
            try:
                record['failure_stage'] = 'content'
                record['verification'] = validate_workflow_system(directory, record)
                record['content_verified'] = True
                record['failure_stage'] = 'resources'
                record['resources_verification'] = _verify_resources(directory)
                record['resources_verified'] = True
                record['failure_stage'] = 'identity'
                _require(_identities(launcher) == (sources, binaries), 'source or binary identity changed during workflow')
                record['status'] = 'passed'
            except (OSError, ValueError, TypeError, KeyError) as error:
                record['status'] = 'failed'
                record['error'] = {'kind': 'workflow-oracle-rejected', 'code': None, 'detail': str(error)}
        try:
            record['public_stream_export'] = _export_streams(directory, record)
            public = _public_case(record, index)
            write_json(directory / 'result.public.json', public)
            if record['status'] == 'passed':
                from research.ime.workflow_system_checks import validate_workflow_system_public
                validate_workflow_system_public(directory)
        except (OSError, ValueError, TypeError, KeyError) as error:
            record.update(status='failed',failure_stage='public')
            record['error'] = {'kind':'public-workflow-audit-rejected','code':None,'detail':str(error)}
            public = _public_case(record, index)
            write_json(directory / 'result.public.json', public)
        write_json(directory / 'result.json', record)
        report['cases'].append(public)
        write_json(out / 'result.public.json', report)
        # Do not repeat failed system input to chase a green result. Preserve
        # the first failure; unknown cleanup never authorizes another round.
        if public['status'] != 'passed':
            break
    report['real_system_workflow_verified'] = len(report['cases']) == 2 and all(
        row['real_system_workflow_verified'] for row in report['cases'])
    report['status'] = 'passed' if report['real_system_workflow_verified'] else 'failed'
    write_json(out / 'result.public.json', report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--launcher', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    try:
        result = execute_hosted_workflow(args.launcher, args.out)
    except (OSError, ValueError) as error:
        parser.exit(2, str(error) + '\n')
    print(json.dumps({'status': result['status'], 'real_system_workflow_verified': result['real_system_workflow_verified']}))
    return 0 if result['real_system_workflow_verified'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
