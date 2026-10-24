"""Bounded QPC/perf-counter evidence mapping; no Windows APIs run on import.

Python's reference epoch is unspecified. Measure the relation rather than
assuming it: https://docs.python.org/3/library/time.html#time.perf_counter_ns
QPC frequency is fixed per boot; cross-thread ordering has +/- one tick:
https://learn.microsoft.com/windows/win32/sysinfo/acquiring-high-resolution-time-stamps
"""
import hashlib
import json
import math
from pathlib import Path
import time

SCHEMA='termforge-workflow-resource-phase-summary-v1'
NANO=1_000_000_000


class PhaseUnavailable(ValueError):
    """Only fixed, non-private reason codes leave the evidence boundary."""


def _require(condition, reason):
    if not condition:raise PhaseUnavailable(reason)


def _integer(value, minimum=None):
    return type(value) is int and (minimum is None or value>=minimum)


def _ceil_div(value,divisor):return -(-value//divisor)


def capture_clock_brackets(batch):
    """Four actual Windows QPC reads, each enclosed by Python clock reads."""
    import os
    _require(os.name=='nt','clock-platform-unavailable')
    import ctypes
    info=time.get_clock_info('perf_counter')
    _require(info.implementation=='QueryPerformanceCounter()' and info.monotonic and not info.adjustable,
             'clock-implementation-incompatible')
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    qpc,qpf=kernel.QueryPerformanceCounter,kernel.QueryPerformanceFrequency
    qpc.argtypes=qpf.argtypes=[ctypes.POINTER(ctypes.c_longlong)];qpc.restype=qpf.restype=ctypes.c_int
    frequency=ctypes.c_longlong()
    _require(qpf(ctypes.byref(frequency)) and frequency.value>0,'clock-frequency-unavailable')
    points=[]
    for _ in range(4):
        value=ctypes.c_longlong();before=time.perf_counter_ns();ok=qpc(ctypes.byref(value));after=time.perf_counter_ns()
        _require(ok and value.value>0,'clock-counter-unavailable')
        points.append(dict(batch=batch,perf_before_ns=before,perf_after_ns=after,
                           qpc_counter=value.value,qpc_frequency=frequency.value))
    return dict(implementation=info.implementation,monotonic=info.monotonic,adjustable=info.adjustable,
                resolution_ns=math.ceil(info.resolution*NANO),points=points)


def relative_calibration(before,after,origin):
    _require(_integer(origin,0),'clock-origin-invalid')
    _require(all(before[k]==after[k] for k in ('implementation','monotonic','adjustable','resolution_ns')),
             'clock-properties-changed')
    result={k:before[k] for k in ('implementation','monotonic','adjustable','resolution_ns')}
    result['points']=[dict(p,perf_before_ns=p['perf_before_ns']-origin,perf_after_ns=p['perf_after_ns']-origin)
                      for p in before['points']+after['points']]
    return result


def _calibration(data):
    fields={'implementation','monotonic','adjustable','resolution_ns','points'}
    _require(type(data) is dict and set(data) in (fields,fields|{'offset_bounds_ns','cross_process_tick_ns'}),'clock-record-invalid')
    _require(data['implementation']=='QueryPerformanceCounter()' and data['monotonic'] is True and data['adjustable'] is False,
             'clock-implementation-incompatible')
    resolution=data['resolution_ns']
    _require(_integer(resolution,1) and resolution<=NANO,'clock-resolution-invalid')
    points=data['points'];_require(type(points) is list and len(points)==8,'clock-bracket-count')
    low=high=frequency=previous_perf=previous_qpc=None
    for index,p in enumerate(points):
        _require(type(p) is dict and set(p)=={'batch','perf_before_ns','perf_after_ns','qpc_counter','qpc_frequency'},'clock-point-invalid')
        _require(p['batch']==('before-go' if index<4 else 'after-cleanup') and
                 all(_integer(p[k]) for k in ('perf_before_ns','perf_after_ns')) and
                 all(_integer(p[k],1) for k in ('qpc_counter','qpc_frequency')) and p['perf_before_ns']<=p['perf_after_ns'],
                 'clock-point-invalid')
        _require(previous_perf is None or (p['perf_before_ns']>=previous_perf and p['qpc_counter']>=previous_qpc),'clock-point-order')
        frequency=p['qpc_frequency'] if frequency is None else frequency
        _require(p['qpc_frequency']==frequency,'clock-frequency-mismatch')
        floor=p['qpc_counter']*NANO//frequency;ceil=_ceil_div(p['qpc_counter']*NANO,frequency)
        a=p['perf_before_ns']-ceil-resolution;b=p['perf_after_ns']-floor+resolution
        low=a if low is None else max(low,a);high=b if high is None else min(high,b)
        previous_perf,previous_qpc=p['perf_after_ns'],p['qpc_counter']
    tick=_ceil_div(NANO,frequency)
    if 'offset_bounds_ns' in data:
        _require(type(data['offset_bounds_ns']) is list and len(data['offset_bounds_ns'])==2 and
                 all(_integer(value) for value in data['offset_bounds_ns']) and _integer(data['cross_process_tick_ns'],1) and
                 data['offset_bounds_ns']==[low,high] and data['cross_process_tick_ns']==tick,'clock-derived-evidence-mismatch')
    return dict(data,offset_bounds_ns=[low,high],cross_process_tick_ns=tick)


def _markers(path,calibration):
    try:
        with Path(path).open('rb') as stream:raw=stream.read(65537)
    except OSError:raise PhaseUnavailable('native-markers-unavailable') from None
    _require(len(raw)<=65536 and raw.endswith(b'\n'),'native-marker-budget-or-truncation')
    lines=raw.splitlines();_require(len(lines)<=32,'native-marker-budget-or-truncation')
    expected=[('eight-ready',0,8,8),('load-started',0,8,8),('background-complete',0,8,8)]
    for cycle in range(1,6):expected.extend([('cycle-six',cycle,6,6+cycle*2),('cycle-six-end',cycle,6,6+cycle*2),('cycle-eight',cycle,8,8+cycle*2)])
    expected.append(('after-paste-close',5,7,18));_require(len(lines)==len(expected),'native-marker-count')
    points=calibration['points'];frequency=points[0]['qpc_frequency'];lo,hi=calibration['offset_bounds_ns'];tick=calibration['cross_process_tick_ns']
    rows=[];previous=None
    for seq,(line,want) in enumerate(zip(lines,expected),1):
        try:row=json.loads(line)
        except (ValueError,UnicodeError):raise PhaseUnavailable('native-marker-json-invalid') from None
        _require(type(row) is dict and set(row)=={'schema','seq','phase','cycle','qpc_counter','qpc_frequency','live_sessions','window_creations'},'native-marker-fields')
        _require(row['schema']=='termforge-workflow-resource-phase-v1' and row['seq']==seq and
                 all(_integer(row[k],0) for k in ('seq','cycle','qpc_counter','qpc_frequency','live_sessions','window_creations')) and
                 (row['phase'],row['cycle'],row['live_sessions'],row['window_creations'])==want,'native-marker-contract')
        q=row['qpc_counter']
        _require(row['qpc_frequency']==frequency,'native-frequency-mismatch')
        _require(points[0]['qpc_counter']<=q<=points[-1]['qpc_counter'],'native-outside-calibration-coverage')
        _require(previous is None or q>previous,'native-marker-order');previous=q
        rows.append(dict(row,elapsed_bounds_ns=[q*NANO//frequency+lo-tick,_ceil_div(q*NANO,frequency)+hi+tick]))
    return rows,hashlib.sha256(raw).hexdigest()


def _window(begin,end,rows,kind,cycle,resolution):
    start=begin['elapsed_bounds_ns'];stop=end['elapsed_bounds_ns']
    lower,upper=start[1],stop[0]
    base=dict(name=begin['phase']+'-to-'+end['phase'],kind=kind,cycle=cycle,
              start_bounds_ns=start,end_bounds_ns=stop,conservative_inside_ns=[lower,upper],
              duration_bounds_ns=[stop[0]-start[1],stop[1]-start[0]],collection_endpoint_uncertainty_ns=resolution)
    if lower>=upper:return dict(base,status='unknown',reason='phase-boundary-uncertainty-overlap',sample_count=0,groups=None,launcher_tree=None)
    admitted=[r for r in rows if r['elapsed_ns']-resolution>=lower and r['elapsed_ns']+r['collection_ns']+resolution<=upper]
    metrics=('private_bytes','working_set_bytes','handles')
    groups={name:dict(coverage='observed-only',observed_accounted_cpu_seconds=None,cpu_pairs=0,observed_samples=0,unavailable_rows=0,
                     **{'sampled_peak_observed_'+metric:None for metric in metrics}) for name in ('root','descendant','sampler','launcher_tree')}
    previous={};previous_seq=None
    for row in admitted:
        current={};sets={name:[] for name in groups}
        for p in row['processes']:
            names=[p['group']]+(['launcher_tree'] if p['group'] in ('root','descendant') else [])
            if p['status']!='observed':
                for name in names:groups[name]['unavailable_rows']+=1
                continue
            current[p['process_identity']]=p
            for name in names:sets[name].append(p)
            old=previous.get(p['process_identity']) if previous_seq is not None and row['seq']==previous_seq+1 else None
            if old is not None:
                delta=p['cpu_seconds_lifetime']-old['cpu_seconds_lifetime']
                for name in names:
                    group=groups[name];group['cpu_pairs']+=1
                    group['observed_accounted_cpu_seconds']=(group['observed_accounted_cpu_seconds'] or 0)+delta
        for name,ps in sets.items():
            if ps:
                groups[name]['observed_samples']+=1
                for metric in metrics:
                    key='sampled_peak_observed_'+metric;total=sum(p[metric] for p in ps)
                    groups[name][key]=total if groups[name][key] is None else max(groups[name][key],total)
        previous=current;previous_seq=row['seq']
    tree=groups.pop('launcher_tree')
    return dict(base,status='observed' if admitted else 'unknown',reason=None if admitted else 'no-whole-collection-sample',
                sample_count=len(admitted),sample_sequences=[r['seq'] for r in admitted],groups=groups,launcher_tree=tree,
                discovery_errors=sum(len(r['errors']) for r in admitted))


def summarize_resource_phases(observations_path,native_phases_path,*,calibration,marks):
    """Pure offline reconstruction using only public observations and clocks."""
    checked=None
    try:
        checked=_calibration(calibration)
        _require(checked['offset_bounds_ns'][0]<=checked['offset_bounds_ns'][1],'clock-offset-intersection-empty')
        markers,native_digest=_markers(native_phases_path,checked)
        _require(type(marks) is list and len(marks)<=16,'python-mark-budget')
        go=[m for m in marks if type(m) is dict and m.get('label')=='launcher-go']
        _require(len(go)==1 and _integer(go[0].get('elapsed_ns'),0) and go[0].get('source')=='python-runner-observation','python-go-observation-missing')
        _require(observations_path is not None,'public-observations-required')
        from experiments.shell.workflow_resources import summarize_resource_observations
        path=Path(observations_path)
        try:
            _require(path.stat().st_size<=64*1024*1024,'observations-budget')
            summary=summarize_resource_observations(path)
            _require(summary['sample_count']<=8192,'observations-budget')
            with path.open('rb') as stream:raw=stream.read(64*1024*1024+1)
            _require(len(raw)<=64*1024*1024,'observations-budget')
            _require(hashlib.sha256(raw).hexdigest()==summary['observations_sha256'],'observations-changed-after-validation')
            rows=[json.loads(line) for line in raw.splitlines()]
        except (OSError,ValueError,UnicodeError) as error:
            if isinstance(error,PhaseUnavailable):raise
            raise PhaseUnavailable('observations-invalid') from None
        samples=[r for r in rows if r['type']=='sample']
        resolution=checked['resolution_ns'];go_point=go[0]['elapsed_ns']
        begin=dict(phase='python-runner-go-observation',elapsed_bounds_ns=[go_point-resolution,go_point+resolution])
        windows=[_window(begin,markers[0],samples,'initialization-observation',0,resolution)]
        for before,after in zip(markers,markers[1:]):
            kind=('six-resident' if before['phase']=='cycle-six' and after['phase']=='cycle-six-end' else
                  'creation' if before['phase']=='cycle-six-end' and after['phase']=='cycle-eight' else
                  'load' if before['phase']=='load-started' else 'transition')
            windows.append(_window(before,after,samples,kind,before['cycle'],resolution))
        trend=[]
        for window in windows:
            if window['kind']=='six-resident':trend.append(dict(cycle=window['cycle'],sample_count=window['sample_count'],launcher_tree=window['launcher_tree']))
        return dict(schema=SCHEMA,status='mapped',reason=None,calibration=checked,markers=markers,windows=windows,six_resident_observations=trend,
                    native_markers_sha256=native_digest,observations_sha256=summary['observations_sha256'],performance_gate='not_evaluated',
                    sampler_scope='entire-python-observer-process',
                    limitations=['Python runner GO observation is not the exact native startup endpoint.',
                                 'Only whole collection intervals inside conservative phase boundaries are admitted.',
                                 'CPU is observed accounted delta between adjacent observed identities in one phase; exit tails and OS accounting granularity remain unknown.',
                                 'Per-sample memory and handles are serial-query observed aggregates, not an instantaneous concurrent peak.',
                                 'Missing processes/discovery and short-lived descendants remain incomplete coverage; sampler is the entire Python observer process.',
                                 'Resident labels validate native counts/no creation; identity stability also requires the independent E2 session evidence.',
                                 'Five short resident windows do not prove absence of leaks or establish a performance pass.'])
    except PhaseUnavailable as error:
        return dict(schema=SCHEMA,status='unknown',reason=str(error),calibration=checked,markers=None,windows=None,performance_gate='not_evaluated')
