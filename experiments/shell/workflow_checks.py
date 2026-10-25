"""Independent E1 payload and receiver oracle; no UI or global input APIs."""
import hashlib
import json
import re
import struct
from pathlib import Path


def _require(condition, message):
    if not condition:
        raise ValueError(message)


def _uint(value, label):
    _require(type(value) is int and value >= 0, label)
    return value


def _read(path, limit):
    _require(path.is_file() and not path.is_symlink(), f'missing or linked file: {path.name}')
    _require(path.stat().st_size <= limit, f'oversized file: {path.name}')
    with path.open('rb') as stream:
        data = stream.read(limit + 1)
    _require(len(data) <= limit, f'oversized file: {path.name}')
    return data


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def validate_console_window(directory, *, expected_role, expected_generation):
    """Validate the bounded F-fixture console, excluding only right blank padding."""
    directory = Path(directory)
    metadata = json.loads(_read(directory / 'capture.bin.console.json', 65536))
    _require(metadata.get('schema') == 'termforge-console-window-v1' and
             metadata.get('role') == expected_role and
             _uint(metadata.get('generation'), 'console generation') == _uint(expected_generation, 'expected generation'),
             'console identity')
    columns = _uint(metadata.get('columns'), 'console columns')
    rows = _uint(metadata.get('rows'), 'console rows')
    top = _uint(metadata.get('top'), 'console top')
    _require(0 < columns <= 500 and 0 < rows <= 200, 'console dimensions')
    cursor_column = _uint(metadata.get('cursor_column'), 'console cursor column')
    cursor_row = _uint(metadata.get('cursor_row'), 'console cursor row')
    _require(cursor_column < columns and top <= cursor_row < top + rows, 'console cursor outside captured window')
    counts = metadata.get('row_units')
    _require(type(counts) is list and len(counts) == rows, 'console row count')
    for count in counts:
        _require(type(count) is int and 0 <= count <= columns, 'console row UTF-16 bound')
    payload = _read(directory / 'capture.bin.console.bin', 200000)
    _require(len(payload) % 2 == 0 and metadata.get('text_sha256') == _sha(payload), 'console text hash/encoding')
    text = payload.decode('utf-16-le')
    _require(_uint(metadata.get('utf16_units'), 'console UTF-16 units') == len(text) == sum(counts),
             'console UTF-16 totals')
    lines = []
    offset = 0
    for count in counts:
        line = text[offset:offset + count]
        _require('\x00' not in line, 'console contains an unmapped cell')
        lines.append(line.rstrip(' '))
        offset += count
    cell_count = _uint(metadata.get('cell_count'), 'console cell count')
    _require(cell_count == columns * rows and _uint(metadata.get('cell_stride'), 'console cell stride') == 4,
             'console cell metadata')
    raw_cells = _read(directory / 'capture.bin.cells.bin', 400000)
    _require(len(raw_cells) == cell_count * 4 and metadata.get('cell_sha256') == _sha(raw_cells),
             'console cell bytes/hash')
    cell_lines = []
    for row in range(rows):
        cells = list(struct.iter_unpack('<HH', raw_cells[row * columns * 4:(row + 1) * columns * 4]))
        text = []
        for column, (codepoint, attributes) in enumerate(cells):
            if attributes & 0x100:
                _require(column + 1 < columns and cells[column + 1][1] & 0x200,
                         'console leading cell lacks its trailing cell')
            if attributes & 0x200:
                _require(column > 0 and cells[column - 1][1] & 0x100,
                         'console trailing cell lacks its leading cell')
                continue
            _require(codepoint != 0, 'console contains an unmapped cell')
            text.append(chr(codepoint))
        cell_lines.append(''.join(text).rstrip(' '))
    return dict(columns=columns, rows=rows, top=top, cursor_column=cursor_column,
                cursor_row=cursor_row, lines=lines, cell_lines=cell_lines)


