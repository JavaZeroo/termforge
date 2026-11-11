"""Offline guards for the dual-axis calibration peer contract; no sockets or original runtime."""
import json
from pathlib import Path
import socket
import tempfile
import unittest

from research.ui import resize_bcd_calibration as probe


def naws(columns, rows):
    return bytes([255, 250, 31]) + columns.to_bytes(2, 'big') + rows.to_bytes(2, 'big') + bytes([255, 240])


DSR = b'\x1b[1;1R'


class Connection:
    def __init__(self, peer, mode):
        self.peer, self.mode = peer, mode
        self.initial, self.resize_sent, self.acked, self.sent = True, set(), set(), []

    def settimeout(self, _):
        pass

    def close(self):
        pass

    def sendall(self, data):
        self.sent.append(data)

    def recv(self, _):
        if self.initial:
            self.initial = False
            return naws(80, 24)
        state, directory = self.peer.state, self.peer.directory
        record = state['records'][-1] if state['records'] else None
        if record is not None and record['cursor_reply'] is None:
            return DSR
        if record is not None and record['phase'] not in self.acked:
            self.acked.add(record['phase'])
            (directory / f"ack-{record['phase']}").write_text('observed')
            if record['phase'] in (0, 2):
                target = record['phase'] + 1
                window = list(probe.WINDOWS[target])
                if self.mode == 'wrong-request':
                    window[0] += 1
                (directory / f"resize-request-{target}").write_text(json.dumps({'phase': target, 'window': window}))
            if record['phase'] == 1 and self.mode == 'checkpoint-duplicate':
                return naws(60, 12)
            if record['phase'] == 1 and self.mode == 'unarmed-change':
                return naws(70, 20)
            raise socket.timeout()
        armed = state.get('resize_armed')
        if armed and armed not in self.resize_sent:
            self.resize_sent.add(armed)
            grid = [60, 12] if armed == 1 else [80, 24]
            if self.mode == 'wrong-grid' and armed == 1:
                grid = [60, 24]
            count = len(state['naws_history']) + (0 if self.mode == 'stale-ready' else 1)
            (directory / f"resize-ready-{armed}").write_text(json.dumps(
                {'phase': armed, 'grid': grid, 'event_count': count}))
            return naws(*grid)
        raise socket.timeout()


def receive(mode='normal'):
    with tempfile.TemporaryDirectory() as temporary:
        peer_type = probe.peer_class('calibrate')
        peer = peer_type.__new__(peer_type)
        peer.directory = Path(temporary)
        (peer.directory / 'fixture-ready').write_text(json.dumps({'grid': [80, 24], 'event_count': 1}))
        peer.state = {'connected': False, 'columns': None, 'rows': None, 'phase': None, 'records': []}
        peer.stop = type('NeverStopped', (), {'is_set': lambda _: False})()
        connection = Connection(peer, mode)
        peer.listener = type('Listener', (), {'accept': lambda _: (connection, ('127.0.0.1', 12345))})()
        peer._serve()
        return peer.state, connection.sent


