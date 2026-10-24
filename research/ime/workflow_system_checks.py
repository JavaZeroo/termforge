"""Independent fixed E3 content contract; pure bytes are not system-input evidence.

The literals do not import the producer, selection serializer or paste preparer.
Companion evidence validation is added only after its native schema is frozen.
"""
import hashlib
import re
import json
import struct
import math
from pathlib import Path
from research.ime.product_checks import _anchored, _candidates, _rect

COPY_TEXT = 'TF_R108_A_0110 中文 e\u0301\r\nTF_R108_A_0111 中文 e\u0301'
PASTE_PREFIX = 'TF_R108_A_0110 中文 e\u0301\rTF_R108_A_0111 中文 e\u0301'.encode('utf-8')
COMMITTED_INPUT = PASTE_PREFIX + '你好'.encode('utf-8')
FINAL_INPUT = COMMITTED_INPUT + b'!'
COPY_UTF16_UNITS = 42
SELECTION_END_COLUMN = 21
SYSTEM_PHASES = ('selected', 'copied', 'paste-requested', 'paste-confirmation', 'paste-complete',
                 'commit-preedit', 'committed', 'cancel-preedit', 'cancelled',
                 'large-cancel-requested', 'large-cancel-ready', 'large-close-requested', 'large-close-ready')


def _require(value, reason):
    if not value:
        raise ValueError(reason)


def _uint(value, reason, maximum=2**63-1):
    _require(type(value) is int and 0 <= value <= maximum, reason)
    return value


def _system_runtime(runtime, role):
    """Apply fixed queue/cache bounds even between the main E2 checkpoints."""
    from experiments.shell.workflow_sessions_checks import _cache_bounds
    _cache_bounds({'search':runtime.get('search')})
    for queue,limit in (('input',16384),('output',262144)):
        value = runtime.get(queue,{})
        _require(_uint(value.get('queued_bytes'),'system queue bytes') <= limit and
                 _uint(value.get('high_water_bytes'),'system queue high water') <= limit, 'system queue capacity')
    incoming = runtime['input']
    _require(_uint(incoming.get('in_flight_bytes'),'system input in flight') <= 1024 and
             incoming.get('failed') is False and incoming.get('rejected') == 0 and
             runtime.get('resize',{}).get('failed') is False, 'system input/resize error')
    _require(_uint(runtime.get('history_rows'),'system history') <= 100 and runtime.get('history_limit') == 100 and
             1 <= _uint(runtime.get('rows'),'system rows') <= 200 and
             _uint(runtime.get('retained_rows'),'system retained rows') <= 100+runtime['rows'], 'system history bound')
    for field in ('paste','last_terminal_paste'):
        paste = runtime.get(field)
        if paste is None: continue
        _require(role in ('B','C') and type(paste) is dict and paste.get('target') == runtime['session_id'],
                 'system paste wrong runtime target')
        total,units = (51,42) if role == 'B' else (3000000,1000000)
        sent = _uint(paste.get('sent_bytes'),'system paste sent')
        accepted = _uint(paste.get('accepted_bytes'),'system paste accepted')
        flight = _uint(paste.get('in_flight_bytes'),'system paste in flight')
        capacity = _uint(paste.get('retained_capacity_bytes'),'system paste capacity')
        _require(paste.get('total_bytes') == total and paste.get('source_units') == units and
                 sent <= accepted <= total and type(paste.get('error_present')) is bool, 'system paste counters/source')
        if field == 'paste':
            _require(paste.get('state') in ('ready','sending') and paste['error_present'] is False and
                     accepted-sent <= flight <= 1024 and total <= capacity <= 4*1024*1024+32,
                     'system active paste allocation/error')
        else:
            _require(flight == 0 and capacity <= 32 and paste.get('state') in ('completed','cancelled','failed') and
                     (paste['state']=='failed' or paste['error_present'] is False), 'system terminal paste allocation/error')
            if role == 'B':
                _require(paste['state']=='completed' and sent == accepted == 51, 'system small paste terminal prefix')


