"""Bounded private replay of the observed feed/local-resize order; no timing claim."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


KINDS = {'desired', 'parser_begin', 'parser_end', 'request', 'host_begin', 'host_end', 'feed', 'child_ack'}


def replay_plan(trace, transport):
    if (not isinstance(trace, dict) or trace.get('schema') != 1 or
            trace.get('observer_order_only') is not True or
            trace.get('events_overflow') is not False or trace.get('transport_overflow') is not False):
        raise ValueError('incomplete or unknown resize trace')
    events = trace.get('events')
    if (not isinstance(events, list) or not events or len(events) > 1024 or
            len(transport) > 128 * 1024 or type(trace.get('consumed_bytes')) is not int or
            trace['consumed_bytes'] != len(transport)):
        raise ValueError('invalid resize trace size')
    offset, plan, resizing = 0, [], None
    for event in events:
        if not isinstance(event, dict) or event.get('kind') not in KINDS:
            raise ValueError('unknown resize trace event')
        if (type(event.get('offset')) is not int or event['offset'] != offset or
                type(event.get('bytes')) is not int or event['bytes'] < 0):
            raise ValueError('resize trace byte ordering invalid')
        kind = event['kind']
        if kind == 'feed':
            count = event['bytes']
            if not count or count > len(transport) - offset or resizing is not None:
                raise ValueError('resize trace feed partition invalid')
            plan.append(f'F {offset} {count}\n')
            offset += count
        else:
            if event['bytes']:
                raise ValueError('non-feed trace event owns bytes')
            if kind == 'parser_begin':
                rows, columns = event.get('rows'), event.get('columns')
                if type(rows) is not int or type(columns) is not int or not 1 <= rows <= 200 or not 1 <= columns <= 500 or resizing is not None:
                    raise ValueError('resize trace dimensions invalid')
                resizing = rows, columns
                plan.append(f'R {rows} {columns}\n')
            elif kind == 'parser_end':
                if resizing != (event.get('rows'), event.get('columns')):
                    raise ValueError('resize trace parser boundary invalid')
                resizing = None
    if offset != len(transport) or resizing is not None:
        raise ValueError('resize trace does not cover transport')
    return ''.join(plan)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--exe', required=True, type=Path)
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
        process = subprocess.run([str(args.exe.resolve(strict=True)), str(transport_path.resolve()),
                                  str(plan_path.resolve())], capture_output=True, timeout=10)
        stdout, stderr, exit_code = process.stdout, process.stderr, process.returncode
    except subprocess.TimeoutExpired as error:
        stdout, stderr, exit_code = error.stdout or b'', error.stderr or b'', None
        timed_out = True
    (args.output / 'states.jsonl').write_bytes(stdout)
    (args.output / 'stderr.txt').write_bytes(stderr)
    summary = dict(schema=1, replay_exit_code=exit_code, timed_out=timed_out,
                   trace_sha256=sha(trace_path), transport_sha256=sha(transport_path),
                   plan_sha256=sha(plan_path), replay_exe_sha256=sha(args.exe),
                   captured_bytes=len(transport), replay_steps=len(plan.splitlines()),
                   timing_or_native_acceptance=False)
    (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(summary))
    return 0 if exit_code == 0 else 1


if __name__ == '__main__':
    raise SystemExit(main())
