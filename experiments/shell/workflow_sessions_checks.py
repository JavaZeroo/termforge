"""Independent E2 session identity oracle, extended only at real scenario seams."""
import hashlib
import json
from pathlib import Path
import re
from experiments.shell.workflow_checks import validate_fixture_case

NAMES = ('A', 'B', 'C', 'F1', 'F2', 'F3', 'F4', 'F5')
EMPTY_SHA256 = hashlib.sha256(b'').hexdigest()
PHASES = ('eight-ready', 'reject-ninth', 'load-started', 'reading-before-append',
          'reading-after-append', 'search-running', 'find-complete', 'foreground-input',
          'zoom-75', 'switch-zoom', 'history-evicted', 'background-complete')
FULL_PHASES = ([(phase, 0) for phase in PHASES[:11]] +
               [('paste-cancel-pending', 0), ('paste-cancelled', 0), ('background-complete', 0)] +
               [(phase, cycle) for cycle in range(1, 6) for phase in ('cycle-six', 'cycle-eight')] +
               [('paste-close-pending', 5), ('after-paste-close', 5)])


def expected_raw(name, phase):
    if name == 'B' and phase not in PHASES[:7]:
        return ('TF_R108_FOREGROUND 中文\r' + ('!' if phase == 'after-paste-close' else '')).encode('utf-8')
    return b''


def validate_checkpoint_raw(slot, phase):
    expected = expected_raw(slot['name'], phase)
    _require(slot.get('raw_hex') == expected.hex() and type(slot.get('raw_bytes')) is int and
             slot['raw_bytes'] == len(expected) and slot.get('raw_sha256') == hashlib.sha256(expected).hexdigest(),
             'checkpoint input literal/target/timing mismatch')


def _require(condition, reason):
    if not condition:
        raise ValueError(reason)


def _integer(value, reason, minimum=0):
    _require(type(value) is int and value >= minimum, reason)
    return value


def _bounded(path, limit):
    _require(path.is_file() and not path.is_symlink(), f'missing or linked {path.name}')
    with path.open('rb') as stream:
        value = stream.read(limit + 1)
    _require(len(value) <= limit, f'oversized {path.name}')
    return value


def _a_line(number):
    return f'TF_R108_A_{number:04d} 中文 e\u0301'


def _f_line(role, number):
    return f'TF_R108_{role}_{number:05d} 中文 e\u0301'.encode('utf-8').ljust(62, b'.').decode('utf-8')


def _cache_bounds(runtime):
    search = runtime.get('search', {})
    # Fixed Windows scenario allocation observation budgets, allowing allocator
    # slack; not portable container guarantees or product performance gates.
    query = _integer(search.get('find_query_bytes'), 'query bytes')
    results = _integer(search.get('result_count'), 'result count')
    _require(query <= 4096 and query <= _integer(search.get('find_query_capacity'), 'query capacity') <= 8192 and
             results <= 1024 and results <= _integer(search.get('result_capacity'), 'result capacity') <= 2048,
             'search allocation bound')
    # Metadata counts physical rows, not bytes. Include the maximum 200-row
    # screen plus 100 history rows across resize/eviction sampling intervals.
    _require(_integer(search.get('retained_metadata_rows'), 'metadata rows') <= 300 and
             _integer(search.get('pending_navigation_count'), 'pending navigation') <= 1, 'search metadata/navigation bound')
    terminal = runtime.get('last_terminal_paste')
    for paste, active in ((runtime.get('paste'), True), (terminal, False)):
        if paste is None:
            continue
        _require(type(paste) is dict, 'paste observation missing')
        total = _integer(paste.get('total_bytes'), 'paste total')
        accepted = _integer(paste.get('accepted_bytes'), 'paste accepted')
        sent = _integer(paste.get('sent_bytes'), 'paste sent')
        flight = _integer(paste.get('in_flight_bytes'), 'paste in flight')
        capacity = _integer(paste.get('retained_capacity_bytes'), 'paste capacity')
        _require(total == 3000000 and paste.get('source_units') == 1000000 and
                 0 <= sent <= accepted <= total and type(paste.get('error_present')) is bool,
                 'paste source/counter/error observation')
        if active:
            _require(runtime.get('paste_busy') is True and paste.get('state') == 'sending' and
                     paste.get('error_present') is False and accepted - sent <= flight <= 1024 and
                     total <= capacity <= 4 * 1024 * 1024 + 32, 'active paste allocation/in-flight/error bound')
        else:
            _require(paste.get('state') in ('cancelled', 'failed') and flight == 0 and capacity <= 32 and
                     (paste['state'] == 'failed' or (paste['error_present'] is False and sent == accepted)),
                     'terminal paste allocation/error bound')


