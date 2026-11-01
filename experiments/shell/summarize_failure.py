"""Summarise the workspace failure dump from a saved shell log (#73).

The dump is three shapes of line:

    workspace-failure mode=<mode> stage=<n> what=<text>
      tab <group>:<index> input_high_water=<n> ... exit_code=<n>
    workspace-failure mode=<mode> what=<text>

This module turns them into rows so a failure can be read without scrolling a
raw CI log. It parses text only; it never runs the shell.
"""
import re

MODE = re.compile(r"workspace-failure mode=(?P<mode>\S+)(?: stage=(?P<stage>\d+))? what=(?P<what>.*)")
TAB = re.compile(r"\btab (?P<group>main|bottom):(?P<index>\d+) (?P<fields>.*)")


def parse(text):
    """Return one dict per workspace failure, each with its tab counters."""
    failures = []
    current = None
    for line in text.splitlines():
        found = MODE.search(line)
        if found:
            current = dict(mode=found.group("mode"),
                           stage=int(found.group("stage")) if found.group("stage") else None,
                           what=found.group("what").strip(),
                           tabs=[])
            failures.append(current)
            continue
        found = TAB.search(line)
        if found and current is not None:
            counters = {}
            for item in found.group("fields").split():
                if "=" in item:
                    key, value = item.split("=", 1)
                    counters[key] = value
            current["tabs"].append(dict(group=found.group("group"),
                                        index=int(found.group("index")),
                                        counters=counters))
    return failures


def describe(failure):
    """Return a one-line verdict for the counters that decide the shape."""
    verdicts = []
    for tab in failure["tabs"]:
        counters = tab["counters"]
        read = counters.get("output_bytes")
        consumed = counters.get("consumed")
        drained = read is not None and read == consumed
        verdicts.append(f"{tab['group']}:{tab['index']} "
                        f"rejected={counters.get('input_rejected')} "
                        f"drained={'yes' if drained else 'no'} "
                        f"child={counters.get('exit_code')}")
    return verdicts
