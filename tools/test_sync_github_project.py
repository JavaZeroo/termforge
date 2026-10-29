"""Offline tests for the GitHub Project sync tool.

The sync policy is exercised against an in-memory backend, so the tests never
touch the network, never need the project scope and never write to GitHub.
"""
from __future__ import annotations

import contextlib
import io
import json
import unittest

from tools import sync_github_project as project

BUILTIN_FIELD_ORDER = {
    'Title': 0,
    'Assignees': 1,
    'Status': 2,
    'Labels': 3,
    'Linked pull requests': 4,
    'Milestone': 5,
    'Repository': 6,
    'Reviewers': 7,
    'Parent issue': 8,
    'Sub-issues progress': 9,
    'Created': 10,
    'Updated': 11,
    'Closed': 12,
}

R2 = 'R2 · 可靠本地终端与开发版'
R5 = 'R5 · 完整复刻验收：先 Windows，后全平台'
ARCHIVE = 'Archive · 早期研究与原型（已迁移）'


def issue(number, *, state='OPEN', labels=(), milestone=R2):
    return project.Issue(
        number=number,
        content_id='I_%d' % number,
        state=state,
        labels=tuple(labels),
        milestone=milestone,
    )


def builtin_fields():
    return {
        'Title': project.Field('F_title', 'Title'),
        'Assignees': project.Field('F_assignees', 'Assignees'),
        'Status': project.Field(
            'F_status',
            'Status',
            (
                project.FieldOption('O_todo', 'Todo', 'GRAY', ''),
                project.FieldOption('O_in_progress', 'In Progress', 'GREEN', ''),
                project.FieldOption('O_done', 'Done', 'PURPLE', ''),
            ),
        ),
        'Labels': project.Field('F_labels', 'Labels'),
        'Milestone': project.Field('F_milestone', 'Milestone'),
        'Sub-issues progress': project.Field('F_subissues', 'Sub-issues progress'),
    }


class FakeBackend:
    """In-memory stand-in for GhBackend that records every mutation."""

    def __init__(self, issues=(), *, drop_fields=()):
        self.issues = list(issues)
        self.drop_fields = tuple(drop_fields)
        self.project = None
        self.field_map = {}
        self.view_list = []
        self.items_list = []
        self.mutations = []
        self._counter = 0

    def _new_id(self, prefix):
        self._counter += 1
        return '%s_%02d' % (prefix, self._counter)

    def user_id(self, login):
        return 'U_owner'

    def repository_id(self, owner, name):
        return 'R_repo'

    def find_project(self, owner, title):
        return self.project

    def create_project(self, owner_id, repository_id, title):
        self.mutations.append(('create_project', title))
        self.project = project.Project(
            'PVT_1', 2, 'https://example.invalid/users/owner/projects/2', None, None, False
        )
        self.field_map = builtin_fields()
        for name in self.drop_fields:
            self.field_map.pop(name, None)
        self.view_list = [project.View('V_1', 'View 1', 'TABLE_LAYOUT', '', ('Title', 'Status', 'Labels'))]
        return self.project

    def update_project(self, project_id, *, readme, short_description, public):
        self.mutations.append(('update_project', readme, short_description, public))
        self.project = project.Project(
            self.project.id, self.project.number, self.project.url, readme, short_description, public
        )

    def fields(self, project_id):
        return dict(self.field_map)

    def create_single_select_field(self, project_id, name, options):
        field = project.Field(
            self._new_id('F'),
            name,
            tuple(project.FieldOption(self._new_id('O'), o.name, o.color, o.description) for o in options),
        )
        self.mutations.append(('create_field', name))
        self.field_map[name] = field
        return field

    def update_single_select_options(self, field_id, options):
        current = next(field for field in self.field_map.values() if field.id == field_id)
        by_name = {option.name: option for option in current.options}
        rebuilt = tuple(
            project.FieldOption(
                by_name[option.name].id if option.name in by_name else self._new_id('O'),
                option.name,
                option.color,
                option.description,
            )
            for option in options
        )
        self.mutations.append(('update_field_options', current.name))
        self.field_map[current.name] = project.Field(current.id, current.name, rebuilt)

    def views(self, project_id):
        return list(self.view_list)

    def _visible(self, field_ids, layout):
        if not project._supports_visible_fields(layout):
            return ()
        by_id = {field.id: name for name, field in self.field_map.items()}
        names = [by_id[field_id] for field_id in field_ids if field_id in by_id]
        # GitHub renders built-in fields first, in its own order, then custom ones.
        names.sort(key=lambda name: BUILTIN_FIELD_ORDER.get(name, 100))
        return tuple(names)

    def create_view(self, project_id, spec, field_ids):
        view = project.View(self._new_id('V'), spec.name, spec.layout, spec.filter, self._visible(field_ids, spec.layout))
        self.mutations.append(('create_view', spec.name))
        self.view_list.append(view)
        return view

    def update_view(self, view_id, spec, field_ids):
        for index, view in enumerate(self.view_list):
            if view.id == view_id:
                updated = project.View(
                    view.id, spec.name, spec.layout, spec.filter, self._visible(field_ids, spec.layout)
                )
                self.mutations.append(('update_view', spec.name))
                self.view_list[index] = updated
                return updated
        raise AssertionError('unknown view %r' % view_id)

    def items(self, project_id):
        return list(self.items_list)

    def list_issues(self):
        return list(self.issues)

    def add_item(self, project_id, content_id):
        source = next(entry for entry in self.issues if entry.content_id == content_id)
        item = project.Item(self._new_id('ITEM'), source.number, {})
        self.items_list.append(item)
        self.mutations.append(('add_item', source.number))
        return item

    def _replace_item(self, updated):
        self.items_list = [updated if existing.id == updated.id else existing for existing in self.items_list]

    def _field_for(self, field_id):
        for name, field in self.field_map.items():
            if field.id == field_id:
                return field
        raise AssertionError('unknown field %r' % field_id)

    def set_single_select(self, project_id, item_id, field_id, option_id):
        item = next(entry for entry in self.items_list if entry.id == item_id)
        field = self._field_for(field_id)
        option_name = next(option.name for option in field.options if option.id == option_id)
        values = dict(item.values)
        values[field.name] = option_name
        self.mutations.append(('set_value', item.number, field.name, option_name))
        self._replace_item(project.Item(item.id, item.number, values))

    def clear_value(self, project_id, item_id, field_id):
        item = next(entry for entry in self.items_list if entry.id == item_id)
        field = self._field_for(field_id)
        values = dict(item.values)
        values.pop(field.name, None)
        self.mutations.append(('clear_value', item.number, field.name))
        self._replace_item(project.Item(item.id, item.number, values))


