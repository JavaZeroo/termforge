"""Idempotently build and sync the TermForge delivery project (GitHub Projects v2).

The project is a derived overview: issue open/closed state, labels and
milestones stay authoritative, and every field written here is recomputed from
them. Re-running the tool converges on the desired board instead of appending a
second progress record, so the board cannot drift from the issues for long.

Modes:
  (default)  apply: create/update the project, fields, views, items and values
  --dry-run  print the plan, write nothing
  --check    print the plan and exit 1 when anything drifted, write nothing

The tool talks to GitHub through the authenticated gh CLI (GraphQL API), so it
needs no Python dependency beyond the standard library.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from dataclasses import dataclass, field
from typing import Any, Protocol, Sequence

OWNER = 'JavaZeroo'
REPOSITORY = 'JavaZeroo/termforge'
PROJECT_TITLE = 'TermForge 交付总览'
PROJECT_SHORT_DESCRIPTION = (
    'WindTerm 2.7.0 复刻的 R1–R5 交付总览；issue 的开闭、标签与 milestone 仍是权威，本看板为派生视图。'
)
PROJECT_README = """# TermForge 交付总览

WindTerm 2.7.0 完整复刻的 R1–R5 交付看板。issue 的开闭状态、标签、原生阻塞/父子关系和 milestone 是唯一权威；这里的主类型、执行状态与 Status 都由它们派生，冲突时以 issue 为准。

## 路标

R1 基础文字交互（已验收）→ R2 可靠本地终端与开发版（当前）→ R3 SSH/SFTP 远程日常版 → R4 Windows 高级功能与性能收敛 → R5 完整复刻验收：先 Windows，后全平台。

