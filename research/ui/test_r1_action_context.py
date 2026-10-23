"""Pure evidence mutations: never starts a reference app or opens clipboard."""
import copy
import hashlib
import json
import unittest
from research.ui.r1_action_context import ACTIONS, PHASES, summarize_context, export_context, replay_context
from research.ui.r1_summarize import EXPECTED_EXE, EXPECTED_FILES, MULTILINE, compare_runs
from research.ui.r1_vt_fixture import payload


def fixture():
    identity = dict(sha256=EXPECTED_EXE, files_sha256=EXPECTED_FILES, profile='synthetic-local-only',
                    scenario='action-context', clipboard='hosted-disposable-vm', pid=123,
                    script_sha256='script', fixture_sha256='fixture', template_sha256='template')
    final = ('x' + MULTILINE).encode().hex()
    peer = dict(connected=True, closed=True, sent_columns=80, sent_sha256=hashlib.sha256(payload(80)).hexdigest(), received_hex=final)
    rows = []
    for i, phase in enumerate(PHASES):
        query = 'LEFTRIGHT' if i < 4 else '中文'
        rows.append(dict(kind='snapshot', phase=phase, focus='ComboBox' if 1 <= i <= 5 else 'ScintillaEditBase',
                         modal='PasteDialog' if phase == 'paste-confirm' else None,
                         text_state=json.dumps(dict(selection=query)),
                         peer=dict(received_hex='' if i < 7 else ('78' if i < 9 else final))))
    for i, phase in enumerate(PHASES[1:6]):
        rows.append(dict(kind='find_edit', phase=phase, text='LEFTRIGHT' if i < 3 else '中文',
                         selected='LEFTRIGHT' if i < 3 else '',
                         system_clipboard='TF-FIND-SENTINEL' if i < 2 else ('LEFTRIGHT' if i == 2 else '中文')))
    rows += [dict(kind='context_action', action=a, input='Qt key press/release') for a in ACTIONS]
    rows += [dict(kind='clipboard_set', system_clipboard=s) for s in ('TF-FIND-SENTINEL', '中文', 'TF-FIRST\r\nTF-SECOND\nTF-THIRD')]
    rows += [dict(kind='confirmation_action', text='queued Return to actual focused widget', focus='ScintillaEditBase'),
             dict(kind='menu_activation', action='paste', input='QMenu active action plus queued Return key'),
             dict(kind='scenario_complete', coverage='action-context')]
    ordered = []
    for i, phase in enumerate(PHASES):
        ordered += [r for r in rows if r.get('phase') == phase]
        if phase == 'find-copy':
            ordered += [r for r in rows if r.get('kind') == 'clipboard_set' and r.get('system_clipboard') == '中文']
        if i < len(ACTIONS):
            ordered += [r for r in rows if r.get('action') == ACTIONS[i]]
        if phase == 'context-session':
            ordered += [r for r in rows if r.get('kind') == 'clipboard_set' and r.get('system_clipboard') == 'TF-FIND-SENTINEL']
        if phase == 'paste-before':
            ordered += [r for r in rows if r.get('kind') == 'clipboard_set' and r.get('system_clipboard') == 'TF-FIRST\r\nTF-SECOND\nTF-THIRD']
            ordered += [r for r in rows if r.get('kind') == 'menu_activation']
        if phase == 'paste-confirm':
            ordered += [r for r in rows if r.get('kind') == 'confirmation_action']
    ordered += [r for r in rows if r.get('kind') == 'scenario_complete']
    rows = ordered
    result = dict(pid=123, source_sha256='script', messages=[dict(type='send', payload=r) for r in rows])
    evidence = dict(ok=True, data=dict(evidence=dict(operation='instrument_with_frida', evidence_id='one', normalized_result=result)))
    return [identity, evidence, dict(active=0), dict(only_synthetic_session=True, session_count=1), 'script', peer]