def _validate_receiver(manifest, receiver, raw):
    units = len(raw.decode('utf-8').encode('utf-16-le')) // 2
    summary = manifest.get('receiver', {})
    _require(summary.get('joined') is True and _uint(summary.get('error'), 'receiver error') == 0 and
             _uint(summary.get('units'), 'units') == units and _uint(summary.get('bytes'), 'bytes') == len(raw) and
             summary.get('sha256') == _sha(raw), 'receiver manifest')
    _require(receiver.get('schema') == 'termforge-raw-console-receiver-v1' and
             receiver.get('state') == 'stopped' and receiver.get('stop_requested') is True and
             receiver.get('worker_joined') is True and receiver.get('failure_step') == '' and
             receiver.get('unexpected_cancel_observed') is False, 'receiver cleanup')
    for key in ('worker_error', 'unexpected_cancel_error', 'last_wait_result', 'last_wait_error'):
        _require(_uint(receiver.get(key), key) == 0, f'receiver {key}')
    _require(_uint(receiver.get('received_units'), 'received units') == units and
             _uint(receiver.get('saved_bytes'), 'saved bytes') == len(raw), 'receiver actual totals')
    reads = _uint(receiver.get('reads_entered'), 'reads entered')
    _require(reads >= (2 if raw else 1) and reads == _uint(receiver.get('reads_completed'), 'reads completed'), 'receiver read join')
    calls = _uint(receiver.get('cancel_calls'), 'cancel calls')
    successes = _uint(receiver.get('cancel_successes'), 'cancel successes')
    _require(calls >= successes >= 1 and _uint(receiver.get('last_cancel_error'), 'cancel error') in (0, 1168), 'receiver cancellation')
    success = receiver.get('last_read_success')
    _require(type(success) is bool and _uint(receiver.get('last_read_count'), 'last read count') == 0, 'receiver final read')
    _require(receiver.get('zero_read_accepted') is success and
             _uint(receiver.get('last_read_error'), 'last read error') == (0 if success else 995), 'receiver final stop semantics')


def validate_rejected_order(directory, *, expected_generation):
    """A rejected append-before-output is a protocol rejection, never normal success."""
    directory = Path(directory)
    manifest = json.loads(_read(directory / 'capture.bin.workflow.json', 65536))
    _require(manifest.get('schema') == 'termforge-workflow-fixture-v1' and
             manifest.get('fixture') == 'r108-console-v1' and manifest.get('role') == 'F2', 'rejection fixture identity')
    _require(_uint(manifest.get('generation'), 'generation') == _uint(expected_generation, 'expected generation'), 'rejection generation')
    _require(manifest.get('status') == 'failed' and manifest.get('failure') == 'workflow command order invalid' and
             manifest.get('worker_joined') is True and manifest.get('stages') == [], 'wrong rejection reason/state')
    for name in ('capture.bin', 'capture.bin.source.bin'):
        _require(_read(directory / name, 16384) == b'', 'rejected order produced unexpected bytes')
    _require(_uint(manifest.get('source_bytes'), 'source bytes') == 0 and
             _uint(manifest.get('records'), 'records') == 0 and manifest.get('source_sha256') == _sha(b''), 'rejected source totals')
    events = [json.loads(line) for line in _read(directory / 'capture.bin.progress.jsonl', 65536).splitlines()]
    expected = [('ready', None), ('event-rejected', 'append'), ('stopped', None)]
    _require(len(events) == len(expected), 'rejection event count')
    previous = 0
    for index, (event, (name, phase)) in enumerate(zip(events, expected), 1):
        _require(event.get('schema') == 'termforge-workflow-progress-v1' and
                 _uint(event.get('seq'), 'seq') == index and event.get('event') == name and
                 event.get('phase') == phase, 'rejection event order')
        elapsed = _uint(event.get('elapsed_ms'), 'elapsed')
        _require(elapsed >= previous, 'rejection clock'); previous = elapsed
        _require(_uint(event.get('source_bytes'), 'event bytes') == 0 and
                 _uint(event.get('records'), 'event records') == 0 and
                 event.get('source_sha256') == _sha(b''), 'rejection event payload')
    receiver = json.loads(_read(directory / 'capture.bin.receiver.json', 65536))
    _validate_receiver(manifest, receiver, b'')
    return dict(role='F2', generation=expected_generation, expected_rejection=True,
                reason='workflow command order invalid', normal_completion=False)