def _runtime_bounds(slot, allow_paste=False):
    runtime = slot['runtime']
    _require(runtime.get('schema') == 'termforge-runtime-snapshot-v1' and
             runtime.get('window_creation_serial') == slot['window_creation_serial'], 'runtime identity')
    _require(_integer(runtime.get('capture_begin_ns'), 'capture begin') <=
             _integer(runtime.get('capture_end_ns'), 'capture end'), 'runtime sampling interval')
    for name, budget in (('output', 262144), ('input', 16384)):
        queue = runtime.get(name, {})
        _require(_integer(queue.get('queued_bytes'), 'queued bytes') <= budget and
                 _integer(queue.get('high_water_bytes'), 'queue high water') <= budget, 'actual queue bound')
    incoming = runtime['input']
    _require(_integer(incoming.get('in_flight_bytes'), 'in flight') <= 1024 and
             _integer(incoming.get('rejected'), 'input rejected') == 0 and incoming.get('failed') is False, 'input worker error/bound')
    _require(_integer(runtime.get('history_rows'), 'history rows') <= 100 and runtime.get('history_limit') == 100 and
             1 <= _integer(runtime.get('rows'), 'rows') <= 200 and
             _integer(runtime.get('retained_rows'), 'retained rows') <= 100 + runtime['rows'], 'history bounds')
    _require(_integer(runtime.get('max_consumed_per_tick'), 'tick bytes') <= 65536, 'UI consumption bound')
    _cache_bounds(runtime)
    _require(runtime.get('pending_prepared_paste') is False and runtime.get('paste_prompt_present') is False and
             runtime.get('clipboard') is None, 'unexpected prompt/clipboard operation in data slice')
    if not allow_paste:
        _require(runtime.get('paste_busy') is False and runtime.get('paste') is None, 'unexpected active paste in load slice')
    resize = runtime.get('resize', {})
    _require(resize.get('failed') is False, 'resize worker failed')


