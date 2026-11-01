"""Offline three-state protocol and archive guards; no sockets or original runtime."""
import json
from pathlib import Path
import socket
import tempfile
import unittest
from unittest.mock import patch

from research.ui import resize_state_reference as probe


def naws(columns, rows):
    return bytes([255, 250, 31]) + columns.to_bytes(2, 'big') + rows.to_bytes(2, 'big') + bytes([255, 240])


class Connection:
    def __init__(self, peer, mode):
        self.peer, self.mode = peer, mode
        self.initial, self.fragment, self.sent, self.resize_sent = True, None, [], set()

    def settimeout(self, _):
        pass

    def close(self):
        pass

    def sendall(self, data):
        self.sent.append(data)

    def recv(self, _):
        if self.initial:
            self.initial = False
            return naws(92 if self.mode == 'stale-initial' else 91, 20)
        state, directory = self.peer.state, self.peer.directory
        armed = state.get('resize_armed')
        if armed and armed not in self.resize_sent:
            self.resize_sent.add(armed)
            grid = [66, 20] if armed == 1 else [91, 20]
            if self.mode == 'height-change':
                grid[1] = 21
            if self.mode == 'wrong-grow' and armed == 2:
                grid[0] = 90
            events = [grid]
            if self.mode == 'batched-resize' and armed == 1:
                events = [[67, 20], [65, 20], [66, 20]]
            count = len(state['naws_history']) + (0 if self.mode == 'stale-ready' else len(events))
            (directory / f'resize-ready-{armed}').write_text(json.dumps({'phase': armed, 'grid': grid, 'event_count': count}))
            return b''.join(naws(*event) for event in events)
        record = state['records'][-1]
        if record['cursor_reply'] is not None:
            (directory / f'ack-{record["phase"]}').write_text('observed')
            target = record['phase'] + 1
            if target < 3:
                window = list(probe.WINDOWS[target])
                if self.mode == 'wrong-request':
                    window[0] += 1
                (directory / f'resize-request-{target}').write_text(json.dumps({'phase': target, 'window': window}))
            raise socket.timeout()
        if self.mode == 'unarmed-change':
            return naws(92, 20) + naws(91, 20)
        if self.mode == 'duplicate-reply':
            return b'\x1b[11;2R\x1b[11;2R'
        if self.fragment is not None:
            result, self.fragment = self.fragment, None
            return result
        self.fragment = b'11;2R'
        return b'\x1b['


def receive(mode='normal'):
    with tempfile.TemporaryDirectory() as temporary:
        peer_type = probe.peer_class()
        peer = peer_type.__new__(peer_type)
        peer.directory = Path(temporary)
        (peer.directory / 'fixture-ready').write_text(json.dumps({'grid': [91, 20], 'event_count': 1}))
        peer.state = {'connected': False, 'columns': None, 'rows': None, 'phase': None, 'records': []}
        peer.stop = type('NeverStopped', (), {'is_set': lambda _: False})()
        connection = Connection(peer, mode)
        peer.listener = type('Listener', (), {'accept': lambda _: (connection, ('127.0.0.1', 12345))})()
        peer._serve()
        return peer.state, connection.sent