class CalibrationPeerContractTests(unittest.TestCase):
    def test_dual_axis_calibration_completes_with_checkpoint_duplicate(self):
        state, sent = receive('checkpoint-duplicate')
        self.assertNotIn('error', state)
        self.assertTrue(state['complete'] and state['closed'])
        self.assertEqual(state['frozen_grids'], [[80, 24], [60, 12], [60, 12], [80, 24]])
        self.assertEqual(state['freeze_event_counts'], [1, 2, 3, 4])
        self.assertEqual(state['naws_history'], [[80, 24], [60, 12], [60, 12], [80, 24]])
        self.assertEqual(state['naws_size_changes'], [[80, 24], [60, 12], [80, 24]])
        self.assertEqual(state['initial_grid'], [80, 24])
        self.assertEqual(state['expected_size_change_sequence'], [[80, 24], [60, 12], [80, 24]])
        self.assertIs(state['phase2_new_naws_required'], False)
        self.assertEqual(state['frozen_grid_history'], state['frozen_grids'])
        self.assertEqual([event['phase'] for event in state['phase_ready_events']], [1, 2, 3])
        self.assertIs(state['phase_ready_events'][1]['new_naws_required'], False)
        self.assertTrue(all(event['new_naws'] for event in (state['phase_ready_events'][0], state['phase_ready_events'][2])))
        boundaries = state['resize_boundaries']
        self.assertEqual(len(boundaries), 2)
        self.assertEqual(boundaries[0]['before_event_count'], 1)
        self.assertEqual(boundaries[0]['after_event_count'], 2)
        self.assertEqual(boundaries[1]['before_event_count'], 3)
        self.assertEqual(boundaries[1]['after_event_count'], 4)
        self.assertEqual(boundaries[1]['phase2_observed_naws'], [[60, 12]])
        self.assertEqual(sent[1:], [b'\x1b[6n'] * 4)
        self.assertEqual(state['received_hex'], (DSR * 4).hex())

    def test_calibration_completes_without_checkpoint_duplicate(self):
        state, _ = receive()
        self.assertNotIn('error', state)
        self.assertTrue(state['complete'])
        self.assertEqual(state['freeze_event_counts'], [1, 2, 2, 3])
        self.assertEqual(state['naws_size_changes'], [[80, 24], [60, 12], [80, 24]])
        self.assertEqual(state['resize_boundaries'][1]['phase2_observed_naws'], [])

    def test_unexpected_size_change_fails(self):
        cases = {'wrong-grid': 'Unexpected resized grid',
                 'unarmed-change': 'outside an armed resize',
                 'stale-ready': 'stale',
                 'wrong-request': 'immutable window'}
        for mode, message in cases.items():
            with self.subTest(mode=mode):
                state, sent = receive(mode)
                self.assertIn(message, state['error'])
                self.assertTrue(state['closed'])
                self.assertFalse(state.get('complete', False))
                self.assertLess(len(sent), 6)

    def test_mechanics_audit_accepts_contract_and_rejects_tampering(self):
        state, _ = receive('checkpoint-duplicate')
        result = {'status': 'observed-four-bcd-calibration-states', 'case_id': 'calibrate', 'error': None,
                  'presented_done': None, 'full_active_grid_verified': False, 'local_shell_transport_verified': False,
                  'window_rect_format': 'qt-qrect-inclusive-ltrb', 'geometry_checks': [], 'observations': []}
        for record, window in zip(state['records'], probe.WINDOWS):
            raw_rect = [113, -27, 113 + window[0] - 1, -27 + window[1] - 1]
            result['geometry_checks'].append({'phase': record['phase'], 'window_rect': raw_rect,
                                              'window_size': list(window), 'expected_size': list(window)})
            result['observations'].append({**{k: record[k] for k in
                ('phase', 'label', 'frozen_grid', 'cursor_reply', 'cursor_reply_private', 'response_hex')},
                'window_rect': raw_rect, 'window_size': list(window), 'terminal': {'connected': True, 'protocol': 'Telnet',
                    'capacity': record['frozen_grid'][1], 'line_count': 0, 'lines': [], 'markers': []}})
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            values = {'peer.json': state, 'bcd-calibration-result.json': result,
                      'cleanup.json': {'active': 0, 'total': 4, 'terminated': 0},
                      'profile-check.json': {'only_synthetic_session': True, 'session_count': 1}}
            for name, value in values.items():
                (directory / name).write_text(json.dumps(value))
            (directory / 'stderr.txt').write_bytes(b'')
            for index in range(4):
                (directory / f'ack-{index}').write_text('observed')
            mechanics = probe.audit_mechanics(directory, 'calibrate')
            self.assertEqual(mechanics['naws_events'], 4)
            self.assertFalse(mechanics['semantic_acceptance'])
            state['naws_size_changes'] = [[80, 24], [60, 12], [60, 12], [80, 24]]
            (directory / 'peer.json').write_text(json.dumps(state))
            with self.assertRaisesRegex(ValueError, 'size-change sequence'):
                probe.audit_mechanics(directory, 'calibrate')
            state['naws_size_changes'] = [[80, 24], [60, 12], [80, 24]]
            state['phase_ready_events'][1]['new_naws_required'] = True
            (directory / 'peer.json').write_text(json.dumps(state))
            with self.assertRaisesRegex(ValueError, 'phase-ready events'):
                probe.audit_mechanics(directory, 'calibrate')
            state['phase_ready_events'][1]['new_naws_required'] = False
            state['resize_boundaries'][1]['phase2_observed_naws'] = [[80, 24]]
            (directory / 'peer.json').write_text(json.dumps(state))
            with self.assertRaisesRegex(ValueError, 'checkpoint observations'):
                probe.audit_mechanics(directory, 'calibrate')


if __name__ == '__main__':
    unittest.main()