def _validate_vertical(out, checkpoints, final_b=None):
    by_phase = {point['phase']: {slot['name']: slot for slot in point['slots']} for point in checkpoints}
    for index, phase in enumerate(PHASES):
        slots = by_phase[phase]
        for name, slot in slots.items():
            _runtime_bounds(slot)
            lines = slot.get('visible_lines')
            _require(type(lines) is list and len(lines) == slot['runtime']['rows'], 'actual visible row count')
            _require(all(type(line) is dict and type(line.get('text')) is str and
                         type(line.get('id')) is int and type(line.get('version')) is int for line in lines), 'visible line identity/text')
            expected_zoom = 75 if name == 'B' and phase == 'zoom-75' else 100
            _require(slot['runtime'].get('zoom_percent') == expected_zoom, 'zoom target isolation/reset')
            if name.startswith('F') and 2 <= index <= 8:
                _require(slot.get('output_done') is False and
                         4096 <= _integer(slot.get('source_bytes'), 'background prefix') <= 2048000,
                         'foreground interaction lacks active background load')
            if name in ('B', 'C'):
                _require(_integer(slot.get('source_bytes'), 'receiver source') == 0, 'receiver role emitted payload')
        if index >= 2:
            selected_main = 'B' if phase in ('foreground-input', 'zoom-75') else 'A'
            _require(slots[selected_main]['selected'] is True, 'foreground selected wrong target')
    before = by_phase['reading-before-append']['A']
    after = by_phase['reading-after-append']['A']
    first = before['visible_lines'][0]
    _require(first['text'] == _a_line(110) and after['visible_lines'][0] == first and
             before['runtime']['viewport']['following'] is False and after['runtime']['viewport']['following'] is False and
             before['runtime']['viewport']['anchor'] == after['runtime']['viewport']['anchor'] == first['id'],
             'append moved or replaced retained reading anchor')
    for phase, count in (('load-started', 160), ('reading-before-append', 160), ('reading-after-append', 180)):
        expected_bytes = len(''.join(_a_line(i)+'\r\n' for i in range(count)).encode('utf-8'))
        _require(by_phase[phase]['A'].get('source_bytes') == expected_bytes, 'A numbered stage byte count')
    searching = by_phase['search-running']['A']; found = by_phase['find-complete']['A']
    for slot, query, state in ((searching, 'TF_R108_A_', 'running'), (found, 'TF_R108_A_0110', 'complete')):
        search = slot['runtime']['search']
        _require(slot.get('find_query') == query and slot.get('find_edit_exists') is True and
                 type(slot.get('find_edit_token')) is str and re.fullmatch(r'[1-9][0-9]*', slot['find_edit_token']) is not None and
                 search.get('state') == state and search.get('find_query_bytes') == len(query), 'actual Find EDIT query/state')
    _require(found['runtime']['search']['generation'] > searching['runtime']['search']['generation'], 'Find query replacement reused generation')
    hit = found.get('current_find')
    _require(type(hit) is dict and hit == dict(line_id=first['id'], version=first['version'], column=0, end_column=14),
             'Find matched stale or wrong literal range')
    _require(any(line == first for line in found['visible_lines']), 'Find result not visible at its live range')
    switched = by_phase['switch-zoom']['A']; evicted = by_phase['history-evicted']['A']
    _require(switched.get('find_edit_exists') is False and switched.get('find_query') is None and
             switched.get('find_edit_token') is None and switched.get('current_find') is None, 'Find not actually closed')
    _require(evicted['runtime']['viewport']['following'] is False and evicted['runtime']['viewport']['top'] == 0 and
             _integer(evicted.get('document_first_line_id'), 'retained first id') > first['id'] and
             evicted.get('current_find') is None and all(line['id'] != first['id'] for line in evicted['visible_lines']),
             'eviction retained old range or failed to clamp reading')
    final = by_phase['background-complete']
    for name, slot in final.items():
        directory = (out / slot['capture']).parent
        validate_fixture_case(directory, expected_role=slot['fixture_role'], expected_generation=slot['generation'],
                              expected_input=final_b if name == 'B' and final_b is not None else expected_raw(name, 'background-complete'))
        if name == 'A':
            tail = [_a_line(i) for i in range(672, 680)] + ['TF_R108_A_EVICT_DONE 0500', '']
            _require(slot.get('source_bytes') == 18360 and slot.get('document_tail') == tail, 'A final retained literal tail')
        elif name.startswith('F'):
            tail = [_f_line(name, i) for i in range(31992, 32000)] + [f'TF_R108_{name}_OUTPUT_DONE 0500', '']
            _require(slot.get('source_bytes') == 2048000 and slot.get('output_done') is True and
                     slot.get('document_tail') == tail, 'background full source/core tail mismatch')


