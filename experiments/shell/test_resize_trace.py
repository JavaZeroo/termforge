"""A replay must preserve geometry/feed ordering and reject incomplete capture."""
import copy
import unittest

from experiments.shell.resize_trace import replay_plan


def fixture():
    events = [dict(kind='feed', offset=0, bytes=1, rows=24, columns=80),
              dict(kind='desired', offset=1, bytes=0, rows=12, columns=60),
              dict(kind='parser_begin', offset=1, bytes=0, rows=12, columns=60),
              dict(kind='parser_end', offset=1, bytes=0, rows=12, columns=60),
              dict(kind='host_begin', offset=1, bytes=0, rows=12, columns=60, generation=1),
              dict(kind='host_end', offset=1, bytes=0, rows=12, columns=60, generation=1),
              dict(kind='request', offset=1, bytes=0, rows=12, columns=60, generation=1),
              dict(kind='feed', offset=1, bytes=2, rows=12, columns=60)]
    return trace(events)


def trace(events):
    for index, event in enumerate(events):
        event.setdefault('qpc', index + 1)
        event.setdefault('generation', 0)
        event.setdefault('stage', 0)
    return dict(schema=1, observer_order_only=True, qpc_frequency=10000000, events_overflow=False,
                transport_overflow=False, consumed_bytes=3, events=events)


def find(trace, kind):
    return next(event for event in trace['events'] if event['kind'] == kind)


class ResizeTraceTests(unittest.TestCase):
    def test_preserves_feed_split_and_ui_geometry_without_turning_host_observation_into_resize(self):
        self.assertEqual(replay_plan(fixture(), b'abc'), 'F 0 1\nR 12 60\nF 1 2\n')

    def test_rejects_missing_overflowing_and_reordered_capture(self):
        for corruption in ('event-overflow', 'transport-overflow', 'count', 'gap', 'duplicate',
                           'missing-feed', 'unknown', 'missing-end', 'feed-during-resize', 'bad-size'):
            changed = copy.deepcopy(fixture())
            if corruption == 'event-overflow':
                changed['events_overflow'] = True
            elif corruption == 'transport-overflow':
                changed['transport_overflow'] = True
            elif corruption == 'count':
                changed['consumed_bytes'] = 4
            elif corruption == 'gap':
                changed['events'][-1]['offset'] = 2
            elif corruption == 'duplicate':
                changed['events'].append(copy.deepcopy(changed['events'][-1]))
            elif corruption == 'missing-feed':
                changed['events'].pop()
            elif corruption == 'unknown':
                changed['events'][2]['kind'] = 'synthetic-ack'
            elif corruption == 'missing-end':
                changed['events'].remove(find(changed, 'parser_end'))
            elif corruption == 'feed-during-resize':
                changed['events'][3], changed['events'][-1] = changed['events'][-1], changed['events'][3]
            else:
                find(changed, 'parser_begin')['columns'] = 0
            with self.subTest(corruption=corruption), self.assertRaises(ValueError):
                replay_plan(changed, b'abc')

    def test_schema_generation_host_pair_geometry_and_qpc_corruptions_are_rejected(self):
        cases = [('host_begin', 'generation', -1), ('host_end', 'generation', 999),
                 ('host_begin', 'columns', 0), ('feed', 'rows', 200), ('host_end', 'rows', 13),
                 ('request', 'generation', 2), ('request', 'rows', 13),
                 ('parser_end', 'generation', 1), ('feed', 'generation', 1),
                 ('feed', 'qpc', -1), ('host_begin', 'qpc', 1),
                 ('host_end', 'qpc', True), ('feed', 'qpc', 1 << 64),
                 ('host_begin', 'generation', 1 << 32), ('feed', 'rows', True),
                 ('host_begin', 'stage', -1), ('feed', 'extra', 0)]
        for kind, field, value in cases:
            changed = fixture()
            find(changed, kind)[field] = value
            with self.subTest(kind=kind, field=field, value=value), self.assertRaises(ValueError):
                replay_plan(changed, b'abc')
        for field, value in [('schema', True), ('qpc_frequency', 0), ('qpc_frequency', False),
                             ('qpc_frequency', -1), ('qpc_frequency', 1.5), ('extra', 0)]:
            changed = fixture()
            changed[field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                replay_plan(changed, b'abc')
        for kind in ('desired', 'parser_begin', 'parser_end', 'request', 'host_begin', 'host_end'):
            changed = fixture()
            changed['events'].remove(find(changed, kind))
            with self.subTest(missing=kind), self.assertRaises(ValueError):
                replay_plan(changed, b'abc')
        for field in ('qpc', 'generation', 'rows', 'columns', 'stage'):
            changed = fixture()
            del find(changed, 'feed')[field]
            with self.subTest(missing=field), self.assertRaises(ValueError):
                replay_plan(changed, b'abc')

    def test_coalescing_and_worker_observation_before_request_are_valid(self):
        events = []
        for generation, (rows, columns) in enumerate(((12, 60), (30, 90), (10, 50), (28, 100)), 1):
            for kind in ('desired', 'parser_begin', 'parser_end'):
                events.append(dict(kind=kind, rows=rows, columns=columns, offset=0, bytes=0))
                if generation == 3 and kind == 'parser_begin':
                    events.append(dict(kind='host_end', generation=2, rows=30, columns=90, offset=0, bytes=0))
            if generation == 2:
                events.append(dict(kind='host_begin', generation=2, rows=rows, columns=columns, offset=0, bytes=0))
            events.append(dict(kind='request', generation=generation, rows=rows, columns=columns, offset=0, bytes=0))
            if generation == 4:
                for kind in ('host_begin', 'host_end'):
                    events.append(dict(kind=kind, generation=4, rows=rows, columns=columns, offset=0, bytes=0))
        events.append(dict(kind='feed', rows=28, columns=100, offset=0, bytes=3))
        self.assertEqual(replay_plan(trace(events), b'abc'), 'R 12 60\nR 30 90\nR 10 50\nR 28 100\nF 0 3\n')

    def test_equal_qpc_is_valid_but_future_host_generation_and_overlapping_calls_are_not(self):
        changed = fixture()
        find(changed, 'host_end')['qpc'] = find(changed, 'host_begin')['qpc']
        self.assertEqual(replay_plan(changed, b'abc'), 'F 0 1\nR 12 60\nF 1 2\n')
        changed = fixture()
        begin = find(changed, 'host_begin')
        begin['generation'] = 2
        find(changed, 'host_end')['generation'] = 2
        with self.assertRaises(ValueError):
            replay_plan(changed, b'abc')
        changed = fixture()
        changed['events'][5] = copy.deepcopy(changed['events'][4])
        with self.assertRaises(ValueError):
            replay_plan(changed, b'abc')


if __name__ == '__main__':
    unittest.main()