def validate_workflow_ime(row, phase, generation):
    """Require native composition/candidate observations, not a success flag."""
    _require(phase in ('commit-preedit','committed','cancel-preedit','cancelled') and
             type(row) is dict and row.get('name') == phase and row.get('session') == 'B' and
             type(row.get('generation')) is int and row['generation'] == generation,
             'system IME checkpoint target/phase/generation')
    preedit = phase.endswith('preedit')
    committed = phase != 'commit-preedit'
    expected = COMMITTED_INPUT if committed else PASTE_PREFIX
    _require(row.get('received_hex') == expected.hex() and row.get('committed') == ('你好' if committed else '') and
             type(row.get('result_events')) is int and row['result_events'] == int(committed) and
             row.get('composing') is preedit and type(row.get('preedit')) is str and
             bool(row['preedit']) is preedit and len(row['preedit']) <= 4096 and
             row.get('ime_error') == '' and row.get('character_units') == [], 'system IME commit/preedit/error bytes')
    _require(1 <= _uint(row.get('composition_events'), 'composition events', 4096) and
             1 <= _uint(row.get('caret_updates'), 'caret updates', 1000000) and
             row.get('caret_positioned') is True and _rect(row.get('caret_client')) and
             _rect(row.get('caret_screen')), 'system actual native caret/composition missing')
    client, screen = row['caret_client'], row['caret_screen']
    size = [client[2]-client[0], client[3]-client[1]]
    _require(size == [screen[2]-screen[0], screen[3]-screen[1]] == row.get('cell_pixels') and
             all(type(n) is int and 1 <= n <= 512 for n in size) and
             48 <= _uint(row.get('display_dpi'), 'display DPI', 768) and
             50 <= _uint(row.get('zoom_percent'), 'zoom', 200), 'system measured caret cell geometry')
    candidates = _candidates(row.get('candidates'))
    if preedit:
        _require(bool(candidates), 'system actual preedit candidate missing')
        _anchored(row)
    else:
        _require(not candidates, 'system commit/cancel retained visible candidate')


