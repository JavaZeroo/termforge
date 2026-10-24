"""Opt-in launcher-tree adapter for the existing resource sampler, not a new sampler.

The caller assigns its gated Popen to its owned Job before start, sends GO only
after start returns, and calls finish even on failure. Raw resources.jsonl is
private and must be excluded from every artifact upload. This module owns no
target process and never changes a desktop, clipboard or language setting.
"""
import copy
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import threading
import time
from itertools import zip_longest

ROOT = Path(__file__).resolve().parents[2]


def _tool(name):
    spec = importlib.util.spec_from_file_location(f'_termforge_workflow_{name}', ROOT / 'benchmarks/tools' / f'{name}.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_sampler = None # sample imports psutil; ordinary resource-disabled runs must not.
_summarizer = _tool('summarize_resources')


def _sampler_tool():
    global _sampler
    if _sampler is None:
        _sampler = _tool('sample')
    return _sampler
MARKS = frozenset({'launcher-go', 'launcher-exit', 'cleanup-finished', 'controlled-child-ready',
                   'eight-ready', 'load-started', 'background-complete'} |
                  {f'cycle-{cycle}-{phase}' for cycle in range(1, 6) for phase in ('six-stable', 'eight-rebuilt')})
OBSERVATION_SCHEMA = 'termforge-workflow-resource-observations-v1'
METRICS = ('cpu_seconds_lifetime', 'cpu_seconds_since_first_observation', 'private_bytes', 'working_set_bytes', 'handles')
SAMPLE_TIMES = ('elapsed_ns', 'actual_interval_ns', 'collection_ns', 'schedule_lateness_ns')


def resource_process_identity(pid, created):
    if (type(pid) is not int or pid <= 0 or type(created) not in (int, float) or
        not math.isfinite(created) or created <= 0):
        raise ValueError('invalid private resource process identity')
    return hashlib.sha256(f'{pid}:{float(created).hex()}'.encode('ascii')).hexdigest()


def _keys(row, expected):
    if type(row) is not dict or set(row) != set(expected):
        raise ValueError('resource observation fields disagree with schema')


def _token(value):
    if type(value) is not str or re.fullmatch('[0-9a-f]{64}', value) is None:
        raise ValueError('invalid resource identity or source digest')
    return value


def _number(row, key, integer=False, positive=False):
    return _summarizer.number(row, key, integer=integer, positive=positive)


def _project_resource_row(row, raw_digest, sample_seq):
    kind = row['type']
    if kind == 'metadata':
        return {'type': kind, 'schema': OBSERVATION_SCHEMA, 'scope': 'launcher-tree',
                'root_role': 'actual-launcher-not-ui', 'clock': 'perf_counter-relative-ns',
                'root_identity': resource_process_identity(row['root_pid'], row['root_create_time']),
                'source_sha256': raw_digest, 'requested_interval_ns': row['requested_interval_ns'],
                'requested_discovery_interval_ns': row['requested_discovery_interval_ns']}
    if kind == 'sample':
        processes = []
        for p in row['processes']:
            public = {'process_identity': resource_process_identity(p['pid'], p['create_time']),
                      'group': p['group'], 'status': p['status']}
            public.update({key: p[key] if p['status'] == 'observed' else None for key in METRICS})
            processes.append(public)
        errors = []
        for error in row['errors']:
            if error.get('operation') != 'children' or error.get('error') not in ('NoSuchProcess', 'AccessDenied'):
                raise ValueError('unrecognized resource discovery error class')
            # Raw discovery errors contain PID only. A reused PID cannot be
            # safely assigned a generation, so no identity token is invented.
            errors.append({'process_identity': None, 'operation': 'children', 'error': error['error']})
        return {'type': kind, 'seq': sample_seq, **{key: row[key] for key in SAMPLE_TIMES},
                'processes': processes, 'errors': errors}
    if kind == 'schedule_gap':
        return {'type': kind, 'after_sample_seq': sample_seq, 'skipped_ticks': row['skipped_ticks']}
    if kind == 'end':
        return {'type': kind, 'elapsed_ns': row['elapsed_ns'], 'status': row['status'], 'sample_count': sample_seq}
    raise ValueError('unrecognized private resource record type')


def summarize_resource_observations(path):
    """Validate the public schema and recompute resources without private IDs."""
    digest = hashlib.sha256()
    metadata = previous = first_elapsed = end = None
    seq = 0
    identities, roles = {}, {}
    groups = {g: {'observed_processes': 0, 'observed_samples': 0, 'unavailable_rows': 0,
                  'observed_cpu_seconds': None, 'observed_cpu_core_equivalents': None,
                  **{'sampled_peak_observed_'+m: None for m in ('private_bytes','working_set_bytes','handles')}}
              for g in ('root','descendant','sampler')}
    tree = {'observed_cpu_seconds': None, 'observed_cpu_core_equivalents': None,
            **{'sampled_peak_observed_'+m: None for m in ('private_bytes','working_set_bytes','handles')}}
    quality = {'collection_ns_max': None, 'actual_interval_ns_max': None, 'schedule_lateness_ns_max': None,
               'skipped_ticks': 0, 'discovery_errors': 0, 'samples_with_unavailable_processes': 0}
    with Path(path).open('rb') as stream:
        for raw in stream:
            digest.update(raw)
            row = json.loads(raw)
            if end is not None:
                raise ValueError('resource observation after end')
            kind = row.get('type') if type(row) is dict else None
            if metadata is None:
                _keys(row, ('type','schema','scope','root_role','clock','root_identity','source_sha256',
                            'requested_interval_ns','requested_discovery_interval_ns'))
                if (kind, row['schema'], row['scope'], row['root_role'], row['clock']) != (
                    'metadata',OBSERVATION_SCHEMA,'launcher-tree','actual-launcher-not-ui','perf_counter-relative-ns'):
                    raise ValueError('unsupported resource observation schema or scope')
                _token(row['root_identity']); _token(row['source_sha256'])
                _number(row,'requested_interval_ns',integer=True,positive=True)
                _number(row,'requested_discovery_interval_ns',integer=True,positive=True)
                metadata = row
            elif kind == 'sample':
                _keys(row, ('type','seq',*SAMPLE_TIMES,'processes','errors'))
                seq += 1
                if _number(row,'seq',integer=True,positive=True) != seq:
                    raise ValueError('resource sample sequence mismatch')
                elapsed = _number(row,'elapsed_ns',integer=True)
                if previous is not None and elapsed <= previous:
                    raise ValueError('nonmonotonic public resource samples')
                expected = None if previous is None else elapsed-previous
                if row['actual_interval_ns'] != expected or (expected is not None and type(row['actual_interval_ns']) is not int):
                    raise ValueError('resource sample interval mismatch')
                for key in ('collection_ns','schedule_lateness_ns'):
                    value = _number(row,key,integer=True)
                    quality[key+'_max'] = value if quality[key+'_max'] is None else max(quality[key+'_max'],value)
                if expected is not None:
                    quality['actual_interval_ns_max'] = max(quality['actual_interval_ns_max'] or 0,expected)
                if type(row['errors']) is not list or type(row['processes']) is not list:
                    raise ValueError('resource rows and errors must be lists')
                for error in row['errors']:
                    _keys(error,('process_identity','operation','error'))
                    if error['process_identity'] is not None or error['operation'] != 'children' or error['error'] not in ('NoSuchProcess','AccessDenied'):
                        raise ValueError('invalid public resource discovery error')
                quality['discovery_errors'] += len(row['errors'])
                seen, observed, unavailable = set(), {g:[] for g in groups}, False
                for p in row['processes']:
                    _keys(p,('process_identity','group','status',*METRICS))
                    identity = _token(p['process_identity']); group = p['group']; status = p['status']
                    if group not in groups or status not in ('observed','NoSuchProcess','AccessDenied') or identity in seen:
                        raise ValueError('invalid duplicate resource identity role or status')
                    seen.add(identity)
                    if (identity == metadata['root_identity']) != (group == 'root') or roles.setdefault(identity,group) != group:
                        raise ValueError('resource identity role changed')
                    if status != 'observed':
                        if any(p[key] is not None for key in METRICS):
                            raise ValueError('unavailable resource rows must retain null metrics')
                        groups[group]['unavailable_rows'] += 1; unavailable = True; continue
                    lifetime = _number(p,'cpu_seconds_lifetime'); delta = _number(p,'cpu_seconds_since_first_observation')
                    for key in ('private_bytes','working_set_bytes','handles'):
                        _number(p,key,integer=True)
                    if identity not in identities:
                        if delta != 0:
                            raise ValueError('first public CPU observation must start at zero')
                        identities[identity] = (lifetime,lifetime,0,group); groups[group]['observed_processes'] += 1
                    base,last,_,_ = identities[identity]
                    if lifetime < last or not math.isclose(lifetime-base,delta,rel_tol=1e-9,abs_tol=1e-9):
                        raise ValueError('public resource CPU baseline mismatch')
                    identities[identity] = (base,lifetime,delta,group); observed[group].append(p)
                if not any(roles[identity] == 'sampler' for identity in seen):
                    raise ValueError('missing sampler row')
                if unavailable:
                    quality['samples_with_unavailable_processes'] += 1
                for group, processes in observed.items():
                    if processes:
                        groups[group]['observed_samples'] += 1
                        for metric in ('private_bytes','working_set_bytes','handles'):
                            key = 'sampled_peak_observed_'+metric; total = sum(p[metric] for p in processes)
                            groups[group][key] = total if groups[group][key] is None else max(groups[group][key],total)
                combined = observed['root'] + observed['descendant']
                if combined:
                    for metric in ('private_bytes','working_set_bytes','handles'):
                        key = 'sampled_peak_observed_'+metric; total = sum(p[metric] for p in combined)
                        tree[key] = total if tree[key] is None else max(tree[key],total)
                first_elapsed = elapsed if first_elapsed is None else first_elapsed
                previous = elapsed
            elif kind == 'schedule_gap':
                _keys(row,('type','after_sample_seq','skipped_ticks'))
                if not seq or _number(row,'after_sample_seq',integer=True,positive=True) != seq:
                    raise ValueError('resource gap sequence mismatch')
                quality['skipped_ticks'] += _number(row,'skipped_ticks',integer=True,positive=True)
            elif kind == 'end':
                _keys(row,('type','elapsed_ns','status','sample_count'))
                if not seq or row['status'] != 'complete' or _number(row,'sample_count',integer=True,positive=True) != seq or _number(row,'elapsed_ns',integer=True) < previous:
                    raise ValueError('invalid public resource end')
                end = row
            else:
                raise ValueError('unexpected public resource record type')
    if end is None:
        raise ValueError('truncated public resource observations')
    span = previous-first_elapsed
    for group,result in groups.items():
        deltas = [values[2] for values in identities.values() if values[3] == group]
        if deltas:
            result['observed_cpu_seconds'] = sum(deltas)
            if span > 0: result['observed_cpu_core_equivalents'] = sum(deltas)/(span/1e9)
    deltas = [values[2] for values in identities.values() if values[3] in ('root','descendant')]
    if deltas:
        tree['observed_cpu_seconds'] = sum(deltas)
        if span > 0: tree['observed_cpu_core_equivalents'] = sum(deltas)/(span/1e9)
    return {'schema':OBSERVATION_SCHEMA,'scope':'launcher-tree','validation_status':'valid','performance_gate':'not_evaluated',
            'source_sha256':metadata['source_sha256'],'observations_sha256':digest.hexdigest(),'sample_count':seq,
            'sampling_span_ns':span,'trace_elapsed_ns':end['elapsed_ns'],'groups':groups,'launcher_tree':tree,'quality':quality}


def validate_resource_observations(raw_path, observations_path, *, expected_identity):
    """Compare every public metric against the actual validated private trace."""
    raw_path, observations_path = Path(raw_path), Path(observations_path)
    raw_report = _summarizer.summarize(raw_path)
    public_report = summarize_resource_observations(observations_path)
    seq, digest = 0, hashlib.sha256()
    with raw_path.open('rb') as raw_stream, observations_path.open('rb') as public_stream:
        for raw_line, public_line in zip_longest(raw_stream,public_stream):
            if raw_line is None or public_line is None:
                raise ValueError('private/public resource record count differs')
            digest.update(raw_line)
            raw, public = json.loads(raw_line), json.loads(public_line)
            if raw['type'] != public['type']:
                raise ValueError('private/public resource record type differs')
            if raw['type'] == 'metadata':
                if (raw['root_pid'],raw['root_create_time']) != expected_identity:
                    raise ValueError('private resource root identity mismatch')
                if (public['root_identity'] != resource_process_identity(*expected_identity) or
                    public['source_sha256'] != raw_report['source_sha256'] or
                    any(public[key] != raw[key] for key in ('requested_interval_ns','requested_discovery_interval_ns'))):
                    raise ValueError('private/public resource metadata differs')
            elif raw['type'] == 'sample':
                seq += 1
                if public['seq'] != seq or any(public[key] != raw[key] for key in SAMPLE_TIMES) or len(public['processes']) != len(raw['processes']) or len(public['errors']) != len(raw['errors']):
                    raise ValueError('private/public resource sample differs')
                for actual,observed in zip(raw['processes'],public['processes']):
                    if (observed['process_identity'] != resource_process_identity(actual['pid'],actual['create_time']) or
                        observed['group'] != actual['group'] or observed['status'] != actual['status'] or
                        any(observed[key] != (actual[key] if actual['status']=='observed' else None) for key in METRICS)):
                        raise ValueError('private/public resource measurement differs')
                for actual,observed in zip(raw['errors'],public['errors']):
                    if observed['process_identity'] is not None or observed['operation'] != actual['operation'] or observed['error'] != actual['error']:
                        raise ValueError('private/public resource discovery error differs')
            elif raw['type'] == 'schedule_gap':
                if public['after_sample_seq'] != seq or public['skipped_ticks'] != raw['skipped_ticks']:
                    raise ValueError('private/public resource gap differs')
            elif public['sample_count'] != seq or public['elapsed_ns'] != raw['elapsed_ns'] or public['status'] != raw['status']:
                raise ValueError('private/public resource end differs')
    if digest.hexdigest() != raw_report['source_sha256'] or public_report['source_sha256'] != raw_report['source_sha256']:
        raise ValueError('private resource trace changed during comparison')
    return public_report


def write_resource_observations(raw_path, observations_path, *, expected_identity):
    raw_path, observations_path = Path(raw_path), Path(observations_path)
    report = _summarizer.summarize(raw_path)
    with raw_path.open('rb') as source:
        metadata = json.loads(source.readline())
    if (metadata['root_pid'],metadata['root_create_time']) != expected_identity:
        raise ValueError('private resource root identity mismatch')
    resource_process_identity(*expected_identity)
    seq = 0
    with raw_path.open('rb') as source, observations_path.open('x',encoding='utf-8',newline='\n') as public:
        for line in source:
            row = json.loads(line)
            if row['type'] == 'sample': seq += 1
            public.write(json.dumps(_project_resource_row(row,report['source_sha256'],seq),allow_nan=False)+'\n')
    return validate_resource_observations(raw_path,observations_path,expected_identity=expected_identity)


def _tree_metrics(path, expected_digest):
    """Supplement the validated v0.1 report; never sum per-process peaks."""
    digest = hashlib.sha256()
    metrics = {key: None for key in ('sampled_peak_observed_private_bytes',
                                    'sampled_peak_observed_working_set_bytes', 'sampled_peak_observed_handles')}
    group_handles = {group: None for group in _summarizer.GROUPS}
    with path.open('rb') as stream:
        for raw in stream:
            digest.update(raw)
            row = json.loads(raw)
            if row['type'] != 'sample':
                continue
            observed = [p for p in row['processes'] if p['status'] == 'observed']
            tree = [p for p in observed if p['group'] in ('root', 'descendant')]
            if tree:
                for name in ('private_bytes', 'working_set_bytes', 'handles'):
                    key = 'sampled_peak_observed_' + name
                    total = sum(p[name] for p in tree)
                    metrics[key] = total if metrics[key] is None else max(metrics[key], total)
            for group in group_handles:
                rows = [p for p in observed if p['group'] == group]
                if rows:
                    total = sum(p['handles'] for p in rows)
                    group_handles[group] = total if group_handles[group] is None else max(group_handles[group], total)
    if digest.hexdigest() != expected_digest:
        raise ValueError('private resource trace changed after validation')
    return metrics, group_handles


class WorkflowResources:
    def __init__(self, process, private_dir, *, seconds=100, interval=.1, discovery_interval=1.0):
        if not all(type(value) in (int,float) and math.isfinite(value) and value > 0
                   for value in (seconds,interval,discovery_interval)) or interval > 1 or interval*1e9 < 1:
            # Existing sample.sleep is not interruptible by Event.set. Keep
            # its sleep bounded; this is a stop budget, not a performance gate.
            raise ValueError('resource durations must be positive finite and sampling interval at most one second')
        self.process = process
        self.private_dir = Path(private_dir)
        self.trace_path = self.private_dir / 'resources.jsonl'
        self.seconds, self.interval, self.discovery_interval = seconds, interval, discovery_interval
        self._thread = None
        self._stop, self._done = threading.Event(), threading.Event()
        self._failure = self._report = self._identity = self._clock_zero = None
        self._identity_verified = False
        self._marks = []
        self._clock_before = self._clock_after = None
        self._clock_error = None

    def _capture_clock(self, batch):
        # Lazy import/API: resource-disabled execution never reaches this seam.
        from experiments.shell.workflow_resource_phases import capture_clock_brackets
        try:
            result = capture_clock_brackets(batch)
        except (OSError, ValueError, AttributeError):
            self._clock_error = 'clock-calibration-unavailable'
            result = None
        if batch == 'before-go': self._clock_before = result
        else: self._clock_after = result
        (self.private_dir/'clock-calibration.private.json').write_text(
            json.dumps({'before':self._clock_before,'after':self._clock_after,'error':self._clock_error},allow_nan=False)+'\n')

    def _verify_live_identity(self):
        if self.process.poll() is not None:
            raise ValueError('resource launcher already exited')
        actual = (self.process.pid, _sampler_tool().psutil.Process(self.process.pid).create_time())
        if self._identity is not None and actual != self._identity:
            raise ValueError('resource launcher identity changed')
        return actual

    def _observe(self):
        try:
            _sampler_tool().sample(self._identity[0], self.trace_path, seconds=self.seconds,
                            interval=self.interval, stop=self._stop.is_set,
                            discovery_interval=self.discovery_interval)
        except BaseException as error:
            self._failure = error
        finally:
            self._done.set()

    def start(self):
        if os.name != 'nt':
            raise RuntimeError('workflow resource recording requires Windows private bytes')
        if self._thread is not None or self._report is not None:
            raise ValueError('workflow resource recording already started')
        self._identity = self._verify_live_identity()
        self.private_dir.mkdir(parents=True, exist_ok=False)
        self._capture_clock('before-go')
        self._thread = threading.Thread(target=self._observe, name='termforge-workflow-resource-sampler', daemon=False)
        self._thread.start()
        deadline = time.monotonic() + 5
        while True:
            if self._done.is_set():
                raise RuntimeError('resource sampler ended before launcher GO') from self._failure
            if self.trace_path.exists():
                with self.trace_path.open('rb') as stream:
                    prefix = stream.read(65536)
                rows = [json.loads(line) for line in prefix.splitlines(keepends=True) if line.endswith(b'\n')]
                if rows:
                    metadata = rows[0]
                    if (metadata.get('root_pid'), metadata.get('root_create_time')) != self._identity:
                        raise ValueError('resource sampler root identity mismatch')
                    if any(p['group'] == 'root' and p['status'] == 'observed'
                           for row in rows if row.get('type') == 'sample' for p in row['processes']):
                        self._verify_live_identity()
                        self._clock_zero = metadata['start_counter_ns']
                        self._identity_verified = True
                        return
            if time.monotonic() >= deadline:
                raise TimeoutError('resource sampler first observed launcher sample deadline')
            time.sleep(.01)

    def mark(self, label):
        if not self._identity_verified or self._report is not None:
            raise ValueError('resource recording is not active')
        if label not in MARKS or len(self._marks) >= 16 or any(row['label'] == label for row in self._marks):
            raise ValueError('invalid duplicate or excessive resource mark')
        self._marks.append({'label': label, 'elapsed_ns': time.perf_counter_ns() - self._clock_zero,
                            'source': 'python-runner-observation'})

    def finish(self, native_phases=None, *, observations_path=None):
        if self._report is not None:
            if native_phases is not None or (observations_path is not None and self._report.get('observations') is None):
                raise ValueError('resource recording is already finalized without the requested extra evidence')
            return copy.deepcopy(self._report)
        finished_before_stop = self._done.is_set()
        self._stop.set()
        if self._thread is None:
            self._report = {'schema': 'termforge-workflow-resources-v1', 'status': 'not-started', 'scope': 'launcher-tree',
                            'root_identity_verified': False, 'sampler_started':False, 'sampler_joined':False,
                            'trace': None, 'launcher_tree': None, 'phase_windows': None, 'performance_gate': 'not_evaluated'}
            return copy.deepcopy(self._report)
        self._thread.join(timeout=5)
        if self._thread.is_alive():
            raise TimeoutError('resource sampler thread did not join')
        if self._failure is not None:
            raise RuntimeError('resource sampler failed') from self._failure
        self._capture_clock('after-cleanup')
        trace = _summarizer.summarize(self.trace_path)
        with self.trace_path.open(encoding='utf-8') as stream:
            metadata = json.loads(stream.readline())
        if (metadata['root_pid'], metadata['root_create_time']) != self._identity:
            raise ValueError('resource trace root identity mismatch')
        tree, handles = _tree_metrics(self.trace_path, trace['source_sha256'])
        cpu = [trace['groups'][group]['observed_cpu_seconds'] for group in ('root', 'descendant')]
        tree['observed_cpu_seconds'] = sum(value for value in cpu if value is not None) if any(value is not None for value in cpu) else None
        tree['observed_cpu_core_equivalents'] = (tree['observed_cpu_seconds'] / (trace['sampling_span_ns'] / 1e9)
                                                if tree['observed_cpu_seconds'] is not None and trace['sampling_span_ns'] > 0 else None)
        observations = None
        if observations_path is not None:
            if Path(observations_path).name != 'resource-observations.jsonl':
                raise ValueError('public resource observation filename must be fixed')
            derived = write_resource_observations(self.trace_path,observations_path,expected_identity=self._identity)
            for metric in ('private_bytes','working_set_bytes','handles'):
                key = 'sampled_peak_observed_'+metric
                if derived['launcher_tree'][key] != tree[key]:
                    raise ValueError('private/public resource peak differs')
            if derived['sample_count'] != trace['sample_count'] or derived['quality'] != trace['quality']:
                raise ValueError('private/public resource quality differs')
            for group in ('root','descendant','sampler'):
                for key,value in trace['groups'][group].items():
                    actual = derived['groups'][group][key]
                    equal = actual == value if type(value) not in (int,float) or actual is None else math.isclose(actual,value,rel_tol=1e-9,abs_tol=1e-9)
                    if not equal:
                        raise ValueError('private/public resource aggregate differs')
            observations = {'file':'resource-observations.jsonl','summary':derived}
        version = metadata.get('psutil_version')
        if type(version) is not str or re.fullmatch(r'\d+(?:\.\d+){1,3}',version) is None:
            version = None
        phase_windows = None
        if native_phases is not None:
            from experiments.shell.workflow_resource_phases import relative_calibration, summarize_resource_phases, SCHEMA
            try:
                if self._clock_error or self._clock_before is None or self._clock_after is None:
                    raise ValueError('clock-calibration-unavailable')
                calibration = relative_calibration(self._clock_before,self._clock_after,self._clock_zero)
                phase_windows = summarize_resource_phases(observations_path,native_phases,calibration=calibration,marks=self._marks)
            except (ValueError,KeyError,TypeError):
                phase_windows = {'schema':SCHEMA,'status':'unknown','reason':'clock-calibration-unavailable',
                                 'calibration':None,'markers':None,'windows':None,'performance_gate':'not_evaluated'}
        self._report = {'schema': 'termforge-workflow-resources-v1', 'status': 'recorded', 'scope': 'launcher-tree',
                        'root_role': 'actual-launcher-not-ui', 'root_identity_verified': self._identity_verified,
                        'sampler_started':True, 'sampler_joined': True, 'sampling_finished_before_stop_request':finished_before_stop,
                        'performance_gate': 'not_evaluated', 'trace': trace,
                        'launcher_tree': tree, 'group_sampled_peak_observed_handles': handles,
                        'marks': list(self._marks), 'phase_windows': phase_windows, 'observations':observations,
                        'dependencies':{'psutil_version':version,
                                        'sampler_sha256':hashlib.sha256((ROOT/'benchmarks/tools/sample.py').read_bytes()).hexdigest(),
                                        'summarizer_sha256':hashlib.sha256((ROOT/'benchmarks/tools/summarize_resources.py').read_bytes()).hexdigest()},
                        'limitations': ['Native phase windows need compatible measured pre/post clock brackets; absent or incompatible mapping remains unknown.',
                                        'Launcher-tree includes UI, console hosts, shells, producers and helpers; it is not UI-only cost.',
                                        'CPU starts at first observation and can miss final CPU; polling can miss short-lived descendants.',
                                        'Peaks sum only observed rows in one sample; unavailable rows are unknown, working sets can share pages.',
                                        'Raw sampling data and process identities are private; a valid trace is not a performance pass.']}
        return copy.deepcopy(self._report)
