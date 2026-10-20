"""Deterministic streaming workloads. No terminal is launched."""
import argparse
from collections import deque
import hashlib
import json
from pathlib import Path

VERSION = "1"
KINDS = ("ascii", "ansi", "unicode", "history", "longline", "overwrite", "alternate")


def content(seed, index, width, variant):
    if variant == "repeated":
        return ("abcdefgh" * ((width + 7) // 8))[:width]
    # Explicit SHA-256 counter construction, independent of Python PRNG versions.
    text = ""
    block = 0
    while len(text) < width:
        text += hashlib.sha256(f"{seed}:{index}:{block}".encode("ascii")).hexdigest()
        block += 1
    return text[:width]


def record(kind, index, seed, variant, width):
    prefix = f"{index:010d} "
    if kind == "unicode":
        text = prefix + "中文 e\u0301 ASCII " * 4
    else:
        text = prefix + content(seed, index, width, variant)
    if kind == "ansi":
        parts = []
        for pos in range(0, len(text), 8):
            color = (index * 10 + pos // 8) % 16
            code = 30 + color if color < 8 else 90 + color - 8
            parts.append(f"\x1b[{code}m{text[pos:pos + 8]}")
        wire = "".join(parts) + "\x1b[0m\r\n"
    elif kind == "overwrite":
        text = f"{index:010d} " + "#" * 68  # 79 cells: no right-edge wrap.
        wire = "\r\x1b[2K" + text
    elif kind == "alternate":
        text = f"ALT {index:010d}"
        wire = "\x1b[?1049h\x1b[2J\x1b[H" + text + "\x1b[?1049l"
    else:
        wire = text + "\r\n"
    return wire.encode("utf-8"), text


def generate(destination, kind="ascii", target_bytes=8388608, count=None,
             seed=20261020, variant="repeated", width=None):
    if kind not in KINDS or variant not in ("repeated", "pseudorandom"):
        raise ValueError("unknown kind/variant")
    if target_bytes <= 0 or (count is not None and count <= 0):
        raise ValueError("positive size/count required")
    width = width if width is not None else (4096 if kind == "longline" else 64)
    if width <= 0 or width > 65536:
        raise ValueError("width must be in 1..65536")
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=False)
    digest = hashlib.sha256()
    oracle_digest = hashlib.sha256()
    total = 0
    tail = deque(maxlen=39)
    control_bytes = 0
    index = 0
    # Directory creation is exclusive. An interrupted directory is incomplete;
    # manifest is written last and never describes partial data as complete.
    with (destination / "payload.bin").open("wb") as payload, \
            (destination / "oracle.txt").open("wb") as oracle:
        def emit(data):
            nonlocal total
            payload.write(data)
            digest.update(data)
            total += len(data)

        def expect(text):
            data = (text + "\n").encode("utf-8")
            oracle.write(data)
            oracle_digest.update(data)
            tail.append(text)

        if kind == "alternate":
            emit(b"MAIN-SCREEN\r\n")
            expect("MAIN-SCREEN")
        while (index < count) if count is not None else (total < target_bytes):
            wire, text = record(kind, index, seed, variant, width)
            emit(wire)
            if kind not in ("overwrite", "alternate"):
                expect(text)
            if kind == "ansi":
                control_bytes += len(wire) - len(text.encode("utf-8")) - 2
            elif kind == "overwrite":
                control_bytes += 5
            elif kind == "alternate":
                control_bytes += len(wire) - len(text)
            index += 1
        if kind == "overwrite":
            emit(b"\r\n")
            expect(text)
        marker = f"TERMFORGE-END {kind} {index:010d}"
        marker_bytes = (marker + "\r\n").encode("ascii")
        emit(marker_bytes)
        expect(marker)
    manifest = {
        "generator_version": VERSION, "protocol_version": "0.1",
        "status": "generated_not_terminal_verified",
        "generator_source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "kind": kind, "seed": seed, "variant": variant,
        "target_bytes": target_bytes if count is None else None,
        "requested_count": count, "records": index, "body_width": width,
        "payload_bytes": total, "sha256": digest.hexdigest(),
        "oracle_sha256": oracle_digest.hexdigest(),
        "marker": marker, "marker_bytes": len(marker_bytes),
        "control_sequence_bytes_excluding_crlf": control_bytes,
        "unicode_pattern": "中文 é ASCII " if kind == "unicode" else None,
        "ansi_style_rule": "color=(record_index*10+character_offset//8)%16; standard/bright foreground; reset at CRLF" if kind == "ansi" else None,
        "expected_logical_tail": list(tail), "final_cursor_on_blank_line": True,
        "oracle": "oracle.txt: main-screen logical lines, LF; includes marker",
        "limitations": [
            "Requires fresh main screen, UTF-8, autowrap; logical oracle is not pixel/layout verification.",
            "Long lines require reflow-aware comparison; Unicode assumes CJK width 2 and combining accent width 0.",
            "ANSI uses 16 colors per 8 characters; oracle omits attributes, inspect style checkpoints separately.",
            "Alternate workload tests switching only; T05 resize and per-step acknowledgement require a driver.",
        ],
    }
    (destination / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True, help="new output directory")
    parser.add_argument("--kind", choices=KINDS, default="ascii")
    parser.add_argument("--bytes", type=int, default=8388608)
    parser.add_argument("--count", type=int)
    parser.add_argument("--seed", type=int, default=20261020)
    parser.add_argument("--variant", choices=("repeated", "pseudorandom"), default="repeated")
    parser.add_argument("--width", type=int)
    args = parser.parse_args()
    print(json.dumps(generate(args.out, args.kind, args.bytes, args.count,
                              args.seed, args.variant, args.width), ensure_ascii=True))


if __name__ == "__main__":
    main()
