"""A replay must preserve geometry/feed ordering and reject incomplete capture."""
import copy
import unittest

from experiments.shell.resize_trace import replay_plan


def fixture():
    return dict(schema=1, observer_order_only=True, events_overflow=False,
                transport_overflow=False, consumed_bytes=3, events=[
                    dict(kind='feed', offset=0, bytes=1),
                    dict(kind='parser_begin', offset=1, bytes=0, rows=12, columns=60),
                    dict(kind='host_begin', offset=1, bytes=0, rows=12, columns=60),
                    dict(kind='parser_end', offset=1, bytes=0, rows=12, columns=60),
                    dict(kind='feed', offset=1, bytes=2)])


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
                del changed['events'][3]
            elif corruption == 'feed-during-resize':
                changed['events'][3], changed['events'][4] = changed['events'][4], changed['events'][3]
            else:
                changed['events'][1]['columns'] = 0
            with self.subTest(corruption=corruption), self.assertRaises(ValueError):
                replay_plan(changed, b'abc')


if __name__ == '__main__':
    unittest.main()