class ResizeReferenceTests(unittest.TestCase):
    def test_qrect_decode_uses_inclusive_edges_and_rejects_invalid_rectangles(self):
        self.assertEqual(probe.qrect_size([0, 0, 1359, 859]), [1360, 860])
        self.assertEqual(probe.qrect_size([113, -27, 1472, 832]), [1360, 860])
        self.assertEqual(probe.qrect_size([-200, -80, 799, 779]), [1000, 860])
        for raw in (None, [0, 0, -1, 859], [0, 0, 1359, -1], [0, 0, 1359], [False, 0, 1359, 859]):
            with self.subTest(raw=raw), self.assertRaisesRegex(ValueError, 'Invalid public window QRect'):
                probe.qrect_size(raw)

    def test_launcher_reuses_the_protected_entry_without_executing_it(self):
        source = probe.launcher_source()
        self.assertIn('from research.ui.resize_state_reference import peer_class', source)
        self.assertIn('with_name("resize_state_reference.js")', source)
        self.assertNotIn('with_name("r1_menu_wrap.js")', source)
        compile(source, '<offline protected resize launcher>', 'exec')

    def test_source_only_initial_phase_changes_terminal_state(self):
        values = probe.phases(20, 91)
        self.assertEqual(values[1:], [('after-width-shrink', b''), ('after-width-restore', b'')])
        source = values[0][1]
        self.assertTrue(source.endswith(b'\x1b[10;91Hwx'))
        for marker in b'vwx':
            self.assertEqual(source.count(bytes([marker])), 1)
        self.assertIn(b'\x1b[2;1H0123456789\x1b[3;1HANCHOR', source)

    def test_three_phases_with_split_dsr(self):
        state, sent = receive()
        self.assertNotIn('error', state)
        self.assertTrue(state['complete'] and state['closed'])
        self.assertEqual(state['frozen_grids'], [[91, 20], [66, 20], [91, 20]])
        self.assertEqual(state['freeze_event_counts'], [1, 2, 3])
        self.assertEqual(sent[1:], [probe.phases(20, 91)[0][1] + b'\x1b[6n', b'\x1b[6n', b'\x1b[6n'])
        self.assertEqual(state['received_hex'], (b'\x1b[11;2R' * 3).hex())

    def test_all_armed_naws_events_are_retained(self):
        state, _ = receive('batched-resize')
        self.assertNotIn('error', state)
        self.assertEqual(state['naws_history'], [[91, 20], [67, 20], [65, 20], [66, 20], [91, 20]])
        self.assertEqual(state['freeze_event_counts'], [1, 4, 5])

    def test_invalid_protocol_stops_without_further_source(self):
        cases = {'stale-initial': 'Initial stable snapshot is stale',
                 'unarmed-change': 'outside an armed resize', 'height-change': 'row count',
                 'wrong-grow': 'shrink/restore', 'stale-ready': 'stale',
                 'duplicate-reply': 'Unexpected DSR', 'wrong-request': 'immutable window'}
        for mode, message in cases.items():
            with self.subTest(mode=mode):
                state, sent = receive(mode)
                self.assertIn(message, state['error'])
                self.assertTrue(state['closed'])
                self.assertFalse(state.get('complete', False))
                self.assertLess(len(sent), 5)

    def test_archive_identity_uses_exact_new_probe_bundle(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in probe.SOURCE_FILES:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(('inert ' + name).encode())
            hashes = {name: probe.sha(root / name) for name in probe.SOURCE_FILES}
            manifest = {'schema': probe.common.ARCHIVE_SCHEMA, 'source_base_commit': 'a' * 40,
                        'source_files_sha256': hashes}
            (root / probe.common.ARCHIVE_MANIFEST).write_text(json.dumps(manifest))
            with patch.object(probe, 'ROOT', root), patch.object(probe.common.subprocess, 'check_output',
                    side_effect=AssertionError('archive must not invoke Git')):
                identity = probe.source_identity()
                self.assertEqual(identity['source_identity']['kind'], 'source-archive')
                self.assertEqual(identity['source_files_sha256'], hashes)
                (root / probe.SOURCE_FILES[0]).write_bytes(b'drift')
                with self.assertRaisesRegex(ValueError, 'bytes differ'):
                    probe.source_identity()

    def test_old_mode_archive_manifest_is_not_accepted_for_resize(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in probe.SOURCE_FILES:
                path = root / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(b'inert')
            manifest = {'schema': probe.common.ARCHIVE_SCHEMA, 'source_base_commit': 'a' * 40,
                        'source_files_sha256': {name: probe.sha(root / name) for name in probe.SOURCE_FILES[1:]}}
            (root / probe.common.ARCHIVE_MANIFEST).write_text(json.dumps(manifest))
            with patch.object(probe, 'ROOT', root), self.assertRaisesRegex(ValueError, 'file/hash set'):
                probe.source_identity()

    def test_false_plan_cannot_start_or_mark_an_attempt(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'plan.json'
            path.write_text(json.dumps({'execution_authorized': False}))
            with self.assertRaisesRegex(ValueError, 'not authorized'):
                probe.run_plan(path)
            self.assertFalse(path.with_suffix('.attempted').exists())

    def test_changed_window_contract_stops_before_runtime(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'plan.json'
            plan = {**probe.fixed_contract(), 'execution_authorized': True}
            plan['windows'][1][0] += 1
            path.write_text(json.dumps(plan))
            with self.assertRaisesRegex(ValueError, 'contract differs'):
                probe.run_plan(path)
            self.assertFalse(path.with_suffix('.attempted').exists())

    def test_mechanics_checks_real_protocol_fixture_and_rejects_changed_boundaries(self):
        state, _ = receive()
        result = {'status': 'observed-three-resize-states', 'error': None, 'presented_done': None,
                  'full_active_grid_verified': False, 'local_shell_transport_verified': False,
                  'window_rect_format': 'qt-qrect-inclusive-ltrb', 'geometry_checks': [], 'observations': []}
        for record, window in zip(state['records'], probe.WINDOWS):
            raw_rect = [113, -27, 113 + window[0] - 1, -27 + window[1] - 1]
            result['geometry_checks'].append({'phase': record['phase'], 'window_rect': raw_rect,
                                             'window_size': list(window), 'expected_size': list(window)})
            result['observations'].append({**{k: record[k] for k in
                ('phase', 'label', 'frozen_grid', 'cursor_reply', 'cursor_reply_private', 'response_hex')},
                'window_rect': raw_rect, 'window_size': list(window), 'terminal': {'connected': True, 'protocol': 'Telnet',
                    'capacity': 20, 'line_count': 2, 'lines': ['0123456789', 'ANCHOR'], 'markers': []}})
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            values = {'peer.json': state, 'resize-state-result.json': result,
                      'cleanup.json': {'active': 0, 'total': 3, 'terminated': 0},
                      'profile-check.json': {'only_synthetic_session': True, 'session_count': 1}}
            for name, value in values.items():
                (directory / name).write_text(json.dumps(value))
            (directory / 'stderr.txt').write_bytes(b'')
            for index in range(3):
                (directory / f'ack-{index}').write_text('observed')
            self.assertFalse(probe.audit_mechanics(directory)['semantic_acceptance'])
            result['observations'][0]['window_rect'] = [0, 0, 1360, 860]
            (directory / 'resize-state-result.json').write_text(json.dumps(result))
            with self.assertRaisesRegex(ValueError, 'UI geometry'):
                probe.audit_mechanics(directory)
            result['observations'][0]['window_rect'] = result['geometry_checks'][0]['window_rect']
            result['geometry_checks'][0]['window_size'] = [1361, 860]
            (directory / 'resize-state-result.json').write_text(json.dumps(result))
            with self.assertRaisesRegex(ValueError, 'geometry check'):
                probe.audit_mechanics(directory)
            result['geometry_checks'][0]['window_size'] = list(probe.WINDOWS[0])
            (directory / 'resize-state-result.json').write_text(json.dumps(result))
            state['resize_boundaries'][0]['before_event_count'] = 0
            (directory / 'peer.json').write_text(json.dumps(state))
            with self.assertRaisesRegex(ValueError, 'NAWS resize boundary'):
                probe.audit_mechanics(directory)


if __name__ == '__main__':
    unittest.main()
