"""Strict synthetic reference action ownership; no runtime or clipboard access."""
import hashlib
import json

PHASES = ('context-session', 'find-typed', 'find-all', 'find-copy', 'find-paste',
          'find-refocus', 'find-return', 'paste-before', 'paste-confirm', 'paste-enter', 'paste-settled')
ACTIONS = ('terminal-find', 'edit-all', 'edit-copy', 'edit-paste', 'edit-find', 'edit-escape', 'terminal-type')


def summarize_context(identity, evidence, cleanup, profile, script_digest, peer):
    from research.ui.r1_summarize import EXPECTED_EXE, EXPECTED_FILES, MULTILINE, require
    from research.ui.r1_vt_fixture import payload
    require(identity.get('sha256') == EXPECTED_EXE and identity.get('files_sha256') == EXPECTED_FILES, 'Unpinned target')
    require(identity.get('scenario') == 'action-context' and identity.get('profile') == 'synthetic-local-only'
            and identity.get('clipboard') == 'hosted-disposable-vm', 'Wrong context scope')
    require(cleanup.get('active') == 0 and peer.get('closed') is True, 'Owned resources remain')
    require(profile == {'only_synthetic_session': True, 'session_count': 1}, 'Profile scope changed')
    require(evidence.get('ok') is True and evidence.get('data', {}).get('cleanup_error') is None, 'REA failed')
    record = evidence['data']['evidence']; result = record['normalized_result']
    public = identity.get('schema') == 'termforge.r1-context-public-identity.v1'
    subject = 'subject_sha256' if public else 'pid'
    require(identity.get(subject) is not None and record.get('operation') == 'instrument_with_frida'
            and result.get(subject) == identity.get(subject), 'Wrong observation')
    require(result.get('source_sha256') == identity.get('script_sha256') == script_digest, 'Script identity mismatch')
    require(not result.get('messages_truncated') and result.get('cleanup_error') is None, 'Incomplete evidence')
    messages = result['messages']
    require(not any(m.get('type') == 'error' or m.get('payload', {}).get('error') for m in messages), 'Probe failed')
    rows = [m['payload'] for m in messages if m.get('type') == 'send']
    snapshots = [r for r in rows if r.get('kind') == 'snapshot']
    require([r['phase'] for r in snapshots] == list(PHASES), 'Missing/reordered/duplicate phase')
    phases = {r['phase']: r for r in snapshots}
    # Event order matters: observations must follow the action they substantiate.
    def position(kind, value, field):
        matches = [i for i, row in enumerate(rows) if row.get('kind') == kind and row.get(field) == value]
        require(len(matches) == 1, 'Missing/duplicate ordered event')
        return matches[0]
    for action, before, after in zip(ACTIONS,
                                    ('context-session', 'find-typed', 'find-all', 'find-copy', 'find-paste', 'find-refocus', 'find-return'),
                                    ('find-typed', 'find-all', 'find-copy', 'find-paste', 'find-refocus', 'find-return', 'paste-before')):
        require(position('snapshot', before, 'phase') < position('context_action', action, 'action') <
                position('snapshot', after, 'phase'), 'Action/observation order differs')
    columns = peer.get('sent_columns')
    require(peer.get('connected') and not peer.get('error') and isinstance(columns, int) and 40 <= columns <= 300, 'Invalid peer')
    require(peer.get('sent_sha256') == hashlib.sha256(payload(columns)).hexdigest(), 'Unexpected VT fixture')
    edits = [r for r in rows if r.get('kind') == 'find_edit']
    require([r['phase'] for r in edits] == list(PHASES[1:6]), 'Missing edit observations')
    expected = [('LEFTRIGHT', None, 'TF-FIND-SENTINEL'), ('LEFTRIGHT', 'LEFTRIGHT', 'TF-FIND-SENTINEL'),
                ('LEFTRIGHT', 'LEFTRIGHT', 'LEFTRIGHT'), ('中文', None, '中文'), ('中文', None, '中文')]
    for edit, (text, selected, clipboard) in zip(edits, expected):
        require(edit.get('text') == text and edit.get('system_clipboard') == clipboard, 'Find edit/clipboard differs')
        require(selected is None or edit.get('selected') == selected, 'Edit Select All/Copy differs')
        phase = phases[edit['phase']]
        require(phase.get('focus') == 'ComboBox' and phase.get('modal') is None, 'Find focus differs')
        require(json.loads(phase['text_state'])['selection'] == text, 'Find target differs')
        index = PHASES.index(edit['phase'])
        require(position('snapshot', edit['phase'], 'phase') < rows.index(edit) <
                position('snapshot', PHASES[index + 1], 'phase'), 'Edit observation order differs')
    actions = [r for r in rows if r.get('kind') == 'context_action']
    require([r.get('action') for r in actions] == list(ACTIONS)
            and all(r.get('input') == 'Qt key press/release' for r in actions), 'Action route differs')
    require([r.get('system_clipboard') for r in rows if r.get('kind') == 'clipboard_set'] ==
            ['TF-FIND-SENTINEL', '中文', 'TF-FIRST\r\nTF-SECOND\nTF-THIRD'], 'Clipboard fixture differs')
    require(position('context_action', 'terminal-find', 'action') < position('clipboard_set', 'TF-FIND-SENTINEL', 'system_clipboard') <
            position('snapshot', 'find-typed', 'phase'), 'Find sentinel order differs')
    require(position('find_edit', 'find-copy', 'phase') < position('clipboard_set', '中文', 'system_clipboard') <
            position('context_action', 'edit-paste', 'action'), 'Find paste fixture order differs')
    require(phases['find-return'].get('focus') == 'ScintillaEditBase' and phases['find-return'].get('modal') is None, 'Escape did not return')
    for name in PHASES[:7]:
        require(phases[name].get('peer', {}).get('received_hex') == '', 'Edit input leaked to PTY')
    for name in ('paste-before', 'paste-confirm'):
        require(phases[name].get('peer', {}).get('received_hex') == '78', 'Paste sent before Return')
    require(phases['paste-confirm'].get('modal') == 'PasteDialog', 'No confirmation dialog')
    confirmations = [r for r in rows if r.get('kind') == 'confirmation_action']
    require(len(confirmations) == 1 and confirmations[0].get('text') == 'queued Return to actual focused widget'
            and isinstance(confirmations[0].get('focus'), str) and confirmations[0]['focus']
            and confirmations[0].get('focus') == phases['paste-confirm'].get('focus'), 'No actual Return route')
    require(position('snapshot', 'paste-confirm', 'phase') < rows.index(confirmations[0]) <
            position('snapshot', 'paste-enter', 'phase'), 'Return/observation order differs')
    menus = [r for r in rows if r.get('kind') == 'menu_activation']
    require(len(menus) == 1 and menus[0].get('action') == 'paste'
            and menus[0].get('input') == 'QMenu active action plus queued Return key', 'Paste entry differs')
    require(position('snapshot', 'paste-before', 'phase') <
            position('clipboard_set', 'TF-FIRST\r\nTF-SECOND\nTF-THIRD', 'system_clipboard') < rows.index(menus[0]) <
            position('snapshot', 'paste-confirm', 'phase'), 'Paste entry order differs')
    # Measure Return's result; default-button metadata does not establish it.
    final = peer.get('received_hex')
    accepted = ('x' + MULTILINE).encode().hex()
    require(final in ('78', accepted), 'Partial, duplicate or unexpected Return bytes')
    outcome = 'accepted' if final == accepted else 'cancelled'
    for name in ('paste-enter', 'paste-settled'):
        require(phases[name].get('modal') is None and phases[name].get('peer', {}).get('received_hex') == final, 'Return paste differs')
    require(peer.get('received_hex') == final, 'Late or missing peer bytes')
    require([r.get('coverage') for r in rows if r.get('kind') == 'scenario_complete'] == ['action-context'], 'No completion')
    require(position('snapshot', 'paste-settled', 'phase') < position('scenario_complete', 'action-context', 'coverage'), 'Premature completion')
    return {'schema': 'termforge.r1-action-context.v1', 'target_sha256': EXPECTED_EXE, 'distribution_sha256': EXPECTED_FILES,
            'fixture_sha256': identity.get('fixture_sha256'), 'template_sha256': identity.get('template_sha256'),
            'evidence_id': record['evidence_id'], 'complete': False, 'context_verified': True,
            'system_clipboard': {'verified': True, 'find_copy': 'LEFTRIGHT', 'find_paste': '中文', 'paste_enter_hex': final},
            'find_edits': [{k: r.get(k) for k in ('phase', 'text', 'selected', 'system_clipboard')} for r in edits],
            'actions': [{k: r.get(k) for k in ('action', 'input')} for r in actions],
            'confirmation': {**{k: confirmations[0].get(k) for k in ('text', 'focus')}, 'outcome': outcome},
            'vt': {'columns': columns, 'sent_sha256': peer['sent_sha256'], 'received_hex': final},
            'cleanup': {'owned_active': 0, 'peer_closed': True},
            'limitations': ['Qt event replay, not physical input or performance evidence.',
                            'Rename, busy and closing contexts remain unobserved.']}