def run_sync(backend, **overrides):
    options = dict(owner='owner', repo='owner/repo', title=project.PROJECT_TITLE, apply=True)
    options.update(overrides)
    return project.Sync(backend, **options).run()


class DerivationTests(unittest.TestCase):
    def test_type_precedence(self):
        self.assertEqual(project.derive_type(['compatibility', 'bug']), '缺陷回归')
        self.assertEqual(project.derive_type(['research', 'performance']), '性能证据')
        self.assertEqual(project.derive_type(['documentation', 'enhancement']), '功能实现')
        self.assertEqual(project.derive_type(['epic', 'bug']), '路标总项')
        self.assertIsNone(project.derive_type(['good first issue']))
        self.assertIsNone(project.derive_type([]))

    def test_state_precedence(self):
        self.assertEqual(project.derive_state(['blocked', 'needs-breakdown']), '阻塞')
        self.assertEqual(project.derive_state(['needs-breakdown', 'ready-for-agent']), '待拆分')
        self.assertEqual(project.derive_state(['ready-for-agent']), '可开工')
        self.assertEqual(project.derive_state([]), '未标记')

    def test_status_is_only_touched_on_close_reopen_or_insert(self):
        self.assertEqual(project.desired_status('CLOSED', 'In Progress'), 'Done')
        self.assertEqual(project.desired_status('OPEN', None), 'Todo')
        self.assertEqual(project.desired_status('OPEN', 'Done'), 'Todo')
        self.assertEqual(project.desired_status('OPEN', 'In Progress'), 'In Progress')

    def test_milestone_rank(self):
        self.assertEqual(project.milestone_rank('R2 · 可靠本地终端与开发版'), 2)
        self.assertEqual(project.milestone_rank('R10 · 未来'), 10)
        self.assertEqual(project.milestone_rank(ARCHIVE), project.MILESTONE_RANK_ARCHIVE)
        self.assertEqual(project.milestone_rank(None), project.MILESTONE_RANK_NONE)



