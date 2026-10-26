"""Bounded private replay of the observed feed/local-resize order; no timing claim."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


KINDS = {'desired', 'parser_begin', 'parser_end', 'request', 'host_begin', 'host_end', 'feed', 'child_ack'}
TRACE_FIELDS = {'schema', 'observer_order_only', 'qpc_frequency', 'events_overflow',
                'transport_overflow', 'consumed_bytes', 'events'}
EVENT_FIELDS = {'kind', 'qpc', 'offset', 'generation', 'rows', 'columns', 'stage', 'bytes'}
# Schema 1 records the ShellWindow parser from its fixed construction geometry.
INITIAL_GRID = (24, 80)


def integer(value, minimum, maximum):
    return type(value) is int and minimum <= value <= maximum


def replay_plan(trace, transport):
    if (not isinstance(trace, dict) or set(trace) != TRACE_FIELDS or
            type(trace.get('schema')) is not int or trace['schema'] != 1 or
            trace.get('observer_order_only') is not True or
            trace.get('events_overflow') is not False or trace.get('transport_overflow') is not False or
            not integer(trace.get('qpc_frequency'), 1, (1 << 63) - 1)):
        raise ValueError('incomplete or unknown resize trace')
    events = trace.get('events')
    if (not isinstance(events, list) or not events or len(events) > 1024 or
            not isinstance(transport, bytes) or len(transport) > 128 * 1024 or
            type(trace.get('consumed_bytes')) is not int or
            trace['consumed_bytes'] != len(transport)):
        raise ValueError('invalid resize trace size')
    offset, plan, grid, last_qpc = 0, [], INITIAL_GRID, -1
    ui_phase, desired = 'idle', None
    requests, active_host, last_host_generation = {}, None, 0
    for event in events:
        if (not isinstance(event, dict) or set(event) != EVENT_FIELDS or
                type(event.get('kind')) is not str or event['kind'] not in KINDS):
            raise ValueError('unknown resize trace event')
        if (not integer(event['offset'], 0, len(transport)) or event['offset'] != offset or
                not integer(event['bytes'], 0, len(transport))):
            raise ValueError('resize trace byte ordering invalid')
        if (not integer(event['qpc'], 0, (1 << 64) - 1) or event['qpc'] < last_qpc or
                not integer(event['generation'], 0, (1 << 32) - 1) or
                not integer(event['stage'], 0, (1 << 31) - 1)):
            raise ValueError('resize trace clock or generation invalid')
        last_qpc = event['qpc']
        if not integer(event['rows'], 1, 200) or not integer(event['columns'], 1, 500):
            raise ValueError('resize trace dimensions invalid')
        dimensions = event['rows'], event['columns']
        kind = event['kind']
        generation = event['generation']
        if kind != 'feed' and event['bytes']:
            raise ValueError('non-feed trace event owns bytes')
        if kind in ('host_begin', 'host_end'):
            if not generation or event['stage'] != 0:
                raise ValueError('resize trace host identity invalid')
            if kind == 'host_begin':
                if active_host is not None or generation <= last_host_generation:
                    raise ValueError('resize trace host calls overlap or regress')
                # The worker can record begin/end before the UI records request.
                # A coalesced request need not have a host call, and a worker
                # already holding an older generation can run after newer UI
                # requests. Neither observation is a completion fence.
                if generation in requests:
                    expected = requests[generation]
                elif generation == len(requests) + 1 and ui_phase == 'request':
                    expected = grid
                else:
                    raise ValueError('resize trace host generation has no request')
                if dimensions != expected:
                    raise ValueError('resize trace host request dimensions differ')
                active_host = generation, dimensions
                last_host_generation = generation
            else:
                if active_host != (generation, dimensions):
                    raise ValueError('resize trace host boundary invalid')
                active_host = None
            continue
        if kind != 'request' and generation:
            raise ValueError('resize trace UI generation invalid')
        if kind == 'feed':
            count = event['bytes']
            if not count or count > len(transport) - offset or ui_phase != 'idle' or dimensions != grid:
                raise ValueError('resize trace feed partition invalid')
            plan.append(f'F {offset} {count}\n')
            offset += count
        else:
            if kind == 'desired':
                if ui_phase != 'idle':
                    raise ValueError('resize trace desired boundary invalid')
                desired, ui_phase = dimensions, 'parser_begin'
            elif kind == 'parser_begin':
                if ui_phase != 'parser_begin' or dimensions != desired:
                    raise ValueError('resize trace parser boundary invalid')
                ui_phase = 'parser_end'
                plan.append(f'R {dimensions[0]} {dimensions[1]}\n')
            elif kind == 'parser_end':
                if ui_phase != 'parser_end' or dimensions != desired:
                    raise ValueError('resize trace parser boundary invalid')
                grid, ui_phase = dimensions, 'request'
            elif kind == 'request':
                if ui_phase != 'request' or dimensions != grid or generation != len(requests) + 1:
                    raise ValueError('resize trace request boundary invalid')
                requests[generation] = dimensions
                desired, ui_phase = None, 'idle'
            elif kind == 'child_ack':
                if ui_phase != 'idle' or dimensions != grid:
                    raise ValueError('resize trace child acknowledgement geometry invalid')
    if offset != len(transport) or ui_phase != 'idle' or active_host is not None:
        raise ValueError('resize trace does not cover transport')
    return ''.join(plan)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--native-viewport', action='store_true',
                        help='use the explicit native viewport resize policy')
    args = parser.parse_args()
    trace_path = args.capture / 'resize-trace.json'
    transport_path = args.capture / 'resize-trace.transport.bin'
    if trace_path.stat().st_size > 512 * 1024 or transport_path.stat().st_size > 128 * 1024:
        raise ValueError('resize capture files exceed bound')
    trace = json.loads(trace_path.read_text(encoding='utf-8'))
    transport = transport_path.read_bytes()
    plan = replay_plan(trace, transport)
    args.output.mkdir(parents=True, exist_ok=False)
    plan_path = args.output / 'plan.txt'
    plan_path.write_text(plan, encoding='ascii')
    # Full snapshots contain the captured terminal text. They are private artifacts.
    timed_out = False
    try:
        command = [str(args.exe.resolve(strict=True)), str(transport_path.resolve()), str(plan_path.resolve())]
        if args.native_viewport:
            command.append('--native-viewport')
        process = subprocess.run(command, capture_output=True, timeout=10)
        stdout, stderr, exit_code = process.stdout, process.stderr, process.returncode
    except subprocess.TimeoutExpired as error:
        stdout, stderr, exit_code = error.stdout or b'', error.stderr or b'', None
        timed_out = True
    (args.output / 'states.jsonl').write_bytes(stdout)
    (args.output / 'stderr.txt').write_bytes(stderr)
    summary = dict(schema=1, replay_exit_code=exit_code, timed_out=timed_out,
                   resize_policy='native_viewport' if args.native_viewport else 'reflow_history',
                   trace_sha256=sha(trace_path), transport_sha256=sha(transport_path),
                   plan_sha256=sha(plan_path), replay_exe_sha256=sha(args.exe),
                   captured_bytes=len(transport), replay_steps=len(plan.splitlines()),
                   timing_or_native_acceptance=False)
    (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(summary))
    return 0 if exit_code == 0 else 1


if __name__ == '__main__':
    raise SystemExit(main())