范围见[总体计划](https://github.com/JavaZeroo/termforge/blob/main/docs/PLAN.md)，当前交付顺序见 [R2 交付路线与验收](https://github.com/JavaZeroo/termforge/blob/main/docs/milestones/R2-local-terminal.md)，任务导航见[任务跟踪](https://github.com/JavaZeroo/termforge/blob/main/docs/TRACKING.md)。

## 字段

| 字段 | 来源 |
|---|---|
| Status | issue 关闭 → Done；重开或首次加入且为空 → Todo；不覆盖人工设置的 In Progress |
| 主类型 | 主题标签优先级 epic > bug > performance > research > enhancement > compatibility > documentation |
| 执行状态 | blocked > needs-breakdown > ready-for-agent > 未标记 |

## 视图

总览（全部条目）、R2 当前路标（milestone 看板）、可开工队列（ready-for-agent）、阻塞与待拆分、研究调查、待归类、路标 Epic（路线图）、已完成。

视图的分组、排序、日期轴和 Web UI 内置工作流（Auto-add to project、Item closed → Done、Item reopened → Todo）由 GitHub 网页端维护，API 不支持，本工具不会改写。

## 同步

看板由仓库工具幂等生成，标签、milestone 或 issue 状态变化后重新同步：

    python tools/sync_github_project.py --check   # 只报告漂移
    python tools/sync_github_project.py           # 写入

需要 gh 已登录且 token 含 project scope（gh auth refresh -s project,read:project）。看板本身不验收任何功能：能力边界见 [docs/FEATURES.md](https://github.com/JavaZeroo/termforge/blob/main/docs/FEATURES.md)。"""

TYPE_FIELD = '主类型'
STATE_FIELD = '执行状态'
STATUS_FIELD = 'Status'

# Milestone ranks keep the backlog readable in the table view: R1 history first,
# then the active roadmap, then archive material, then items without a milestone.
ARCHIVE_MILESTONE_PREFIX = 'Archive'
MILESTONE_RANK_ARCHIVE = 90
MILESTONE_RANK_UNKNOWN = 80
MILESTONE_RANK_NONE = 99


# The GitHub API occasionally drops TLS handshakes from constrained networks;
# a run makes hundreds of requests, so a bounded retry is part of normal work.
RETRY_ATTEMPTS = 6
RETRY_BACKOFF_SECONDS = 2.0


class SyncError(RuntimeError):
    """A failure the CLI reports as a normal error (exit code 1)."""


class TransientError(SyncError):
    """A network hiccup worth retrying; never surfaces to the CLI."""


class PreconditionError(SyncError):
    """The gh CLI, authentication or token scope is missing (exit code 2)."""


class ScopeError(PreconditionError):
    """The token lacks the project scope required by Projects v2."""


@dataclass(frozen=True)
class Option:
    """One single-select value derived from a set of issue labels."""

    name: str
    color: str
    description: str
    labels: tuple[str, ...] = ()


# Order matters: the first option whose label is present wins. The order is the
# documented 主类型 precedence, not an evaluation result.
TYPE_OPTIONS: tuple[Option, ...] = (
    Option('路标总项', 'PURPLE', '标签 epic', ('epic',)),
    Option('缺陷回归', 'RED', '标签 bug', ('bug',)),
    Option('性能证据', 'ORANGE', '标签 performance', ('performance',)),
    Option('研究调查', 'YELLOW', '标签 research', ('research',)),
    Option('功能实现', 'GREEN', '标签 enhancement', ('enhancement',)),
    Option('兼容性', 'BLUE', '标签 compatibility', ('compatibility',)),
    Option('文档', 'GRAY', '标签 documentation', ('documentation',)),
)

# 未标记 has no labels and is the fallback, so it must stay last.
STATE_OPTIONS: tuple[Option, ...] = (
    Option('可开工', 'GREEN', '标签 ready-for-agent', ('ready-for-agent',)),
    Option('阻塞', 'RED', '标签 blocked', ('blocked',)),
    Option('待拆分', 'YELLOW', '标签 needs-breakdown', ('needs-breakdown',)),
    Option('未标记', 'GRAY', '三个执行标签都没有'),
)

STATE_FALLBACK = '未标记'
STATE_PRECEDENCE: tuple[str, ...] = ('blocked', 'needs-breakdown', 'ready-for-agent')


@dataclass(frozen=True)
class ViewSpec:
    """A managed project view. Grouping and sorting stay user-owned."""

    name: str
    layout: str
    filter: str
    fields: tuple[str, ...]


VIEWS: tuple[ViewSpec, ...] = (
    ViewSpec(
        '总览',
        'TABLE_LAYOUT',
        '',
        ('Title', 'Status', TYPE_FIELD, STATE_FIELD, 'Milestone', 'Sub-issues progress', 'Assignees', 'Labels'),
    ),
    ViewSpec(
        'R2 当前路标',
        'BOARD_LAYOUT',
        'milestone:"R2 · 可靠本地终端与开发版"',
        ('Title', TYPE_FIELD, STATE_FIELD, 'Sub-issues progress'),
    ),
    ViewSpec(
        '可开工队列',
        'TABLE_LAYOUT',
        'label:ready-for-agent -label:blocked',
        ('Title', TYPE_FIELD, 'Milestone', 'Sub-issues progress'),
    ),
    ViewSpec(
        '阻塞与待拆分',
        'TABLE_LAYOUT',
        'label:blocked,needs-breakdown',
        ('Title', STATE_FIELD, 'Milestone', TYPE_FIELD),
    ),
    ViewSpec(
        '研究调查',
        'TABLE_LAYOUT',
        'label:research',
        ('Title', TYPE_FIELD, 'Milestone', STATE_FIELD),
    ),
    ViewSpec(
        '待归类',
        'TABLE_LAYOUT',
        '-label:ready-for-agent -label:blocked -label:needs-breakdown',
        ('Title', TYPE_FIELD, 'Milestone'),
    ),
    ViewSpec(
        '路标 Epic',
        'ROADMAP_LAYOUT',
        'label:epic',
        ('Title', 'Status', 'Milestone'),
    ),
    ViewSpec(
        '已完成',
        'TABLE_LAYOUT',
        'is:closed',
        ('Title', TYPE_FIELD, 'Milestone', STATE_FIELD),
    ),
)

# GitHub's default first view; reused as 总览 so a fresh project does not keep an
# untitled table next to the managed views.
DEFAULT_VIEW_NAME = 'View 1'
OVERVIEW_VIEW_NAME = '总览'

COLORS = frozenset({'BLUE', 'GRAY', 'GREEN', 'ORANGE', 'PINK', 'PURPLE', 'RED', 'YELLOW'})
LAYOUTS = frozenset({'TABLE_LAYOUT', 'BOARD_LAYOUT', 'ROADMAP_LAYOUT'})


def derive_type(labels: Sequence[str]) -> str | None:
    """主类型: first matching theme label in precedence order, else no value."""
    for option in TYPE_OPTIONS:
        if any(label in labels for label in option.labels):
            return option.name
    return None


def derive_state(labels: Sequence[str]) -> str:
    """执行状态: blocked > needs-breakdown > ready-for-agent > 未标记."""
    for label in STATE_PRECEDENCE:
        if label in labels:
            for option in STATE_OPTIONS:
                if label in option.labels:
                    return option.name
    return STATE_FALLBACK


def desired_status(state: str, current: str | None) -> str:
    """Status is only touched on close, reopen and first insert."""
    if state == 'CLOSED':
        return 'Done'
    if current is None or current == 'Done':
        return 'Todo'
    return current


def milestone_rank(milestone: str | None) -> int:
    """Order issues by roadmap milestone; unrelated milestones sort last."""
    if not milestone:
        return MILESTONE_RANK_NONE
    text = milestone.strip()
    if len(text) >= 2 and text[0] == 'R' and text[1].isdigit():
        digits = []
        for character in text[1:]:
            if not character.isdigit():
                break
            digits.append(character)
        return int(''.join(digits)) if digits else MILESTONE_RANK_UNKNOWN
    if text.startswith(ARCHIVE_MILESTONE_PREFIX):
        return MILESTONE_RANK_ARCHIVE
    return MILESTONE_RANK_UNKNOWN



@dataclass(frozen=True)
class FieldOption:
    id: str
    name: str
    color: str
    description: str


@dataclass(frozen=True)
class Field:
    id: str
    name: str
    options: tuple[FieldOption, ...] = ()

    def option_id(self, name: str) -> str | None:
        for option in self.options:
            if option.name == name:
                return option.id
        return None


@dataclass(frozen=True)
class View:
    id: str
    name: str
    layout: str
    filter: str
    fields: tuple[str, ...]


@dataclass(frozen=True)
class Project:
    id: str
    number: int
    url: str
    readme: str | None
    short_description: str | None
    public: bool


@dataclass(frozen=True)
class Item:
    id: str
    number: int | None
    values: dict[str, str] = field(default_factory=dict)


@dataclass(frozen=True)
class Issue:
    number: int
    content_id: str
    state: str
    labels: tuple[str, ...]
    milestone: str | None


class Backend(Protocol):
    """The seam between the sync policy and GitHub; tests substitute it."""

    def user_id(self, login: str) -> str: ...

    def repository_id(self, owner: str, name: str) -> str: ...

    def find_project(self, owner: str, title: str) -> Project | None: ...

    def create_project(self, owner_id: str, repository_id: str, title: str) -> Project: ...

    def update_project(self, project_id: str, *, readme: str, short_description: str, public: bool) -> None: ...

    def fields(self, project_id: str) -> dict[str, Field]: ...

    def create_single_select_field(self, project_id: str, name: str, options: Sequence[Option]) -> Field: ...

    def update_single_select_options(self, field_id: str, options: Sequence[Option]) -> None: ...

    def views(self, project_id: str) -> list[View]: ...

    def create_view(self, project_id: str, spec: ViewSpec, field_ids: Sequence[str]) -> View: ...

    def update_view(self, view_id: str, spec: ViewSpec, field_ids: Sequence[str]) -> View: ...

    def items(self, project_id: str) -> list[Item]: ...

    def list_issues(self) -> list[Issue]: ...

    def add_item(self, project_id: str, content_id: str) -> Item: ...

    def set_single_select(self, project_id: str, item_id: str, field_id: str, option_id: str) -> None: ...

    def clear_value(self, project_id: str, item_id: str, field_id: str) -> None: ...


TRANSIENT_MARKERS = (
    'unexpected eof',
    'connection reset',
    'connection refused',
    'tls handshake',
    'i/o timeout',
    'timed out',
    'temporary failure',
    ' 502',
    ' 503',
    ' 429',
)


def _is_transient(message: str) -> bool:
    lowered = message.lower()
    return any(marker in lowered for marker in TRANSIENT_MARKERS)


def _literal(value: str) -> str:
    """A JSON string literal is also a valid GraphQL string literal."""
    return json.dumps(value, ensure_ascii=False)


def _id_literal(value: str) -> str:
    if not value or '"' in value or '\\' in value or '\n' in value:
        raise SyncError(f'非法 GraphQL ID：{value!r}')
    return '"%s"' % value


def _option_literal(option: Option) -> str:
    if option.color not in COLORS:
        raise SyncError(f'非法颜色：{option.color}')
    return '{ name: %s, color: %s, description: %s }' % (
        _literal(option.name),
        option.color,
        _literal(option.description),
    )


def _layout(layout: str) -> str:
    if layout not in LAYOUTS:
        raise SyncError(f'非法视图布局：{layout}')
    return layout


def _supports_visible_fields(layout: str) -> bool:
    """Roadmap views reject visible-field configuration on the Projects v2 API."""
    return layout != 'ROADMAP_LAYOUT'


def _configuration(layout: str, field_ids: Sequence[str]) -> str:
    if not _supports_visible_fields(layout):
        return ''
    return ', configuration:{ visibleFieldIds: [%s] }' % ', '.join(
        _id_literal(field_id) for field_id in field_ids
    )


def _project(node: dict[str, Any]) -> Project:
    return Project(
        id=node['id'],
        number=node['number'],
        url=node['url'],
        readme=node.get('readme'),
        short_description=node.get('shortDescription'),
        public=bool(node.get('public')),
    )


def _field(node: dict[str, Any]) -> Field:
    options = tuple(
        FieldOption(option['id'], option['name'], option['color'], option['description'])
        for option in (node.get('options') or [])
    )
    return Field(node['id'], node['name'], options)


def _view(node: dict[str, Any]) -> View:
    return View(
        id=node['id'],
        name=node['name'],
        layout=node['layout'],
        filter=node.get('filter') or '',
        fields=tuple(field['name'] for field in (node.get('fields') or {}).get('nodes') or []),
    )



class GhBackend:
    """GitHub Projects v2 backend over the authenticated gh CLI."""

    def __init__(self, gh: str = 'gh', repo: str = REPOSITORY) -> None:
        self._gh = gh
        self.repo = repo

    def _run(self, args: Sequence[str], stdin: str | None = None) -> subprocess.CompletedProcess[str]:
        try:
            return subprocess.run(
                list(args),
                input=stdin,
                capture_output=True,
                text=True,
                encoding='utf-8',
            )
        except FileNotFoundError as exc:
            raise PreconditionError('找不到 gh CLI，请安装 GitHub CLI 并运行 gh auth login') from exc

    def _graphql(
        self,
        query: str,
        variables: dict[str, Any] | None = None,
        *,
        retry: bool = True,
    ) -> dict[str, Any]:
        """One request, retried on network hiccups; auth failures are not retried.

        Create-style mutations pass retry=False: a retry after a lost response
        would leave a duplicate project, field or view behind. Their callers only
        issue them when the resource is missing, so the next run converges.
        """
        attempts = RETRY_ATTEMPTS if retry else 1
        for attempt in range(1, attempts + 1):
            try:
                return self._graphql_once(query, variables)
            except TransientError as exc:
                if attempt == attempts:
                    raise SyncError(f'网络错误，重试 {attempts} 次后仍失败：{exc}') from exc
                time.sleep(RETRY_BACKOFF_SECONDS * attempt)

    def _graphql_once(self, query: str, variables: dict[str, Any] | None) -> dict[str, Any]:
        payload = json.dumps({'query': query, 'variables': variables or {}}, ensure_ascii=False)
        proc = self._run([self._gh, 'api', 'graphql', '--input', '-'], stdin=payload)
        body = (proc.stdout or '').strip()
        if proc.returncode != 0:
            message = (body + '\n' + (proc.stderr or '')).strip()
            self._raise_precondition(message)
            if _is_transient(message):
                raise TransientError(message)
            raise SyncError(message or f'gh 退出码 {proc.returncode}')
        try:
            data = json.loads(body)
        except json.JSONDecodeError as exc:
            raise SyncError(f'gh 输出不是 JSON：{body[:400]}') from exc
        errors = data.get('errors') or []
        if errors:
            message = '; '.join(str(error.get('message', '')) for error in errors)
            self._raise_precondition(message)
            raise SyncError(message)
        return data['data']

    @staticmethod
    def _raise_precondition(message: str) -> None:
        lowered = message.lower()
        if (
            'insufficient_scopes' in lowered
            or 'missing required scopes' in lowered
            or 'resource not accessible by personal access token' in lowered
        ):
            raise ScopeError(message.strip())
        if (
            'gh auth login' in lowered
            or 'authentication required' in lowered
            or 'requires authentication' in lowered
            or 'bad credentials' in lowered
            or 'http 401' in lowered
        ):
            raise PreconditionError(message.strip())

    def user_id(self, login: str) -> str:
        data = self._graphql('query($login:String!){ user(login:$login){ id } }', {'login': login})
        return data['user']['id']

    def repository_id(self, owner: str, name: str) -> str:
        data = self._graphql(
            'query($owner:String!,$name:String!){ repository(owner:$owner,name:$name){ id } }',
            {'owner': owner, 'name': name},
        )
        return data['repository']['id']

    def find_project(self, owner: str, title: str) -> Project | None:
        data = self._graphql(
            'query($login:String!){ user(login:$login){ projectsV2(first:100){ nodes'
            '{ id number title url shortDescription readme public } } } }',
            {'login': owner},
        )
        for node in data['user']['projectsV2']['nodes']:
            if node['title'] == title:
                return _project(node)
        return None

    def create_project(self, owner_id: str, repository_id: str, title: str) -> Project:
        data = self._graphql(
            'mutation($owner:ID!,$title:String!,$repo:ID!){ createProjectV2(input:'
            '{ ownerId:$owner, title:$title, repositoryId:$repo })'
            '{ projectV2{ id number url shortDescription readme public } } }',
            {'owner': owner_id, 'title': title, 'repo': repository_id},
        )
        return _project(data['createProjectV2']['projectV2'])

    def update_project(self, project_id: str, *, readme: str, short_description: str, public: bool) -> None:
        self._graphql(
            'mutation($pid:ID!,$readme:String!,$desc:String!,$public:Boolean!){ updateProjectV2(input:'
            '{ projectId:$pid, readme:$readme, shortDescription:$desc, public:$public })'
            '{ projectV2{ id } } }',
            {'pid': project_id, 'readme': readme, 'desc': short_description, 'public': public},
        )

    def fields(self, project_id: str) -> dict[str, Field]:
        data = self._graphql(
            'query($pid:ID!){ node(id:$pid){ ... on ProjectV2 { fields(first:100){ nodes'
            '{ __typename ... on ProjectV2FieldCommon { id name }'
            ' ... on ProjectV2SingleSelectField { options{ id name color description } } } } } } }',
            {'pid': project_id},
        )
        result: dict[str, Field] = {}
        for node in data['node']['fields']['nodes']:
            result[node['name']] = _field(node)
        return result

    def create_single_select_field(self, project_id: str, name: str, options: Sequence[Option]) -> Field:
        mutation = (
            'mutation { createProjectV2Field(input:{ projectId: %s, dataType: SINGLE_SELECT, name: %s, '
            'singleSelectOptions: [%s] }){ projectV2Field{ ... on ProjectV2SingleSelectField'
            '{ id name options{ id name color description } } } } }'
            % (_id_literal(project_id), _literal(name), ', '.join(_option_literal(o) for o in options))
        )
        data = self._graphql(mutation, retry=False)
        return _field(data['createProjectV2Field']['projectV2Field'])

    def update_single_select_options(self, field_id: str, options: Sequence[Option]) -> None:
        mutation = (
            'mutation { updateProjectV2Field(input:{ fieldId: %s, singleSelectOptions: [%s] })'
            '{ projectV2Field{ ... on ProjectV2SingleSelectField{ id } } } }'
            % (_id_literal(field_id), ', '.join(_option_literal(o) for o in options))
        )
        self._graphql(mutation)

    def views(self, project_id: str) -> list[View]:
        data = self._graphql(
            'query($pid:ID!){ node(id:$pid){ ... on ProjectV2 { views(first:50){ nodes'
            '{ id name layout filter fields(first:40){ nodes{ ... on ProjectV2FieldCommon { name } } } } } } } }',
            {'pid': project_id},
        )
        return [_view(node) for node in data['node']['views']['nodes']]

    def create_view(self, project_id: str, spec: ViewSpec, field_ids: Sequence[str]) -> View:
        configuration = _configuration(spec.layout, field_ids)
        mutation = (
            'mutation { createProjectV2View(input:{ projectId: %s, name: %s, layout: %s%s })'
            '{ projectV2View{ id name layout filter } } }'
            % (_id_literal(project_id), _literal(spec.name), _layout(spec.layout), configuration)
        )
        data = self._graphql(mutation, retry=False)
        view = data['createProjectV2View']['projectV2View']
        visible = tuple(spec.fields) if _supports_visible_fields(spec.layout) else ()
        if spec.filter:
            return self.update_view(view['id'], spec, field_ids)
        return View(view['id'], view['name'], view['layout'], view.get('filter') or '', visible)

    def update_view(self, view_id: str, spec: ViewSpec, field_ids: Sequence[str]) -> View:
        configuration = _configuration(spec.layout, field_ids)
        mutation = (
            'mutation { updateProjectV2View(input:{ viewId: %s, name: %s, layout: %s, filter: %s%s })'
            '{ projectV2View{ id name layout filter } } }'
            % (
                _id_literal(view_id),
                _literal(spec.name),
                _layout(spec.layout),
                _literal(spec.filter),
                configuration,
            )
        )
        data = self._graphql(mutation)
        view = data['updateProjectV2View']['projectV2View']
        visible = tuple(spec.fields) if _supports_visible_fields(spec.layout) else ()
        return View(view['id'], view['name'], view['layout'], view.get('filter') or '', visible)

    def items(self, project_id: str) -> list[Item]:
        query = (
            'query($pid:ID!,$after:String){ node(id:$pid){ ... on ProjectV2 { items(first:100, after:$after){'
            ' pageInfo{ hasNextPage endCursor } nodes{ id content{ __typename ... on Issue { number } }'
            ' fieldValues(first:50){ nodes{ ... on ProjectV2ItemFieldSingleSelectValue'
            '{ name field{ ... on ProjectV2FieldCommon { name } } } } } } } } } }'
        )
        result: list[Item] = []
        after: str | None = None
        while True:
            data = self._graphql(query, {'pid': project_id, 'after': after})
            connection = data['node']['items']
            for node in connection['nodes']:
                content = node.get('content') or {}
                number = content.get('number') if content.get('__typename') == 'Issue' else None
                values: dict[str, str] = {}
                for value in node['fieldValues']['nodes']:
                    field_node = value.get('field') or {}
                    if value.get('name') and field_node.get('name'):
                        values[field_node['name']] = value['name']
                result.append(Item(node['id'], number, values))
            if not connection['pageInfo']['hasNextPage']:
                return result
            after = connection['pageInfo']['endCursor']

    def list_issues(self) -> list[Issue]:
        for attempt in range(1, RETRY_ATTEMPTS + 1):
            try:
                return self._list_issues_once()
            except TransientError as exc:
                if attempt == RETRY_ATTEMPTS:
                    raise SyncError(f'网络错误，重试 {RETRY_ATTEMPTS} 次后仍失败：{exc}') from exc
                time.sleep(RETRY_BACKOFF_SECONDS * attempt)

    def _list_issues_once(self) -> list[Issue]:
        proc = self._run([
            self._gh, 'issue', 'list', '--repo', self.repo, '--state', 'all', '--limit', '1000',
            '--json', 'number,id,state,labels,milestone',
        ])
        if proc.returncode != 0:
            message = (proc.stderr or proc.stdout or '').strip()
            if _is_transient(message):
                raise TransientError(message)
            raise SyncError(message or 'gh issue list 失败')
        payload = json.loads(proc.stdout or '[]')
        return [
            Issue(
                number=entry['number'],
                content_id=entry['id'],
                state=entry['state'],
                labels=tuple(label['name'] for label in entry.get('labels') or []),
                milestone=(entry.get('milestone') or {}).get('title'),
            )
            for entry in payload
        ]

    def add_item(self, project_id: str, content_id: str) -> Item:
        data = self._graphql(
            'mutation($pid:ID!,$content:ID!){ addProjectV2ItemById(input:{ projectId:$pid, contentId:$content })'
            '{ item{ id content{ ... on Issue { number } } } } }',
            {'pid': project_id, 'content': content_id},
        )
        item = data['addProjectV2ItemById']['item']
        number = (item.get('content') or {}).get('number')
        return Item(item['id'], number, {})

    def set_single_select(self, project_id: str, item_id: str, field_id: str, option_id: str) -> None:
        self._graphql(
            'mutation($pid:ID!,$item:ID!,$field:ID!,$option:String!){ updateProjectV2ItemFieldValue(input:'
            '{ projectId:$pid, itemId:$item, fieldId:$field, value:{ singleSelectOptionId:$option } })'
            '{ projectV2Item{ id } } }',
            {'pid': project_id, 'item': item_id, 'field': field_id, 'option': option_id},
        )

    def clear_value(self, project_id: str, item_id: str, field_id: str) -> None:
        self._graphql(
            'mutation($pid:ID!,$item:ID!,$field:ID!){ clearProjectV2ItemFieldValue(input:'
            '{ projectId:$pid, itemId:$item, fieldId:$field }){ projectV2Item{ id } } }',
            {'pid': project_id, 'item': item_id, 'field': field_id},
        )



class Sync:
    """Converges one project on the desired derived state."""

    def __init__(
        self,
        backend: Backend,
        *,
        owner: str = OWNER,
        repo: str = REPOSITORY,
        title: str = PROJECT_TITLE,
        include_closed: bool = True,
        apply: bool = True,
        skip_views: bool = False,
    ) -> None:
        if '/' not in repo:
            raise SyncError(f'仓库必须写成 owner/name：{repo}')
        self.backend = backend
        self.owner = owner
        self.repo_name = repo.split('/', 1)[1]
        self.title = title
        self.include_closed = include_closed
        self.apply = apply
        self.skip_views = skip_views
        self.plan: list[str] = []
        self.stats: dict[str, int] = {
            'project_created': 0,
            'metadata_updated': 0,
            'fields_created': 0,
            'fields_updated': 0,
            'views_created': 0,
            'views_updated': 0,
            'items_added': 0,
            'values_set': 0,
            'values_cleared': 0,
            'items_unchanged': 0,
        }
        self.fields: dict[str, Field] = {}

    def run(self) -> dict[str, Any]:
        issues = sorted(self.backend.list_issues(), key=lambda issue: (milestone_rank(issue.milestone), issue.number))
        if not self.include_closed:
            issues = [issue for issue in issues if issue.state != 'CLOSED']

        owner_id = self.backend.user_id(self.owner)
        project = self.backend.find_project(self.owner, self.title)

        if project is None:
            if not self.apply:
                self.plan.append(f'create-project {self.title}')
                self.plan.append('update-project-metadata')
                self.plan.append(f'create-field {TYPE_FIELD}')
                self.plan.append(f'create-field {STATE_FIELD}')
                if not self.skip_views:
                    for spec in VIEWS:
                        self.plan.append(f'create-view {spec.name}')
                self.plan.append(f'add-items {len(issues)}')
                return self.summary(None)
            repository_id = self.backend.repository_id(self.owner, self.repo_name)
            project = self.backend.create_project(owner_id, repository_id, self.title)
            self.stats['project_created'] += 1

        self._sync_metadata(project)
        self.fields = dict(self.backend.fields(project.id))
        self._ensure_field(project, TYPE_FIELD, TYPE_OPTIONS)
        self._ensure_field(project, STATE_FIELD, STATE_OPTIONS)
        if not self.skip_views:
            self._ensure_views(project)
        self._sync_items(project, issues)
        return self.summary(project)

    def _sync_metadata(self, project: Project) -> None:
        if (
            (project.readme or '').strip() == PROJECT_README.strip()
            and (project.short_description or '').strip() == PROJECT_SHORT_DESCRIPTION.strip()
            and project.public
        ):
            return
        self._record('update-project-metadata')
        if self.apply:
            self.backend.update_project(
                project.id,
                readme=PROJECT_README,
                short_description=PROJECT_SHORT_DESCRIPTION,
                public=True,
            )
        self.stats['metadata_updated'] += 1

    def _ensure_field(self, project: Project, name: str, options: Sequence[Option]) -> None:
        current = self.fields.get(name)
        desired = tuple((option.name, option.color, option.description) for option in options)
        if current is None:
            self._record(f'create-field {name}')
            if self.apply:
                self.fields[name] = self.backend.create_single_select_field(project.id, name, options)
            else:
                self.fields[name] = Field(
                    id='',
                    name=name,
                    options=tuple(FieldOption('', o.name, o.color, o.description) for o in options),
                )
            self.stats['fields_created'] += 1
            return
        actual = tuple((option.name, option.color, option.description) for option in current.options)
        if actual == desired:
            return
        self._record(f'update-field {name}')
        if self.apply:
            self.backend.update_single_select_options(current.id, options)
            self.fields[name] = self.backend.fields(project.id)[name]
        else:
            self.fields[name] = Field(
                id=current.id,
                name=name,
                options=tuple(FieldOption('', o.name, o.color, o.description) for o in options),
            )
        self.stats['fields_updated'] += 1

    def _ensure_views(self, project: Project) -> None:
        existing = {view.name: view for view in self.backend.views(project.id)}
        for spec in VIEWS:
            field_ids = [self.fields[name].id for name in spec.fields if name in self.fields]
            visible = (
                tuple(name for name in spec.fields if name in self.fields)
                if _supports_visible_fields(spec.layout)
                else ()
            )
            view = existing.get(spec.name)
            if view is None and spec.name == OVERVIEW_VIEW_NAME and DEFAULT_VIEW_NAME in existing:
                view = existing.pop(DEFAULT_VIEW_NAME)
                self._record(f'rename-view {DEFAULT_VIEW_NAME} -> {spec.name}')
                if self.apply:
                    self.backend.update_view(view.id, spec, field_ids)
                self.stats['views_updated'] += 1
                continue
            if view is None:
                self._record(f'create-view {spec.name}')
                if self.apply:
                    self.backend.create_view(project.id, spec, field_ids)
                self.stats['views_created'] += 1
                continue
            if (
                view.layout == spec.layout
                and view.filter == spec.filter
                # GitHub returns built-in fields in its own order and always
                # exposes default fields on roadmap views, so compare the set
                # and ignore roadmap columns entirely.
                and (not visible or set(view.fields) == set(visible))
            ):
                continue
            self._record(f'update-view {spec.name}')
            if self.apply:
                self.backend.update_view(view.id, spec, field_ids)
            self.stats['views_updated'] += 1

    def _sync_items(self, project: Project, issues: Sequence[Issue]) -> None:
        existing = {item.number: item for item in self.backend.items(project.id) if item.number is not None}
        for issue in issues:
            item = existing.get(issue.number)
            if item is None:
                self._record(f'add-item #{issue.number}')
                if not self.apply:
                    continue
                item = self.backend.add_item(project.id, issue.content_id)
                existing[issue.number] = item
                self.stats['items_added'] += 1
            changed = self._sync_values(project, item, issue)
            if not changed:
                self.stats['items_unchanged'] += 1

    def _sync_values(self, project: Project, item: Item, issue: Issue) -> bool:
        desired: list[tuple[str, str | None]] = []
        if STATUS_FIELD in self.fields:
            desired.append((STATUS_FIELD, desired_status(issue.state, item.values.get(STATUS_FIELD))))
        if TYPE_FIELD in self.fields:
            desired.append((TYPE_FIELD, derive_type(issue.labels)))
        if STATE_FIELD in self.fields:
            desired.append((STATE_FIELD, derive_state(issue.labels)))

        changed = False
        for field_name, option_name in desired:
            field = self.fields[field_name]
            current = item.values.get(field_name)
            if option_name is None:
                if current is None:
                    continue
                self._record(f'clear-field {field_name} #{issue.number}')
                if self.apply:
                    self.backend.clear_value(project.id, item.id, field.id)
                self.stats['values_cleared'] += 1
                changed = True
                continue
            if current == option_name:
                continue
            option_id = field.option_id(option_name)
            if option_id is None:
                raise SyncError(f'字段 {field_name} 缺少选项 {option_name}')
            self._record(f'set-field {field_name}={option_name} #{issue.number}')
            if self.apply:
                self.backend.set_single_select(project.id, item.id, field.id, option_id)
            self.stats['values_set'] += 1
            changed = True
        return changed

    def _record(self, description: str) -> None:
        """Record the step; callers gate the mutation on apply mode themselves."""
        self.plan.append(description)

    def summary(self, project: Project | None) -> dict[str, Any]:
        return {
            'project': None
            if project is None
            else {
                'title': self.title,
                'number': project.number,
                'url': project.url,
                'public': project.public,
            },
            'applied': self.apply,
            'plan': list(self.plan),
            # In apply mode the plan is the executed audit trail, so it does not
            # describe outstanding drift; --check uses it for exactly that.
            'drift': (not self.apply) and bool(self.plan),
            'stats': dict(self.stats),
        }


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description='幂等同步 JavaZeroo/termforge 的 GitHub Project 交付总览；issue 是权威，本工具只写派生字段。',
    )
    parser.add_argument('--owner', default=OWNER, help='项目归属用户，默认 %(default)s')
    parser.add_argument('--repo', default=REPOSITORY, help='owner/name，默认 %(default)s')
    parser.add_argument('--title', default=PROJECT_TITLE, help='项目标题，默认 %(default)s')
    parser.add_argument('--no-closed-issues', action='store_true', help='只同步未关闭 issue')
    parser.add_argument('--skip-views', action='store_true', help='不创建或更新视图')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--dry-run', action='store_true', help='只打印计划，不写入')
    mode.add_argument('--check', action='store_true', help='只报告漂移；有漂移时退出码 1')
    return parser.parse_args(argv)


def _configure_output() -> None:
    """Keep Chinese diagnostics and JSON identical across consoles and CI."""
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, 'reconfigure'):
            stream.reconfigure(encoding='utf-8')


def main(argv: Sequence[str] | None = None, backend_factory: Any = None) -> int:
    _configure_output()
    args = parse_args(argv)
    factory = backend_factory or (lambda: GhBackend(repo=args.repo))
    mode = 'apply'
    if args.dry_run:
        mode = 'dry-run'
    elif args.check:
        mode = 'check'
    try:
        sync = Sync(
            factory(),
            owner=args.owner,
            repo=args.repo,
            title=args.title,
            include_closed=not args.no_closed_issues,
            apply=mode == 'apply',
            skip_views=args.skip_views,
        )
        summary = sync.run()
    except ScopeError as exc:
        print(f'缺少 project scope：{exc}', file=sys.stderr)
        print('请先运行：gh auth refresh -s project,read:project', file=sys.stderr)
        return 2
    except PreconditionError as exc:
        print(f'前置条件不满足：{exc}', file=sys.stderr)
        return 2
    except SyncError as exc:
        print(f'同步失败：{exc}', file=sys.stderr)
        return 1

    summary['mode'] = mode
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    if mode == 'check' and summary['drift']:
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