class SyncTests(unittest.TestCase):
    def test_fresh_project_creates_fields_views_and_items(self):
        backend = FakeBackend([
            issue(6, state='CLOSED', labels=('epic',), milestone='R1 · 基础文字交互复刻'),
            issue(7, labels=('epic',)),
            issue(38, labels=('bug', 'compatibility', 'blocked')),
            issue(155, labels=()),
        ])
        summary = run_sync(backend)

        self.assertEqual(summary['project']['number'], 2)
        self.assertEqual([view.name for view in backend.view_list], [spec.name for spec in project.VIEWS])
        self.assertNotIn('View 1', [view.name for view in backend.view_list])
        self.assertEqual(
            [name for name in backend.field_map if name not in builtin_fields()],
            [project.TYPE_FIELD, project.STATE_FIELD],
        )

        values = {item.number: item.values for item in backend.items_list}
        self.assertEqual(values[6]['Status'], 'Done')
        self.assertEqual(values[7]['Status'], 'Todo')
        self.assertEqual(values[7][project.TYPE_FIELD], '路标总项')
        self.assertEqual(values[38][project.TYPE_FIELD], '缺陷回归')
        self.assertEqual(values[38][project.STATE_FIELD], '阻塞')
        self.assertEqual(values[155][project.STATE_FIELD], '未标记')
        self.assertNotIn(project.TYPE_FIELD, values[155])

        self.assertEqual(summary['stats']['project_created'], 1)
        self.assertEqual(summary['stats']['fields_created'], 2)
        self.assertEqual(summary['stats']['items_added'], 4)
        self.assertEqual(summary['stats']['views_created'], 7)
        self.assertEqual(summary['stats']['views_updated'], 1)

    def test_second_run_changes_nothing(self):
        backend = FakeBackend([
            issue(1, labels=('research',)),
            issue(2, state='CLOSED', labels=('bug', 'ready-for-agent')),
        ])
        run_sync(backend)
        backend.mutations.clear()

        summary = run_sync(backend)

        self.assertEqual(backend.mutations, [])
        self.assertFalse(summary['drift'])
        self.assertEqual(summary['stats']['items_added'], 0)
        self.assertEqual(summary['stats']['values_set'], 0)

    def test_label_change_updates_only_the_affected_item(self):
        backend = FakeBackend([issue(1, labels=('bug',)), issue(2, labels=('research',))])
        run_sync(backend)
        backend.issues = [issue(1, labels=('bug', 'ready-for-agent')), issue(2, labels=('research',))]
        backend.mutations.clear()

        run_sync(backend)

        self.assertEqual(backend.mutations, [('set_value', 1, project.STATE_FIELD, '可开工')])

    def test_type_is_cleared_when_no_theme_label_remains(self):
        backend = FakeBackend([issue(1, labels=('research',))])
        run_sync(backend)
        backend.issues = [issue(1, labels=('good first issue',))]
        backend.mutations.clear()

        run_sync(backend)

        self.assertEqual(backend.mutations, [('clear_value', 1, project.TYPE_FIELD)])

    def test_status_preserves_manual_in_progress_and_reopens_done(self):
        backend = FakeBackend([issue(3, labels=('bug',))])
        run_sync(backend)
        item = backend.items_list[0]
        status = backend.field_map['Status']
        backend.set_single_select('PVT_1', item.id, status.id, status.option_id('In Progress'))
        backend.mutations.clear()

        run_sync(backend)
        self.assertEqual(backend.mutations, [])

        backend.set_single_select('PVT_1', item.id, status.id, status.option_id('Done'))
        backend.mutations.clear()
        run_sync(backend)
        self.assertEqual(backend.mutations, [('set_value', 3, 'Status', 'Todo')])

    def test_check_mode_reports_drift_without_writing(self):
        backend = FakeBackend([issue(1)])
        summary = run_sync(backend, apply=False)

        self.assertTrue(summary['drift'])
        self.assertEqual(backend.mutations, [])
        self.assertIn('create-project ' + project.PROJECT_TITLE, summary['plan'])

    def test_check_mode_on_a_synced_project_reports_no_drift(self):
        backend = FakeBackend([issue(1, labels=('research',))])
        run_sync(backend)
        backend.mutations.clear()

        summary = run_sync(backend, apply=False)

        self.assertFalse(summary['drift'])
        self.assertEqual(backend.mutations, [])

    def test_views_are_created_with_filters_and_layouts(self):
        backend = FakeBackend([issue(1)])
        run_sync(backend)
        views = {view.name: view for view in backend.view_list}

        self.assertEqual(views['R2 当前路标'].layout, 'BOARD_LAYOUT')
        self.assertEqual(views['R2 当前路标'].filter, 'milestone:"R2 · 可靠本地终端与开发版"')
        self.assertEqual(views['路标 Epic'].layout, 'ROADMAP_LAYOUT')
        self.assertEqual(views['已完成'].filter, 'is:closed')
        self.assertEqual(views['总览'].filter, '')
        self.assertEqual(set(views['总览'].fields), {
            'Title', 'Status', project.TYPE_FIELD, project.STATE_FIELD,
            'Milestone', 'Sub-issues progress', 'Assignees', 'Labels',
        })

    def test_unknown_visible_field_is_skipped(self):
        backend = FakeBackend([issue(1)], drop_fields=('Assignees',))
        run_sync(backend)
        overview = {view.name: view for view in backend.view_list}['总览']

        self.assertNotIn('Assignees', overview.fields)
        self.assertIn('Title', overview.fields)

    def test_items_follow_milestone_order(self):
        backend = FakeBackend([
            issue(10, milestone=R5),
            issue(1, milestone=ARCHIVE),
            issue(37, milestone=None),
            issue(6, milestone='R1 · 基础文字交互复刻'),
            issue(20, milestone=R2),
        ])
        run_sync(backend)

        added = [mutation[1] for mutation in backend.mutations if mutation[0] == 'add_item']
        self.assertEqual(added, [6, 20, 10, 1, 37])

    def test_closed_issues_can_be_excluded(self):
        backend = FakeBackend([issue(1), issue(2, state='CLOSED')])
        run_sync(backend, include_closed=False)

        added = [mutation[1] for mutation in backend.mutations if mutation[0] == 'add_item']
        self.assertEqual(added, [1])

    def test_metadata_is_written_once(self):
        backend = FakeBackend([issue(1)])
        run_sync(backend)
        metadata = [mutation for mutation in backend.mutations if mutation[0] == 'update_project']

        self.assertEqual(len(metadata), 1)
        self.assertEqual(metadata[0][1], project.PROJECT_README)
        self.assertIn('issue 的开闭状态、标签', metadata[0][1])
        self.assertTrue(metadata[0][3])