def validate_workflow_system_manifest(data, sessions):
    """Validate companion observations against independent literals and sessions.

    The caller must separately verify files, native actions/progress, host guard,
    process identity, capture bytes and cleanup; this is a pure manifest check.
    """
    _require(type(data) is dict and data.get('schema') == 'termforge-workflow-system-v1' and
             data.get('mode') == 'ui-workflow-system', 'system companion identity')
    _require(data.get('data_only') is False and data.get('system_clipboard') is True and
             data.get('real_ime') is True and type(data.get('shortcut_batches')) is int and
             data['shortcut_batches'] == 4, 'system companion input contract flags')
    for field,expected in (('expected_copy_utf8_hex',COPY_TEXT.encode().hex()),
                           ('expected_prefix_utf8_hex',PASTE_PREFIX.hex()),
                           ('expected_committed_utf8_hex',COMMITTED_INPUT.hex()),
                           ('expected_final_utf8_hex',FINAL_INPUT.hex())):
        _require(data.get(field) == expected, 'declared system literal differs from independent contract')
    _require(sessions.get('data_only') is False and sessions.get('system_clipboard') is True and
             sessions.get('ime') is True, 'system evidence cannot impersonate data-only')
    desktop = data.get('input_desktop', {})
    _require(desktop.get('station') == 'WinSta0' and desktop.get('query_only') is True and
             type(desktop.get('input_name')) is str and 0 < len(desktop['input_name']) < 256 and
             desktop.get('thread_name') == desktop['input_name'], 'actual shared input desktop missing')
    provider = {'clsid':'{81D4E9C9-1D3B-41BC-9E6C-4B40BF79E35E}',
                'profile':'{FA550B04-5AD7-411F-A5AC-CA038EC515D7}',
                'langid':2052,'language_hresult':0,'activation_hresult':0}
    _require(data.get('provider') == provider and all(type(data['provider'][k]) is int for k in
             ('langid','language_hresult','activation_hresult')), 'actual fixed Pinyin provider/HRESULT')
    _require(_uint(data.get('key_batches'),'key batches') == 9 and
             _uint(data.get('guarded_key_batches'),'guarded batches') == 9, 'actual guarded input batch count')
    points = data.get('snapshots')
    _require(type(points) is list and len(points) == 13 and [p.get('phase') for p in points] == list(SYSTEM_PHASES),
             'system snapshot count/order')
    workspace = data.get('workspace_window_token')
    _require(type(workspace) is str and re.fullmatch(r'[1-9][0-9]*',workspace), 'actual workspace HWND missing')
    by_phase = {(p['phase'],p['cycle']):{s['name']:s for s in p['slots']} for p in sessions['checkpoints']}
    initial, latest = by_phase[('eight-ready',0)], by_phase[('cycle-eight',5)]
    known_roles = {'A','B','C','F1','F2','F3','F4','F5'}
    elapsed = -1; observations = 0; ime_rows = []; selected_range = None; clipboard_sequence = None
    for index, point in enumerate(points):
        phase = SYSTEM_PHASES[index]
        _require(_uint(point.get('seq'),'system sequence') == index+1 and
                 _uint(point.get('elapsed_ms'),'system elapsed') >= elapsed, 'system sequence/clock')
        _require(type(point.get('active_sessions')) is int and point['active_sessions'] == 8,
                 'system snapshot not taken with eight live sessions')
        elapsed = point['elapsed_ms']
        expected_batches = (0,0,0,0,0,5,6,8,9,9,9,9,9)[index]
        _require(type(point.get('key_batches')) is int and point['key_batches'] == expected_batches,
                 'system checkpoint key action timing')
        targets = point.get('targets')
        _require(type(targets) is list and len(targets) == 8 and
                 {t.get('name') for t in targets} == known_roles, 'system actual eight targets missing')
        identities = latest if phase.startswith('large-close') else initial
        expected_b = b'' if index < 4 else PASTE_PREFIX if index < 6 else COMMITTED_INPUT
        for target in targets:
            role = target['name']; identity = identities[role]
            _require(all(type(target.get(k)) is type(identity[k]) and target[k] == identity[k] for k in
                         ('session_id','generation','window_creation_serial','window_token')),
                     'system stale or wrong target identity')
            _require(target.get('received_hex') == (expected_b.hex() if role == 'B' else ''),
                     'system intermediate bytes/preedit/target isolation')
        for role in ('A','B') + (('C',) if phase.startswith('large-') else ()):
            obj = point.get(role)
            _require(type(obj) is dict and type(obj.get('runtime')) is dict, 'system actual runtime absent')
            runtime = obj['runtime']
            _require(runtime.get('schema') == 'termforge-runtime-snapshot-v1' and
                     all(type(runtime.get(k)) is int and runtime[k] == identities[role][k]
                         for k in ('session_id','window_creation_serial')), 'system runtime identity mismatch')
            _system_runtime(runtime,role)
        observations += 1 + len(targets) + sum(type(point.get(role)) is dict and type(point[role].get('runtime')) is dict
                                                for role in ('A','B','C'))
        observations += sum(point.get(field) is not None for field in ('clipboard','prompt','ime'))
        if index < 2:
            a = point['A']; selection = a['runtime'].get('selection',{}).get('range')
            _require(a.get('selected_text_hex') == COPY_TEXT.encode().hex() and a.get('selection_status') == 'ok' and
                     type(selection) is dict and type(selection.get('begin')) is dict and type(selection.get('end')) is dict,
                     'actual selected two-line text/range missing')
            first = by_phase[('find-complete',0)]['A']['current_find']
            begin, end = selection['begin'], selection['end']
            _require(begin.get('line_id') == first['line_id'] and begin.get('version') == first['version'] and
                     begin.get('column') == 0 and end.get('column') == 21 and
                     type(end.get('line_id')) is int and end['line_id'] > begin['line_id'] and
                     type(end.get('version')) is int and end['version'] > 0, 'selection endpoint/version mismatch')
            lines = by_phase[('find-complete',0)]['A']['visible_lines']
            next_line = next((line for line in lines if line['id'] == end['line_id']), None)
            _require(next_line is not None and next_line['version'] == end['version'] and
                     next_line['text'] == 'TF_R108_A_0111 中文 e\u0301', 'selection second line is not actual 0111')
            if selected_range is None: selected_range = selection
            _require(selection == selected_range, 'Copy changed selection identity')
        clip = point.get('clipboard')
        if index < 2:
            _require(type(clip) is dict, 'actual sentinel/Copy clipboard absent')
        if clip is not None:
            text = 'TF_R109_COPY_SENTINEL' if index == 0 else COPY_TEXT
            _require(clip.get('utf16_hex') == text.encode('utf-16-le').hex() and
                     clip.get('text_utf8_hex') == text.encode().hex() and
                     type(clip.get('owner_window_token')) is str and
                     re.fullmatch(r'[1-9][0-9]*',clip['owner_window_token']) is not None,
                     'actual clipboard differs from literal')
            sequence = _uint(clip.get('sequence'),'clipboard sequence',2**32-1)
            _require(sequence > 0 and clip['owner_window_token'] == (workspace if index == 0 else initial['A']['window_token']),
                     'actual Copy/sentinel ownership mismatch')
            if index == 0: clipboard_sequence = sequence
            if index == 1: _require(sequence != clipboard_sequence, 'Copy left sentinel sequence unchanged')
        if phase in ('paste-requested','large-cancel-requested','large-close-requested'):
            role = 'B' if phase == 'paste-requested' else 'C'
            worker = point[role]['runtime'].get('clipboard')
            _require(type(worker) is dict and worker.get('state') in ('running','completed') and
                     worker.get('error_present') is False and worker.get('native_error') == 0,
                     'actual requested clipboard worker absent/failed')
        if phase in ('paste-confirmation','large-cancel-ready','large-close-ready'):
            role = 'B' if phase == 'paste-confirmation' else 'C'
            worker = point[role]['runtime'].get('last_terminal_clipboard')
            _require(type(worker) is dict and worker.get('state') == 'completed' and
                     worker.get('input_units') == (42 if role == 'B' else 1000000) and
                     worker.get('cleanup_complete') is True and worker.get('error_present') is False and
                     worker.get('native_error') == 0, 'clipboard completion/cleanup/input units')
        prompt = point.get('prompt')
        if phase == 'paste-confirmation':
            _require(type(prompt) is dict and prompt.get('present') is True and prompt.get('visible') is True and
                     prompt.get('confirm_button') is True and prompt.get('owner_window_token') == workspace and
                     prompt.get('target_window_token') == initial['B']['window_token'] and
                     prompt.get('target_session') == initial['B']['session_id'] and
                     prompt.get('title') == f"Paste to session {initial['B']['session_id']}" and
                     point['B']['runtime'].get('pending_prepared_paste') is True and
                     point['B']['runtime'].get('paste_prompt_present') is True and
                     type(prompt.get('window_token')) is str and re.fullmatch(r'[1-9][0-9]*',prompt['window_token']),
                     'actual owned paste confirmation missing')
        if phase == 'paste-complete':
            _require(prompt is None or prompt.get('present') is False, 'paste confirmation survived acceptance')
            _require(point['B']['runtime'].get('visible') is False and point['A']['runtime'].get('visible') is True,
                     'paste completion did not preserve hidden target')
            from experiments.shell.workflow_sessions_checks import _cache_bounds
            _cache_bounds(point['B']['runtime'],system_receiver=True)
            _require(point['B']['runtime'].get('last_terminal_paste') is not None, 'actual completed small paste missing')
        if phase in ('commit-preedit','committed','cancel-preedit','cancelled'):
            row = point.get('ime'); validate_workflow_ime(row,phase,initial['B']['generation']); ime_rows.append(row)
        else:
            _require(point.get('ime') is None, 'unexpected IME checkpoint phase')
    for before, after in zip(ime_rows,ime_rows[1:]):
        _require(all(after[key] >= before[key] for key in ('composition_events','result_events','caret_updates')),
                 'IME cumulative observation regressed')
    _require(ime_rows[2]['composition_events'] > ime_rows[1]['composition_events'], 'NI cancel lacks new composition')
    observations += 15  # Nine real key batches, four shortcuts, confirmation and completed journal rows.
    _require(data.get('observations') == observations == sessions.get('system_observations') and
             sessions.get('base_observations') == 235 and sessions.get('operation_observations') == 2 and
             data.get('combined_observations') == sessions.get('observations') == observations+235+2 <= 512,
             'independent system observation total mismatch')
    return {'snapshots':13,'companion_observations':observations,'combined_observations':observations+237,
            'manifest_verified':True,'complete_workflow_verified':False}