def validate_fixture_case(directory, *, expected_role, expected_generation, expected_input, expected_complete=True):
    """Validate data-only cases. Transport/Job checks belong to host audit."""
    flood = expected_role in ('F1', 'F2', 'F3', 'F4', 'F5')
    _require(expected_role in ('A', 'B') or flood, 'unsupported fixture role')
    _require(type(expected_complete) is bool and (expected_complete or flood), 'unsupported partial case')
    _uint(expected_generation, 'expected generation')
    _require(type(expected_input) is bytes, 'expected input must be bytes')
    directory = Path(directory)
    def document(suffix):
        return json.loads(_read(directory / ('capture.bin' + suffix), 65536))
    manifest = document('.workflow.json')
    receiver = document('.receiver.json')
    source = _read(directory / 'capture.bin.source.bin', 3 * 1024 * 1024)
    raw = _read(directory / 'capture.bin', 16384)
    events = [json.loads(line) for line in _read(directory / 'capture.bin.progress.jsonl', 65536).splitlines()]
    _require(manifest.get('schema') == 'termforge-workflow-fixture-v1' and
             manifest.get('fixture') == 'r108-console-v1', 'fixture identity')
    _require(manifest.get('role') == expected_role and
             _uint(manifest.get('generation'), 'generation') == expected_generation, 'role/generation mismatch')
    _require(manifest.get('status') == 'stopped' and manifest.get('worker_joined') is True, 'fixture not joined/stopped')
    _require(manifest.get('failure', '') == '', 'fixture reported failure')
    specification = [('output', 0, 160), ('append', 160, 20), ('evict', 180, 500)] if expected_role == 'A' else []
    expected = b''.join(f'TF_R108_A_{i:04d} 中文 e\u0301\r\n'.encode('utf-8') for i in range(680)) if expected_role == 'A' else b''
    if flood:
        specification = [('output', 0, 32000)]
        expected = b''.join(f'TF_R108_{expected_role}_{i:05d} 中文 e\u0301'.encode('utf-8').ljust(62, b'.') + b'\r\n' for i in range(32000))
    if expected_complete:
        _require(source == expected, 'producer payload literal mismatch')
    else:
        _require(0 < len(source) < len(expected) and len(source) % 4096 == 0 and
                 source == expected[:len(source)], 'producer incomplete block prefix mismatch')
    _require(raw == expected_input, 'receiver bytes mismatch')
    records = len(source) // 64 if flood else (680 if expected_role == 'A' else 0)
    _require(_uint(manifest.get('source_bytes'), 'source bytes') == len(source) and
             manifest.get('source_sha256') == _sha(source) and
             _uint(manifest.get('records'), 'records') == records, 'producer totals/hash')
    stages = manifest.get('stages')
    _require(type(stages) is list and len(stages) == len(specification), 'stage count')
    expected_events = [('ready', None, 0, 0)]
    offset = count = 0
    for stage, (name, first, requested) in zip(stages, specification):
        payload = source if flood else ''.join(f'TF_R108_A_{i:04d} 中文 e\u0301\r\n' for i in range(first, first + requested)).encode('utf-8')
        actual_records = records if flood else requested
        expected_events.append(('event-start', name, offset, count))
        fields = dict(first=first, requested_records=requested, records=actual_records,
                      bytes=len(payload), source_begin=offset, source_end=offset + len(payload))
        _require(stage.get('name') == name and stage.get('complete') is expected_complete and
                 stage.get('source_sha256') == _sha(payload), 'stage identity/completion/hash')
        for key, value in fields.items():
            _require(_uint(stage.get(key), key) == value, f'stage {key}')
        _require(_uint(stage.get('begin_ms'), 'begin ms') <= _uint(stage.get('end_ms'), 'end ms'), 'stage timing')
        offset += len(payload); count += actual_records
        if expected_complete:
            expected_events.append(('output-complete', name, offset, count))
    stop_offset, stop_count = offset, count
    if not expected_complete:
        _require(len(events) == 4, 'partial event count')
        stop_offset = _uint(events[2].get('source_bytes'), 'stop bytes')
        stop_count = _uint(events[2].get('records'), 'stop records')
        _require(stop_offset <= len(source) and stop_offset % 4096 == 0 and stop_count == stop_offset // 64,
                 'stop snapshot must be completed block prefix')
    expected_events.extend([('stop', None, stop_offset, stop_count), ('stopped', None, offset, count)])
    _require(len(events) == len(expected_events), 'event count')
    previous = 0
    for index, (event, (name, phase, size, total)) in enumerate(zip(events, expected_events), 1):
        _require(event.get('schema') == 'termforge-workflow-progress-v1' and
                 _uint(event.get('seq'), 'event seq') == index and
                 event.get('event') == name and event.get('phase') == phase, 'event order/identity')
        elapsed = _uint(event.get('elapsed_ms'), 'event time')
        _require(elapsed >= previous, 'event time regressed'); previous = elapsed
        _require(_uint(event.get('source_bytes'), 'event bytes') == size and
                 _uint(event.get('records'), 'event records') == total and
                 event.get('source_sha256') == _sha(source[:size]), 'event prefix mismatch')
    for index, stage in enumerate(stages):
        start = events[1 + index * 2]
        end = events[2 + index * 2] if expected_complete else events[-1]
        _require(start['elapsed_ms'] <= stage['begin_ms'] <= stage['end_ms'] <= end['elapsed_ms'], 'stage outside actual events')
    _validate_receiver(manifest, receiver, raw)
    needs_console = flood and expected_complete
    _require(manifest.get('console_window', False) is needs_console, 'console capture scope')
    console = validate_console_window(directory, expected_role=expected_role,
                                      expected_generation=expected_generation) if needs_console else None
    return dict(role=expected_role, generation=expected_generation, records=records,
                source_bytes=len(source), source_sha256=_sha(source), received_bytes=len(raw),
                received_sha256=_sha(raw), events=len(events), ime_verified=False,
                clipboard_verified=False, transport_verified=False, complete=expected_complete,
                console_window=console)