class ContextEvidenceTests(unittest.TestCase):
    def public_fixture(self):
        args = fixture()
        for key in ('script_sha256', 'fixture_sha256', 'template_sha256'): args[0][key] = 'a' * 64
        args[4] = 'a' * 64
        args[1]['data']['evidence']['normalized_result']['source_sha256'] = 'a' * 64
        return args

    def test_public_replay_and_private_fields_removed(self):
        args = self.public_fixture()
        args[0]['private_path'] = 'PRIVATE_MARKER'
        result = args[1]['data']['evidence']['normalized_result']
        result['private_dump'] = 'PRIVATE_MARKER'
        for message in result['messages']:
            message['payload']['widgets'] = [{'ptr': 'PRIVATE_MARKER', 'text': 'PRIVATE_MARKER'}]
        result['messages'].insert(0, {'type': 'send', 'payload': {'kind': 'unlisted', 'value': 'PRIVATE_MARKER'}})
        public = export_context(*args)
        self.assertEqual(replay_context(public), summarize_context(*args))
        serialized = json.dumps(public)
        self.assertNotIn('PRIVATE_MARKER', serialized)
        self.assertNotIn('"pid"', serialized)
        self.assertNotIn('widgets', serialized)

    def test_public_rejects_reordered_source_mismatch_and_capacity(self):
        public = export_context(*self.public_fixture())
        for mutation in ('order', 'digest', 'subject', 'capacity'):
            with self.subTest(mutation=mutation):
                value = copy.deepcopy(public)
                result = value['evidence']['data']['evidence']['normalized_result']
                if mutation == 'order':
                    action = next(m for m in result['messages'] if m['payload'].get('action') == 'edit-copy')
                    result['messages'].remove(action); result['messages'].append(action)
                if mutation == 'digest': result['source_sha256'] = 'b' * 64
                if mutation == 'subject': result['subject_sha256'] = 'b' * 64
                if mutation == 'capacity': value['oversized'] = 'x' * 65536
                with self.assertRaises(ValueError): replay_context(value)

    def test_public_export_rejects_unexpected_free_text(self):
        args = self.public_fixture()
        # The core oracle does not require selected text at this phase. The
        # public writer must still reject arbitrary user text there.
        for message in args[1]['data']['evidence']['normalized_result']['messages']:
            row = message['payload']
            if row.get('kind') == 'find_edit' and row.get('phase') == 'find-typed': row['selected'] = 'PRIVATE_MARKER'
        with self.assertRaisesRegex(ValueError, 'synthetic text'): export_context(*args)

    def test_public_export_omits_unconsumed_paste_terminal_selection(self):
        args = self.public_fixture()
        # CI 40025079604 observed the menu gesture selecting the first physical
        # fixture row. These phases' oracle uses modal/focus/peer, not selection.
        selected = 'TF-SOFT:' + 's' * (63 - 12) + 'LEFT\r\n'
        for message in args[1]['data']['evidence']['normalized_result']['messages']:
            row = message['payload']
            if row.get('kind') == 'snapshot' and row.get('phase') in ('paste-confirm', 'paste-enter', 'paste-settled'):
                row['text_state'] = json.dumps({'selection': selected})
        expected = summarize_context(*args)
        public = export_context(*args)
        self.assertEqual(replay_context(public), expected)
        self.assertNotIn('TF-SOFT:', json.dumps(public))
        for message in public['evidence']['data']['evidence']['normalized_result']['messages']:
            row = message['payload']
            if row.get('kind') == 'snapshot' and row['phase'] not in PHASES[1:6]:
                self.assertNotIn('text_state', row)

    def test_public_export_rejects_unknown_initial_modal_class(self):
        args = self.public_fixture()
        rows = args[1]['data']['evidence']['normalized_result']['messages']
        next(row['payload'] for row in rows if row['payload'].get('phase') == 'context-session')['modal'] = 'PRIVATE_MARKER'
        with self.assertRaisesRegex(ValueError, 'modal class'): export_context(*args)

    def test_context_refuses_local_launch_before_preparing_files(self):
        from pathlib import Path
        from research.ui.r1_launch import run
        with self.assertRaisesRegex(ValueError, 'disposable hosted VM'):
            run(Path('unused'), Path('unused'), Path('unused'), 1, scenario='action-context')

    def test_complete_and_repeat(self):
        first = summarize_context(*fixture()); second = copy.deepcopy(first); second['evidence_id'] = 'two'
        self.assertTrue(compare_runs(first, second)['verified'])
        self.assertEqual(first['confirmation']['outcome'], 'accepted')

    def test_return_cancelled_is_observed_not_inferred(self):
        args = fixture(); args[5]['received_hex'] = '78'
        for message in args[1]['data']['evidence']['normalized_result']['messages']:
            row = message['payload']
            if row.get('phase') in ('paste-enter', 'paste-settled'):
                row['peer']['received_hex'] = '78'
        first = summarize_context(*args)
        self.assertEqual(first['confirmation']['outcome'], 'cancelled')
        second = copy.deepcopy(first); second['evidence_id'] = 'two'
        self.assertTrue(compare_runs(first, second)['verified'])
        accepted = summarize_context(*fixture()); accepted['evidence_id'] = 'three'
        with self.assertRaises(ValueError): compare_runs(first, accepted)

    def test_return_partial_duplicate_or_open_dialog_rejected(self):
        for received in (('x' + MULTILINE[:5]).encode().hex(), ('x' + MULTILINE * 2).encode().hex()):
            args = fixture(); args[5]['received_hex'] = received
            for message in args[1]['data']['evidence']['normalized_result']['messages']:
                row = message['payload']
                if row.get('phase') in ('paste-enter', 'paste-settled'): row['peer']['received_hex'] = received
            with self.assertRaises(ValueError): summarize_context(*args)
        args = fixture()
        for message in args[1]['data']['evidence']['normalized_result']['messages']:
            if message['payload'].get('phase') == 'paste-enter': message['payload']['modal'] = 'PasteDialog'
        with self.assertRaises(ValueError): summarize_context(*args)

    def test_reject_missing_or_changed_observation(self):
        for index in range(len(fixture()[1]['data']['evidence']['normalized_result']['messages'])):
            with self.subTest(index=index):
                args = fixture(); del args[1]['data']['evidence']['normalized_result']['messages'][index]
                with self.assertRaises((ValueError, KeyError)):
                    summarize_context(*args)

    def test_reject_leak_late_bytes_default_inference_and_bad_cleanup(self):
        mutations = [lambda a: a[0].update(clipboard='owned-window-station'),
                     lambda a: a[2].update(active=1), lambda a: a[5].update(received_hex='78'),
                     lambda a: a[5].update(closed=False), lambda a: a[5].update(sent_sha256='wrong')]
        for mutate in mutations:
            args = fixture(); mutate(args)
            with self.assertRaises(ValueError): summarize_context(*args)
        for phase in ('find-copy', 'paste-confirm'):
            args = fixture()
            for m in args[1]['data']['evidence']['normalized_result']['messages']:
                if m['payload'].get('phase') == phase and m['payload']['kind'] == 'snapshot':
                    m['payload']['peer']['received_hex'] = 'bad'
            with self.assertRaises(ValueError): summarize_context(*args)
        args = fixture()
        for m in args[1]['data']['evidence']['normalized_result']['messages']:
            if m['payload']['kind'] == 'confirmation_action': m['payload']['text'] = 'default button inferred'
        with self.assertRaises(ValueError): summarize_context(*args)

    def test_reject_action_after_observation(self):
        args = fixture(); messages = args[1]['data']['evidence']['normalized_result']['messages']
        action = next(m for m in messages if m['payload'].get('action') == 'edit-copy')
        messages.remove(action); messages.append(action)
        with self.assertRaises(ValueError): summarize_context(*args)


if __name__ == '__main__':
    unittest.main()