def validate_workflow_system_progress(rows, data, sessions):
    """Replay the action/checkpoint order, including exact snapshot journal copies."""
    order = [('checkpoint','selected'), ('shortcut','CtrlShiftC'), ('checkpoint','copied'),
             ('shortcut','CtrlShiftV'), ('checkpoint','paste-requested'), ('checkpoint','paste-confirmation'),
             ('confirm',None), ('checkpoint','paste-complete')]
    order += [('ime-key',key) for key in ('N','I','H','A','O')]
    order += [('checkpoint','commit-preedit'),('ime-key','SPACE'),('checkpoint','committed'),
              ('ime-key','N'),('ime-key','I'),('checkpoint','cancel-preedit'),('ime-key','ESC'),
              ('checkpoint','cancelled'),('shortcut','CtrlShiftV'),('checkpoint','large-cancel-requested'),
              ('checkpoint','large-cancel-ready'),('shortcut','CtrlShiftV'),('checkpoint','large-close-requested'),
              ('checkpoint','large-close-ready'),('completed',None)]
    _require(type(rows) is list and len(rows) == len(order) <= 64, 'system progress action count')
    initial = {slot['name']:slot for slot in sessions['checkpoints'][0]['slots']}
    by_phase = {point['phase']:point for point in data['snapshots']}
    previous = -1; shortcut_index = 0
    for index,(row,(kind,code)) in enumerate(zip(rows,order),1):
        _require(type(row) is dict and row.get('schema') == 'termforge-workflow-system-progress-v1' and
                 _uint(row.get('seq'),'progress sequence') == index and row.get('kind') == kind and
                 _uint(row.get('elapsed_ms'),'progress elapsed') >= previous, 'system progress order/clock/identity')
        previous = row['elapsed_ms']
        if kind == 'checkpoint':
            _require(row.get('snapshot') == by_phase[code] and previous >= by_phase[code]['elapsed_ms'],
                     'journal checkpoint differs from companion')
            continue
        if kind == 'completed':
            _require(row.get('checkpoints') == 13 and row.get('key_batches') == 9 and row.get('shortcuts') == 4,
                     'system completed journal count')
            continue
        role = 'B'
        if kind == 'shortcut':
            role = ('A','B','C','C')[shortcut_index]; shortcut_index += 1
            _require(row.get('code') == code and row.get('source') == 'native-WM_KEYDOWN', 'actual native shortcut missing')
        elif kind == 'ime-key':
            _require(row.get('code') == code and row.get('guarded') is True and
                     row.get('actual_generation') == initial['B']['generation'], 'unguarded/stale real IME action')
        else:
            prompt = by_phase['paste-confirmation']['prompt']
            _require(row.get('source') == 'native-BM_CLICK' and row.get('button') == 1 and
                     row.get('dialog_window_token') == prompt['window_token'], 'actual owned confirmation action missing')
        _require(row.get('role') == role and all(type(row.get(k)) is type(initial[role][k]) and
                 row[k] == initial[role][k] for k in ('session_id','generation','window_token')),
                 'system action targeted stale/different session')
    return {'ordered_events':len(rows),'guarded_key_batches':9,'native_shortcuts':4}