def _validate_closed(closed, baseline, full=False):
    entities = {identity['session_id']: dict(identity, name=identity.get('name', key)) for key, identity in baseline.items()}
    _require(type(closed) is list and len(closed) == len(entities) and
             {row.get('session_id') for row in closed} == set(entities), 'all distinct closed entities required')
    for row in closed:
        _integer(row.get('session_id'), 'closed session identity', 1)
        _integer(row.get('generation'), 'closed fixture generation', 1)
        identity = entities[row['session_id']]
        abnormal = row.get('fixture_closed_while_running') is True
        _require(row.get('name') == identity['name'] and all(row.get(key) == identity[key] for key in ('session_id', 'generation', 'window_token')) and
                 row.get('window_gone') is True and row.get('fixture_signaled') is True and
                 type(row.get('fixture_exit_code')) is int and row['fixture_exit_code'] == (91 if abnormal else 0), 'closed entity or fixture mismatch')
        if full:
            _require(type(row.get('fixture_closed_while_running')) is bool and row.get('find_window_gone') is True, 'closed fixture/Find observation missing')
        before, final = row.get('before_close'), row.get('final')
        _require(type(before) is dict and type(final) is dict, 'actual preclose/final snapshots missing')
        for snapshot in (before, final):
            if full:
                _cache_bounds(snapshot)
            _require(snapshot.get('schema') == 'termforge-runtime-snapshot-v1' and
                     _integer(snapshot.get('session_id'), 'closed runtime identity', 1) == identity['session_id'] and
                     _integer(snapshot.get('window_creation_serial'), 'closed creation serial', 1) == identity['window_creation_serial'], 'close runtime bound to wrong window')
        _require(final.get('final_after_join') is True and final.get('window_exists') is False and
                 final.get('close_error_present') is False, 'close not joined/window still exists')
        for worker in ('input', 'resize'):
            state = final.get(worker, {})
            _require(state.get('joined') is True and state.get('failed') is False, 'worker not reclaimed')
        _require(final['input'].get('queued_bytes') == 0 and final['input'].get('in_flight_bytes') == 0 and
                 final['resize'].get('working') is False and final['resize'].get('pending') is False,
                 'closed worker retained queued/in-flight work')
        _require(final.get('paste_busy') is False and final.get('pending_prepared_paste') is False and
                 final.get('paste_prompt_present') is False and final.get('paste') is None and final.get('clipboard') is None,
                 'closed window retained paste/clipboard task')
        session = final.get('final_session', {})
        _require(session.get('active_children_query_failed') is False and
                 type(session.get('active_children')) is int and session['active_children'] == 0 and
                 session.get('child_signaled') is True and session.get('cleanup_complete') is True and
                 session.get('capture_overflow') is False, 'session Job/capture cleanup failed')


def validate_cycle_transition(previous, six, eight, closed):
    _require(set(previous) == set(eight) == set(NAMES) and set(six) == set(NAMES[:6]), 'cycle live target sets')
    keys = ('session_id', 'generation', 'window_creation_serial', 'window_token')
    for name in NAMES[:6]:
        _require(all(previous[name][key] == six[name][key] == eight[name][key] for key in keys), 'resident identity changed during replacement')
    _require(len(closed) == 2 and {row['name'] for row in closed} == {'F4','F5'}, 'closed replacement pair')
    for row in closed:
        _require(row.get('window_gone') is True and all(row[key] == previous[row['name']][key]
                 for key in ('session_id', 'generation', 'window_token')), 'old generation was not destroyed before replacement')
    for name in ('F4', 'F5'):
        _require(all(eight[name][key] not in {row[key] for row in previous.values()}
                     for key in keys[:3]), 'replacement reused generation/session/creation serial')
    _require(len({row['window_token'] for row in eight.values()}) == 8, 'replacement aliases a live HWND')