def export_context(identity, evidence, cleanup, profile, script_digest, peer):
    """Export only validated synthetic facts, retaining their original order.

    The subject digest preserves run/observation equality without publishing a
    PID. Source digest is the REA observed digest, not a reconstructed script.
    Unknown message kinds, widgets, pointers, paths and stacks are omitted.
    """
    from research.ui.r1_summarize import require
    summarize_context(identity, evidence, cleanup, profile, script_digest, peer)
    record = evidence['data']['evidence']; result = record['normalized_result']
    evidence_id = record.get('evidence_id')
    require(isinstance(evidence_id, str) and len(evidence_id) <= 128 and
            all(c in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-' for c in evidence_id), 'Unsafe evidence identity')
    def digest(value):
        require(isinstance(value, str) and len(value) == 64 and all(c in '0123456789abcdef' for c in value), 'Invalid public digest')
        return value
    subject = hashlib.sha256((str(identity['pid']) + ':' + evidence_id).encode()).hexdigest()
    run = {key: digest(identity[key]) for key in ('sha256', 'files_sha256', 'script_sha256', 'fixture_sha256', 'template_sha256')}
    run.update(schema='termforge.r1-context-public-identity.v1', subject_sha256=subject,
               scenario='action-context', profile='synthetic-local-only', clipboard='hosted-disposable-vm')
    safe = []
    fields = {
        'find_edit': ('phase', 'text', 'selected', 'system_clipboard'),
        'context_action': ('action', 'input'),
        'clipboard_set': ('system_clipboard',),
        'confirmation_action': ('text', 'focus'),
        'menu_activation': ('action', 'input'),
        'scenario_complete': ('coverage',),
    }
    allowed_text = {'', 'LEFTRIGHT', '中文', 'TF-FIND-SENTINEL', 'TF-FIRST\r\nTF-SECOND\nTF-THIRD',
                    'queued Return to actual focused widget', 'Qt key press/release',
                    'QMenu active action plus queued Return key', 'action-context', 'paste',
                    'ComboBox', 'ScintillaEditBase', 'QPushButton', *PHASES, *ACTIONS}
    for message in result['messages']:
        if message.get('type') != 'send':
            continue
        row = message['payload']; kind = row.get('kind')
        if kind == 'snapshot':
            require(row.get('focus') in ('ComboBox', 'ScintillaEditBase', 'QPushButton'), 'Unexpected public focus')
            require(row.get('modal') in (None, 'PasteDialog'), 'Unexpected public modal class')
            selection = json.loads(row['text_state'])['selection']
            require(selection in ('', 'LEFTRIGHT', '中文'), 'Unexpected public terminal selection')
            value = {k: row[k] for k in ('phase', 'focus', 'modal')}
            value.update(kind=kind, text_state=json.dumps({'selection': selection}, ensure_ascii=False),
                         peer={'received_hex': row['peer']['received_hex']})
        elif kind in fields:
            value = {'kind': kind, **{k: row[k] for k in fields[kind]}}
            require(all(isinstance(value[k], str) and value[k] in allowed_text for k in fields[kind]), 'Unexpected public synthetic text')
        else:
            continue
        safe.append({'type': 'send', 'payload': value})
    observed = dict(subject_sha256=subject, source_sha256=digest(result['source_sha256']),
                    messages_truncated=False, cleanup_error=None, messages=safe)
    normalized = {'ok': True, 'data': {'cleanup_error': None, 'evidence': {
        'operation': 'instrument_with_frida', 'evidence_id': evidence_id, 'normalized_result': observed}}}
    cleaned = {'active': 0}
    peer_public = {key: peer[key] for key in ('connected', 'closed', 'sent_columns', 'sent_sha256', 'received_hex')}
    exported = {'schema': 'termforge.r1-context-ordered-evidence.v1', 'identity': run, 'evidence': normalized,
                'cleanup': cleaned, 'profile': profile, 'script_digest': digest(script_digest), 'peer': peer_public,
                'limitations': ['Subject digest binds the original run and observation without publishing its PID.',
                                'Runtime script bytes are not published; source digests are recorded observations.']}
    require(len(safe) <= 64 and len(json.dumps(exported, ensure_ascii=False).encode()) <= 65536, 'Public evidence exceeds bound')
    replay_context(exported)
    return exported


def replay_context(data):
    """Re-evaluate the complete action oracle from allowlisted ordered evidence."""
    from research.ui.r1_summarize import require
    require(isinstance(data, dict) and data.get('schema') == 'termforge.r1-context-ordered-evidence.v1'
            and data.get('identity', {}).get('schema') == 'termforge.r1-context-public-identity.v1', 'Wrong public evidence schema')
    require(len(json.dumps(data, ensure_ascii=False).encode()) <= 65536, 'Public evidence exceeds bound')
    return summarize_context(*(data[key] for key in ('identity', 'evidence', 'cleanup', 'profile', 'script_digest', 'peer')))