def validate_workflow_operations(sessions, marks):
    """Verify actual A scroll state/submission endpoints, never presentation."""
    records,contexts = sessions.get('operation_records'),sessions.get('operation_context')
    _require(type(records) is list and type(contexts) is list and len(records) == len(contexts) == 2,
             'two actual scroll records/contexts required')
    points = {(p['phase'],p['cycle']):{s['name']:s for s in p['slots']} for p in sessions['checkpoints']}
    identity = points[('eight-ready',0)]['A']
    _require(identity['generation'] == 2, 'fixed operation A generation')
    mark_by_phase = {(m['phase'],m['cycle']):m for m in marks}
    first,last = mark_by_phase[('load-started',0)],mark_by_phase[('background-complete',0)]
    frequency = _uint(first.get('qpc_frequency'),'operation QPC frequency')
    _require(frequency > 0 and all(m.get('qpc_frequency') == frequency for m in marks), 'operation/resource clock mismatch')
    previous_submit = first['qpc_counter']; previous_revision = 0; previous_context = None
    for index,(record,context) in enumerate(zip(records,contexts),1):
        _require(record.get('schema') == 'termforge-scroll-operation-v1' and record.get('clock') == 'QPC-ticks' and
                 type(record.get('operation_id')) is int and record['operation_id'] == index and
                 type(record.get('scroll_command')) is int and record['scroll_command'] == index-1 and
                 record.get('qpc_frequency') == frequency and record.get('reason') == 'submitted' and
                 'presented_done' in record and record['presented_done'] is None,
                 'actual scroll action/clock/submission contract')
        for observation in (record,context):
            _require(observation.get('operation_id') == index and all(type(observation.get(key)) is int and
                     observation[key] == identity[key] for key in ('session_id','generation','window_creation_serial')),
                     'scroll operation stale/wrong session')
        _require(context.get('window_token') == identity['window_token'], 'scroll context wrong HWND')
        received = _uint(record.get('received_qpc'),'scroll received QPC')
        state = _uint(record.get('state_qpc'),'scroll state QPC')
        submitted = _uint(record.get('submitted_qpc'),'scroll submitted QPC')
        revision = _uint(record.get('published_revision'),'scroll revision')
        _require(previous_submit <= received <= state <= submitted <= last['qpc_counter'] and revision > previous_revision,
                 'scroll endpoint ordering/revision/load window')
        for side,number in (('before',110 if index==1 else 109),('after',109 if index==1 else 110)):
            sample = context.get(side)
            _require(type(sample) is dict and type(sample.get('top_line')) is dict,
                     'actual scroll visible top-line sample missing')
            line = sample['top_line']
            _require(type(line.get('id')) is int and line['id'] > 0 and type(line.get('version')) is int and
                     line['version'] > 0 and line.get('text') == f'TF_R108_A_{number:04d} 中文 e\u0301',
                     'scroll independent top-line literal/version')
            _uint(sample.get('published_revision'),'scroll sampled revision')
        _require(context['after']['published_revision'] == revision and
                 context['before']['published_revision'] < revision, 'scroll sample not bound to actual published revision')
        if previous_context is not None:
            _require(context['before'] == previous_context['after'] and
                     context['after']['top_line'] == previous_context['before']['top_line'],
                     'scroll roundtrip did not restore actual same line identity')
        previous_submit,previous_revision,previous_context = submitted,revision,context
    reading = points[('reading-before-append',0)]['A']['visible_lines'][0]
    _require(reading == contexts[1]['after']['top_line'], 'subsequent reading checkpoint differs from scroll return')
    return {'operations_verified':2,'state_and_submission_verified':True,'presented_done':None}