class CliTests(unittest.TestCase):
    def test_apply_returns_zero_and_prints_summary(self):
        backend = FakeBackend([issue(1)])
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            code = project.main([], backend_factory=lambda: backend)

        self.assertEqual(code, 0)
        summary = json.loads(output.getvalue())
        self.assertEqual(summary['mode'], 'apply')
        self.assertEqual(summary['project']['number'], 2)

    def test_check_returns_one_on_drift_and_zero_when_clean(self):
        backend = FakeBackend([issue(1)])
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(project.main(['--check'], backend_factory=lambda: backend), 1)
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(project.main([], backend_factory=lambda: backend), 0)
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(project.main(['--check'], backend_factory=lambda: backend), 0)

    def test_dry_run_returns_zero_and_writes_nothing(self):
        backend = FakeBackend([issue(1)])
        with contextlib.redirect_stdout(io.StringIO()):
            code = project.main(['--dry-run'], backend_factory=lambda: backend)

        self.assertEqual(code, 0)
        self.assertEqual(backend.mutations, [])

    def test_missing_scope_exits_with_two_and_hint(self):
        class ScopeLimited(FakeBackend):
            def user_id(self, login):
                raise project.ScopeError('missing required scopes [project]')

        error = io.StringIO()
        with contextlib.redirect_stderr(error):
            code = project.main([], backend_factory=ScopeLimited)

        self.assertEqual(code, 2)
        self.assertIn('gh auth refresh -s project,read:project', error.getvalue())

    def test_missing_gh_exits_with_two(self):
        class NoGh(FakeBackend):
            def user_id(self, login):
                raise project.PreconditionError('找不到 gh CLI')

        error = io.StringIO()
        with contextlib.redirect_stderr(error):
            code = project.main([], backend_factory=NoGh)

        self.assertEqual(code, 2)
        self.assertIn('前置条件不满足', error.getvalue())

    def test_backend_failure_exits_with_one(self):
        class Broken(FakeBackend):
            def list_issues(self):
                raise project.SyncError('boom')

        error = io.StringIO()
        with contextlib.redirect_stderr(error):
            code = project.main([], backend_factory=Broken)

        self.assertEqual(code, 1)
        self.assertIn('同步失败', error.getvalue())


if __name__ == '__main__':
    unittest.main()
