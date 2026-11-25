"""Strict reads and checked source adaptations for offline resize exporters."""

import json


def read(path):
    if path.stat().st_size > 4 * 1024 * 1024:
        raise ValueError("Recording exceeds byte bound")

    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("Duplicate recording field")
            result[key] = value
        return result

    def nonfinite(_):
        raise ValueError("Nonfinite JSON value")

    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique, parse_constant=nonfinite)


def adapt(source, changes):
    for old, new in changes.items():
        if source.count(old) != 1:
            raise ValueError("Protected offline audit adapter changed: " + old)
        source = source.replace(old, new)
    return source


def extract(path, start, end):
    source = path.read_text(encoding="utf-8")
    if source.count(start) != 1 or source.count(end) != 1:
        raise ValueError("Protected offline audit function boundary changed")
    return start + source.split(start, 1)[1].split(end, 1)[0]