def validate_workflow_system(directory, record):
    """Strict system entry; never rewrite the system evidence into data-only."""
    from experiments.shell.workflow_sessions_checks import _bounded
    directory = Path(directory)
    validate_system_record(record)
    result = _validate_workflow_system_content(directory)
    streams = record.get('streams')
    _require(type(streams) is dict and set(streams) == {'stdout.jsonl','stderr.txt'}, 'system raw streams absent')
    raw_streams = {}
    for name in streams:
        raw = _bounded(directory/name,1024*1024); raw_streams[name] = raw
        _require(streams[name] == {'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}, 'system stream identity mismatch')
    shell,launcher = _validate_native_output(raw_streams['stdout.jsonl'])
    result.update(inner_and_outer_job_zero_verified=True, inner_job_after=launcher['inner_job_after'])
    return result


def _validate_workflow_system_content(directory):
    from experiments.shell.workflow_sessions_checks import _bounded, _validate_workflow_sessions_content
    directory = Path(directory)
    shared = _validate_workflow_sessions_content(directory,contract='system')
    sessions = json.loads(_bounded(directory/'sessions.json',2*1024*1024))
    data = json.loads(_bounded(directory/'workflow-system.json',4*1024*1024))
    for point in data.get('snapshots',[]):
        _require(len(json.dumps(point,ensure_ascii=False).encode('utf-8')) <= 32768, 'system snapshot exceeded bound')
    rows = [json.loads(line) for line in _bounded(directory/'workflow-system-progress.jsonl',1024*1024).splitlines()]
    result = validate_workflow_system_manifest(data,sessions)
    result.update(validate_workflow_system_progress(rows,data,sessions))
    marks = [json.loads(line) for line in _bounded(directory/'resource-phases.jsonl',65536).splitlines()]
    result.update(validate_workflow_operations(sessions,marks))
    frame = _bounded(directory/'frame.bmp',32*1024*1024)
    _require(len(frame) >= 54 and frame[:2] == b'BM', 'system actual frame missing')
    size,offset,dib,width,height = struct.unpack_from('<IxxxxIIii',frame,2)
    planes,bits,compression = struct.unpack_from('<HHI',frame,26)
    _require(dib == 40 and 0 < width <= 8192 and 0 < abs(height) <= 8192 and planes == 1 and bits == 32 and
             compression == 0 and offset == 54 and size == len(frame) == 54+width*abs(height)*4,
             'system actual frame format/geometry')
    result.update(sessions_verified=shared['checkpoints']==26, system_clipboard_verified=True, ime_verified=True,
                  frame_sha256=hashlib.sha256(frame).hexdigest(), complete_workflow_verified=False)
    return result


def _validate_native_output(raw):
    def unique_object(pairs):
        value = {}
        for key,item in pairs:
            _require(key not in value, 'duplicate native output key')
            value[key] = item
        return value
    _require(len(raw) <= 1024*1024 and raw.endswith(b'\n'), 'system native output bound/truncation')
    lines = raw.splitlines()
    _require(len(lines) == 2 and all(lines), 'system native stdout record count')
    shell,launcher = (json.loads(line,object_pairs_hook=unique_object) for line in lines)
    _require(shell == {'valid':True,'mode':'ui-workflow-system','sessions_closed':18,'active_children_after':0,'presented_done':None} and
             shell.get('valid') is True and type(shell.get('sessions_closed')) is int and
             type(shell.get('active_children_after')) is int, 'system native eighteen-session cleanup')
    from research.ime.product_checks import _job
    _require(type(launcher) is dict and set(launcher) == {'schema','mode','child_exit_code','child_signaled','timed_out','inner_job_after'} and
             launcher.get('schema') == 'termforge-hosted-ime-launcher-v1' and launcher.get('mode') == 'ui-workflow-system' and
             type(launcher.get('child_exit_code')) is int and launcher['child_exit_code'] == 0 and
             launcher.get('child_signaled') is True and launcher.get('timed_out') is False and
             _job(launcher.get('inner_job_after'),1), 'system native inner Job/child cleanup')
    _require(set(launcher['inner_job_after']) == {'active','total','terminated'}, 'system inner Job extra fields')
    return shell,launcher


def validate_system_record(record):
    from research.ime.product_checks import _job
    _require(type(record) is dict and record.get('mode') == 'ui-workflow-system' and
             record.get('status') in ('collected','passed') and record.get('assigned') is True and
             record.get('gate_released') is True and record.get('process_reaped') is True and
             record.get('timed_out') is False and type(record.get('returncode')) is int and record['returncode'] == 0 and
             record.get('forced_job_termination') is False and record.get('cleanup_errors') == [] and
             record.get('evidence_errors') == [] and record.get('error') is None and
             _job(record.get('outer_job_before_cleanup'),1) and _job(record.get('outer_job_after'),1),
             'system hosted launcher/owned Job cleanup')
    _require('desktop_gone' not in record, 'shared system input desktop cannot be removed or claimed gone')
    lifecycle = record.get('lifecycle')
    _require(type(lifecycle) is list and all(type(row) is dict for row in lifecycle) and
             [row.get('stage') for row in lifecycle] == ['starting','awaiting-go','job-assigned','releasing-go','finished'],
             'system GO occurred outside owned Job lifecycle')


def validate_system_public_record(record):
    """A public host gate; never synthesize the private product runner record."""
    keys = {'schema','mode','round','status','content_verified','resources_verified','real_system_workflow_verified',
            'desktop_policy','presented_done','complete_r1_verified','assigned','gate_released','process_reaped',
            'timed_out','forced_job_termination','returncode','outer_job_before_cleanup','outer_job_after',
            'cleanup_error_count','evidence_error_count','lifecycle','streams','public_streams','stderr_error_bytes',
            'native_shell','native_launcher','error_kind','verification','resources'}
    _require(type(record) is dict and set(record) == keys and record['schema'] == 'termforge-workflow-system-case-v1' and
             record['mode'] == 'ui-workflow-system' and type(record['round']) is int and record['round'] in (0,1) and
             record['status'] == 'passed' and record['desktop_policy'] == 'existing-input-desktop-no-create-switch-or-delete' and
             record['presented_done'] is None and record['complete_r1_verified'] is False and record['error_kind'] is None,
             'public system host identity/status/schema')
    _require(all(record[field] is True for field in ('assigned','gate_released','process_reaped','content_verified',
             'resources_verified','real_system_workflow_verified')) and record['timed_out'] is False and
             record['forced_job_termination'] is False and type(record['returncode']) is int and record['returncode'] == 0,
             'public system process/GO flags')
    from research.ime.product_checks import _job
    _require(_job(record['outer_job_before_cleanup'],1) and _job(record['outer_job_after'],1) and
             all(type(record[key]) is int and record[key] == 0 for key in
                 ('cleanup_error_count','evidence_error_count','stderr_error_bytes')), 'public system cleanup/errors')
    for key in ('outer_job_before_cleanup','outer_job_after'):
        _require(set(record[key]) == {'active','total','terminated'}, 'public Job unknown fields')
    lifecycle = record['lifecycle']
    _require(type(lifecycle) is list and len(lifecycle) == 5 and
             [p.get('stage') for p in lifecycle] == ['starting','awaiting-go','job-assigned','releasing-go','finished'],
             'public bounded GO lifecycle')
    previous = 0
    for point in lifecycle:
        elapsed = point.get('elapsed_seconds')
        _require(set(point) == {'stage','elapsed_seconds'} and type(elapsed) in (int,float) and
                 math.isfinite(elapsed) and elapsed >= previous, 'public GO clock/fields')
        previous = elapsed
    _require(record['public_streams'] == {'stdout.jsonl':'public/stdout.jsonl','stderr.txt':'public/stderr.txt'} and
             type(record['streams']) is dict and set(record['streams']) == {'stdout.jsonl','stderr.txt'},
             'public native stream references')


def validate_workflow_system_public(directory):
    """Offline entry for the artifact whitelist: public GO + original bytes + content.

    No private result, PID, resource raw trace or environment spoofing is needed.
    The native binary identities still require separate exact-head provenance
    review; these data do not provide independent source-to-binary attestation.
    """
    from experiments.shell.workflow_sessions_checks import _bounded
    from experiments.shell.workflow_resources import summarize_resource_observations
    from experiments.shell.workflow_resource_phases import summarize_resource_phases
    directory = Path(directory)
    record = json.loads(_bounded(directory/'result.public.json',256*1024))
    validate_system_public_record(record)
    raw = {}
    for name in ('stdout.jsonl','stderr.txt'):
        raw[name] = _bounded(directory/'public'/name,1024*1024)
        _require(record['streams'][name] == {'bytes':len(raw[name]),'sha256':hashlib.sha256(raw[name]).hexdigest()},
                 'public original stream digest mismatch')
    _require(raw['stderr.txt'] == b'', 'public successful stderr must be empty')
    shell,launcher = _validate_native_output(raw['stdout.jsonl'])
    _require(record['native_shell'] == shell and record['native_launcher'] == launcher, 'public native cleanup objects differ')
    result = _validate_workflow_system_content(directory)
    summary = json.loads(_bounded(directory/'resource-summary.json',4*1024*1024))
    derived = summarize_resource_observations(directory/'resource-observations.jsonl')
    _require(summary.get('status') == 'recorded' and summary.get('scope') == 'launcher-tree' and
             summary.get('root_identity_verified') is True and summary.get('sampler_started') is True and
             summary.get('sampler_joined') is True and summary.get('performance_gate') == 'not_evaluated' and
             summary.get('observations') == {'file':'resource-observations.jsonl','summary':derived},
             'public resource derivation/sampler identity')
    phases = summary.get('phase_windows')
    _require(type(phases) is dict and phases.get('status') == 'mapped', 'public native/resource mapping unknown')
    checked = summarize_resource_phases(directory/'resource-observations.jsonl',directory/'resource-phases.jsonl',
                                        calibration=phases.get('calibration'),marks=summary.get('marks'))
    _require(checked == phases and checked.get('status') == 'mapped', 'public phase/resource replay differs')
    _require(record['resources'] == dict(scope='launcher-tree',phase_status='mapped',performance_gate='not_evaluated',
             sample_count=derived['sample_count']), 'public resource counts differ')
    _require(record['verification'] == {key:result[key] for key in ('snapshots','ordered_events','guarded_key_batches',
             'native_shortcuts','companion_observations','combined_observations')}, 'public verification counts differ')
    result.update(public_host_and_streams_verified=True,resources_verified=True,
                  inner_and_outer_job_zero_verified=True,private_resource_raw_available=False)
    return result


def validate_system_input_snapshot(snapshot, phase):
    """Check actual intermediate raw content against a fixed synthetic contract.

    This helper establishes neither actual Copy/IME actions nor their ordering;
    those need the system companion and independently captured native evidence.
    """
    expected_by_phase = {
        'copied': b'', 'paste-complete': PASTE_PREFIX,
        'commit-preedit': PASTE_PREFIX, 'committed': COMMITTED_INPUT,
        'cancel-preedit': COMMITTED_INPUT, 'cancelled': COMMITTED_INPUT,
        'after-paste-close': FINAL_INPUT,
    }
    if phase not in expected_by_phase:
        raise ValueError('unknown system input phase')
    if type(snapshot) is not dict or set(snapshot) != {'A','B','C','F1','F2','F3','F4','F5'}:
        raise ValueError('system input requires all eight actual roles')
    for name, raw in snapshot.items():
        expected = expected_by_phase[phase] if name == 'B' else b''
        if type(raw) is not dict or type(raw.get('raw_bytes')) is not int or (
            raw['raw_bytes'] != len(expected) or raw.get('raw_hex') != expected.hex() or
            raw.get('raw_sha256') != hashlib.sha256(expected).hexdigest()
        ):
            raise ValueError('system input literal/target/timing mismatch')