def validate_fixture_host(directory, host):
    """Validate five actual host cases, including transport; outer Job is caller-owned."""
    directory = Path(directory)
    _require(host.get('schema') == 'termforge-workflow-fixture-test-v1' and
             host.get('data_only') is True and host.get('ime_or_global_input_used') is False and
             host.get('passed') is True, 'host identity/status')
    specifications = [('A', 'A', 1, b'', True),
                      ('B', 'B', 2, 'TF_R108_PASTE 中文\rNEXT\r你好'.encode('utf-8'), True),
                      ('F1', 'F1', 3, b'', True), ('F2-stop', 'F2', 4, b'', False),
                      ('F2-order', 'F2', 5, b'', False)]
    cases = host.get('cases')
    _require(type(cases) is list and len(cases) == 5, 'host case count')
    results = []
    for row, (name, role, generation, raw, complete) in zip(cases, specifications):
        rejected = name == 'F2-order'
        _require(row.get('name') == name and row.get('role') == role and
                 _uint(row.get('generation'), 'host generation') == generation, 'host case identity')
        for flag in ('ready', 'stopped_event', 'cleanup_complete', 'passed'):
            _require(row.get(flag) is True, f'host {flag}')
        _require(_uint(row.get('exit_code'), 'host exit') == (2 if rejected else 0) and
                 _uint(row.get('inner_job_active_after'), 'inner Job') == 0 and
                 row.get('transport_file') == 'transport.bin', 'host exit/Job/transport')
        _require(row.get('expected_failure', False) is rejected and
                 row.get('output_complete') is (name in ('A', 'F1')), 'host outcome')
        case = directory / name
        result = validate_rejected_order(case, expected_generation=generation) if rejected else validate_fixture_case(
            case, expected_role=role, expected_generation=generation, expected_input=raw, expected_complete=complete)
        transport = _read(case / 'transport.bin', 4 * 1024 * 1024)
        markers = [f'TF_R108_{role}_READY'.encode()]
        if role == 'A':
            markers += [b'TF_R108_A_OUTPUT_DONE 0160', b'TF_R108_A_APPEND_DONE 0020', b'TF_R108_A_EVICT_DONE 0500']
        elif name == 'F1':
            markers += [b'TF_R108_F1_OUTPUT_DONE 0500']
        if not rejected:
            markers += [f'TF_R108_{role}_STOPPED'.encode()]
        _require(all(transport.count(marker) == 1 for marker in markers), 'transport control marker missing/duplicate')
        positions = [transport.index(marker) for marker in markers]
        _require(positions == sorted(positions), 'transport control order')
        if name in ('B', 'F2-stop', 'F2-order'):
            _require(b'_DONE ' not in transport, 'unexpected transport completion')
        source = _read(case / 'capture.bin.source.bin', 3 * 1024 * 1024)
        observed = re.findall(rb'TF_R108_(?:A|B|F[1-5])_[0-9]{4,5} [^\r\n\x1b]*', transport)
        _require(observed == source.splitlines(), 'transport numbered payload mismatch')
        result.update(name=name, transport_verified=True, transport_bytes=len(transport),
                      transport_sha256=_sha(transport), inner_job_active_after=0)
        results.append(result)
    return results