def _validate_cycles_and_paste(out, data, entities):
    marks = [json.loads(line) for line in _bounded(out/'resource-phases.jsonl', 65536).splitlines()]
    validate_resource_phases(marks)
    observed = sum(len(point['slots'])+len(point.get('background_load', [])) for point in data['checkpoints']) + 2*len(data['closed'])+len(marks)
    _require(len(entities) == 18 and observed == 235 and _integer(data.get('observations'), 'observations') == observed <= 256 and
             data.get('resource_markers') == len(marks), 'bounded eighteen-entity observation count')
    _validate_closed(data.get('closed'), entities, full=True)
    checkpoints = data['checkpoints']
    by_phase = {(p['phase'], p['cycle']): {s['name']: s for s in p['slots']} for p in checkpoints}
    closed = data['closed']; prior = by_phase[('background-complete', 0)]
    for cycle in range(1, 6):
        six = by_phase[('cycle-six', cycle)]; eight = by_phase[('cycle-eight', cycle)]
        expected_closed = {prior[name]['session_id'] for name in ('F4', 'F5')}
        rows = closed[(cycle-1)*2:cycle*2]
        validate_cycle_transition(prior, six, eight, rows)
        _require({row['session_id'] for row in rows} == expected_closed, 'cycle closed wrong generations')
        for row in rows:
            _require(row['fixture_closed_while_running'] is (cycle > 1), 'cycle fixture exit reason inconsistent')
            if row['name'] == 'F4':
                search = row['before_close']['search']
                _require(search.get('state') == 'running' and search.get('editor_present') is True and
                         search.get('work_budget_units') == 1, 'cycle did not close actual running Find')
        _require(set(six) == set(NAMES[:6]), 'cycle six residents changed')
        for name in ('F4', 'F5'):
            slot = eight[name]
            _runtime_bounds(slot)
            _require(slot.get('fixture_running') is True and slot.get('output_done') is False and
                     4096 <= _integer(slot.get('source_bytes'), 'replacement source bytes') < 2048000, 'replacement output not active')
            texts = slot.get('document_tail', []) + [row['text'] for row in slot.get('visible_lines', [])]
            numbered = [text for text in texts if re.match(r'TF_R108_F[1-5]_[0-9]{5} ', text)]
            _require(numbered and all(text.startswith(f'TF_R108_{name}_') and
                     text == _f_line(name, int(text.split('_')[3].split(' ')[0])) for text in numbered), 'replacement core payload/role mismatch')
            path = out / slot['capture']; source = _bounded(Path(str(path)+'.source.bin'), 2048000)
            expected = b''.join((_f_line(name, number)+'\r\n').encode('utf-8') for number in range(32000))
            _require(0 < len(source) < len(expected) and len(source)%4096 == 0 and
                     source == expected[:len(source)], 'replacement literal source prefix')
            _require(all(int(text.split('_')[3].split(' ')[0]) < len(source)//64 for text in numbered),
                     'replacement core row lies outside its actual source prefix')
            if cycle == 5:
                validate_fixture_case(path.parent, expected_role=name, expected_generation=slot['generation'],
                                      expected_input=b'', expected_complete=False)
            else:
                _require(_bounded(Path(str(path)+'.workflow.json'), 65536) == b'' and
                         not Path(str(path)+'.receiver.json').exists(), 'killed fixture fabricated completed sidecars')
                progress = [json.loads(line) for line in _bounded(Path(str(path)+'.progress.jsonl'), 65536).splitlines()]
                _require(len(progress) == 2, 'killed fixture unexpectedly completed')
                elapsed = 0
                for index, (event, phase) in enumerate((('ready', None), ('event-start', 'output'))):
                    row = progress[index]
                    _require(row.get('schema') == 'termforge-workflow-progress-v1' and _integer(row.get('seq'), 'progress sequence') == index+1 and
                             row.get('event') == event and row.get('phase') == phase and _integer(row.get('source_bytes'), 'progress bytes') == 0 and
                             _integer(row.get('records'), 'progress records') == 0 and row.get('source_sha256') == EMPTY_SHA256, 'killed fixture progress identity')
                    current = _integer(row.get('elapsed_ms'), 'progress time')
                    _require(current >= elapsed, 'killed fixture progress time regressed'); elapsed = current
        prior = eight
    original_c = by_phase[('eight-ready', 0)]['C']
    _require(closed[10]['session_id'] == original_c['session_id'] and closed[10]['name'] == 'C' and
             closed[10]['final']['final_session'].get('exit_code') == 91, 'paste close did not collect C native session')
    _validate_paste_cases(out, data, original_c['session_id'])
    terminal_snapshots = (by_phase[('paste-cancelled', 0)]['C']['runtime'], closed[10]['final'])
    for case, snapshot in zip(data['paste_cases'], terminal_snapshots):
        terminal = snapshot.get('last_terminal_paste')
        _require(type(terminal) is dict and all(terminal.get(observed) == case[recorded] for observed, recorded in
                 (('target', 'target'), ('state', 'state'), ('total_bytes', 'total'),
                  ('accepted_bytes', 'accepted'), ('sent_bytes', 'sent'), ('in_flight_bytes', 'in_flight'),
                  ('retained_capacity_bytes', 'retained_capacity'))), 'terminal snapshot contradicts receiver paste case')
    for phase, cycle, roles, previous in (('paste-cancel-pending', 0, NAMES[3:], ('history-evicted', 0)),
                                          ('paste-close-pending', 5, ('F4','F5'), ('cycle-eight', 5))):
        point = next(point for point in checkpoints if point['phase'] == phase)
        backgrounds = point.get('background_load')
        _require(type(backgrounds) is list and sorted(row.get('name','') for row in backgrounds) == sorted(roles), 'actual background load set missing at paste seam')
        for row in backgrounds:
            _require(row.get('generation') == by_phase[previous][row['name']]['generation'] and
                     row.get('fixture_running') is True and row.get('output_done') is False and
                     4096 <= _integer(row.get('source_bytes'), 'paste background bytes') < 2048000, 'paste did not overlap its bound running background generation')
        c = by_phase[(phase,cycle)]['C']
        pending = c['runtime'].get('paste')
        _require(type(pending) is dict and pending.get('target') == original_c['session_id'] and
                 pending.get('state') == 'sending' and pending.get('total_bytes') == 3000000 and
                 16384 <= pending.get('sent_bytes', -1) <= pending.get('accepted_bytes', -1) < 3000000,
                 'pending checkpoint lacks actual partial target transfer')


def validate_resource_phases(marks):
    expected = [('eight-ready',0), ('load-started',0), ('background-complete',0)] + [
        (phase,cycle) for cycle in range(1,6) for phase in ('cycle-six','cycle-six-end','cycle-eight')] + [('after-paste-close',5)]
    _require(type(marks) is list and len(marks) == 19, 'resource phase count')
    previous = 0; frequency = None; starts = {}
    for index,(row,(phase,cycle)) in enumerate(zip(marks,expected),1):
        _require(row.get('schema') == 'termforge-workflow-resource-phase-v1' and
                 _integer(row.get('seq'),'resource sequence') == index and row.get('phase') == phase and
                 _integer(row.get('cycle'),'resource cycle') == cycle, 'resource marker order')
        tick = _integer(row.get('qpc_counter'),'QPC counter',1)
        freq = _integer(row.get('qpc_frequency'),'QPC frequency',1)
        if frequency is None: frequency = freq
        _require(freq == frequency and tick >= previous, 'resource clock changed/regressed'); previous = tick
        six = phase in ('cycle-six','cycle-six-end')
        _require(row.get('live_sessions') == (6 if six else 7 if phase=='after-paste-close' else 8) and
                 row.get('window_creations') == 8+2*(cycle-1 if six else cycle), 'resource sample includes wrong population/creation')
        if phase == 'cycle-six': starts[cycle] = tick
        elif phase == 'cycle-six-end':
            _require(tick-starts[cycle] >= freq, 'six-resident interval shorter than one second')


def _validate_paste_cases(out, data, target):
    cases = data.get('paste_cases')
    _require(type(cases) is list and [row.get('name') for row in cases] == ['cancel', 'close'], 'both actual paste cases required')
    expected = '汉'.encode('utf-8') * 1000000
    for row in cases:
        for key in ('target', 'total', 'before_sent', 'before_accepted', 'accepted', 'sent',
                    'in_flight', 'retained_capacity', 'checkpoint_bytes', 'received'):
            _integer(row.get(key), f'paste {key}')
        _require(row['target'] == target and row['total'] == 3000000 and
                 16384 <= row['before_sent'] <= row['before_accepted'] < row['total'] and
                 row['before_sent'] <= row['sent'] <= row['accepted'] < row['total'] and
                 row['accepted'] >= row['before_accepted'] and row['in_flight'] == 0 and
                 row['retained_capacity'] <= 32 and row['checkpoint_bytes'] == 768 and
                 row.get('flood_observed') is True and row.get('state') in ('cancelled', 'failed'), 'paste did not remain bounded/partial/terminal')
        raw = _bounded(out / (row['name']+'.bin'), 3000000)
        _require(0 < len(raw) == row['received'] <= row['sent'] and raw == expected[:len(raw)] and len(raw)%3 == 0,
                 'gated receiver differs from exact complete Unicode prefix')
        if row['name'] == 'cancel':
            _require(row['state'] == 'cancelled' and len(raw) == row['sent'] == row['accepted'], 'cancel left submitted bytes unconsumed')
        else:
            _require(raw == '汉'.encode('utf-8')*256, 'closed receiver escaped its 768-byte checkpoint')


def validate_workflow_sessions(out, record, *, allow_partial=False):
    """First slice: eight live identities and rejected ninth creation, not full E2."""
    out = Path(out)
    _require(type(record) is dict and record.get('timed_out') is False and
             type(record.get('exit_code')) is int and record['exit_code'] == 0 and
             record.get('desktop_gone') is True and not record.get('cleanup_errors'), 'runner exit/desktop cleanup')
    job = record.get('job_after', {})
    _require(type(job) is dict and _integer(job.get('active'), 'outer Job active') == 0, 'outer Job not empty')
    data = json.loads(_bounded(out / 'sessions.json', 2 * 1024 * 1024))
    _require(type(data) is dict and data.get('schema') == 'termforge-workflow-sessions-v1' and
             data.get('fixture') == 'r108-console-v1' and data.get('data_only') is True and
             data.get('system_clipboard') is False and data.get('ime') is False, 'session evidence identity')
    checkpoints = data.get('checkpoints')
    _require(type(checkpoints) is list and (len(checkpoints) == 26 or allow_partial and len(checkpoints) in (2, 12)), 'full E2 checkpoint count required')
    full = len(checkpoints) == 26
    vertical = len(checkpoints) >= 12
    phases = FULL_PHASES if full else [(phase, 0) for phase in (PHASES if vertical else PHASES[:2])]
    previous_time = 0
    baseline = baseline_job = None
    all_entities = {}
    current_identities = {}
    for index, (phase, cycle) in enumerate(phases):
        point = checkpoints[index]
        _require(type(point) is dict and _integer(point.get('seq'), 'checkpoint sequence') == index + 1 and
                 point.get('phase') == phase and _integer(point.get('cycle'), 'cycle') == cycle, 'identity phase order')
        elapsed = _integer(point.get('elapsed_ms'), 'elapsed time')
        _require(elapsed >= previous_time, 'checkpoint clock regressed'); previous_time = elapsed
        created = 8 + 2 * (cycle - 1 if phase == 'cycle-six' else cycle)
        _require(_integer(point.get('attempted_creations'), 'creation attempts') == (8 if index == 0 else created + 1) and
                 _integer(point.get('window_creations'), 'created windows') == created, 'actual creation count mismatch')
        observed_job = point.get('job')
        _require(type(observed_job) is dict, 'actual Job accounting absent')
        accounting = tuple(_integer(observed_job.get(key), f'Job {key}') for key in ('active', 'total', 'terminated'))
        expected_names = ('B',) if phase == 'after-paste-close' else ('B', 'C') if phase.startswith('paste-') else NAMES[:6] if phase == 'cycle-six' else NAMES
        _require(accounting[0] >= len(expected_names) and accounting[1] >= accounting[0] and accounting[2] <= accounting[1], 'impossible Job accounting')
        slots = point.get('slots')
        _require(type(slots) is list and len(slots) == len(expected_names) and all(type(slot) is dict for slot in slots), 'actual slot count')
        _require(sorted(slot.get('name', '') for slot in slots) == sorted(expected_names), 'fixed roles missing/duplicated')
        identities = {}
        unique = {key: set() for key in ('session_id', 'generation', 'window_token', 'window_creation_serial', 'process_identity')}
        selections = {'main': 0, 'bottom': 0}
        for slot in slots:
            name = slot['name']
            group = 'main' if name in ('A', 'B', 'F1', 'F2') else 'bottom'
            _require(slot.get('fixture_role') == ('B' if name == 'C' else name) and slot.get('group') == group, 'role/group binding')
            for key in ('session_id', 'generation', 'window_creation_serial'):
                _integer(slot.get(key), key, 1)
            token = slot.get('window_token')
            _require(type(token) is str and re.fullmatch(r'[1-9][0-9]*', token) is not None, 'actual nonzero HWND token')
            identity = slot.get('process_identity')
            _require(type(identity) is str and re.fullmatch(r'[0-9a-f]{64}', identity) is not None, 'bound process identity digest')
            for key, values in unique.items():
                _require(slot[key] not in values, f'duplicate live {key}'); values.add(slot[key])
            for flag in ('exists', 'fixture_ready', 'fixture_bound'):
                _require(slot.get(flag) is True, f'slot {flag}')
            _require(type(slot.get('selected')) is bool and type(slot.get('visible')) is bool and
                     slot['selected'] == slot['visible'], 'actual selected/visible mismatch')
            selections[group] += int(slot['selected'])
            runtime = slot.get('runtime')
            _require(type(runtime) is dict and type(runtime.get('session_id')) is int and
                     runtime['session_id'] == slot['session_id'], 'runtime attached to wrong session')
            if full:
                _runtime_bounds(slot, allow_paste=name == 'C' and phase in ('paste-cancel-pending','paste-close-pending'))
            capture = f'fixtures/{name}-{slot["generation"]}/capture.bin'
            _require(slot.get('capture') == capture, 'capture role/generation path')
            path = out / capture
            _require(path.resolve().is_relative_to(out.resolve()), 'capture escaped evidence directory')
            raw = expected_raw(name, phase) if vertical else b''
            final_raw = expected_raw(name, phases[-1][0]) if vertical else b''
            _require(_bounded(path, 16384) == final_raw and _integer(slot.get('raw_bytes'), 'raw bytes') == len(raw) and
                     slot.get('raw_sha256') == hashlib.sha256(raw).hexdigest(), 'input literal/target/timing mismatch')
            if vertical:
                validate_checkpoint_raw(slot, phase)
            identities[name] = {key: slot[key] for key in ('session_id', 'generation', 'window_token',
                'window_creation_serial', 'process_identity', 'capture', 'selected', 'visible', 'group')}
        if len(expected_names) >= 6:
            _require(selections == {'main': 1, 'bottom': 1}, 'one visible selected slot per group required')
        if index == 0:
            baseline, baseline_job = identities, accounting
        elif index == 1:
            _require(identities == baseline and accounting == baseline_job, 'rejected ninth altered live entities or Job totals')
        else:
            for name in expected_names:
                if phase == 'cycle-eight' and name in ('F4', 'F5'):
                    _require(all(identities[name][key] not in {old[key] for old in all_entities.values()}
                                 for key in ('session_id', 'generation', 'window_creation_serial', 'process_identity')), 'replacement reused old entity identity')
                else:
                    _require(all(identities[name][key] == current_identities[name][key] for key in identities[name]
                                 if key not in ('selected', 'visible')), 'live target identity changed')
        current_identities.update(identities)
        for name, identity in identities.items():
            all_entities[identity['session_id']] = dict(identity, name=name)
    if vertical:
        _validate_vertical(out, [p for p in checkpoints if p['phase'] in PHASES],
                           final_b=expected_raw('B', 'after-paste-close') if full else None)
    if full:
        _validate_cycles_and_paste(out, data, all_entities)
    elif vertical or 'closed' in data:
        _validate_closed(data.get('closed'), baseline)
    return dict(schema='termforge-workflow-sessions-identity-validation-v1', live_sessions=8,
                checkpoints=len(checkpoints), rejected_ninth=True, load_foreground_verified=vertical,
                data_workflow_verified=full, complete_workflow_verified=False,
                system_clipboard_verified=False, ime_verified=False)
