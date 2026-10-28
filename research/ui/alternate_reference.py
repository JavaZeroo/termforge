"""Bounded original WindTerm terminal-mode observation over an owned loopback peer.

Uses the existing pinned-package, synthetic-profile and non-input station guards.
This observes the original terminal directly, independently of local-shell transport.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import socket
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))


def phases(rows: int) -> list[tuple[str, bytes]]:
    esc = b"\x1b["
    return [
        ("main-before", esc+b"0m"+esc+b"2J"+esc+b"HMAIN_A"+esc+b"3;6H"+esc+b"31;44m"),
        ("1049-first-enter", esc+b"?1049h"),
        ("1049-alt-written", b"ALT_ONE"+esc+b"5;9H"+esc+b"32;45m"),
        ("1049-duplicate-enter", esc+b"?1049h"),
        ("1049-duplicate-write", b"DUP"),
        ("1049-first-exit", esc+b"?1049l"),
        ("1049-main-write", b"M"),
        ("1049-duplicate-exit", esc+b"?1049l"),
        ("1049-second-enter", esc+b"?1049h"),
        ("1049-second-write", b"ALT_TWO"),
        ("1049-alt-pressure", b"\r\n"+b"\r\n".join(f"ALT_LINE_{n:02}".encode() for n in range(rows+5))),
        ("1049-second-exit", esc+b"?1049l"),
        ("47-first-enter", esc+b"?47h"),
        ("47-alt-written", b"ALT47"+esc+b"6;11H"+esc+b"33;46m"),
        ("47-first-exit", esc+b"?47l"),
        ("47-second-enter", esc+b"?47h"),
        ("47-second-exit", esc+b"?47l"),
        ("1047-first-enter", esc+b"?1047h"),
        ("1047-alt-written", b"ALT1047"+esc+b"7;12H"),
        ("1047-first-exit", esc+b"?1047l"),
        ("1047-second-enter", esc+b"?1047h"),
        ("1047-second-exit", esc+b"?1047l"),
        ("1048-save", esc+b"8;13H"+esc+b"31;44m"+esc+b"?1048h"),
        ("1048-moved", esc+b"12;20H"+esc+b"32;45m"),
        ("1048-restore", esc+b"?1048l"),
        ("1048-restored-write", b"Q"),
    ]


class Peer:
    def __init__(self, directory):
        self.directory = directory
        self.listener = socket.socket()
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.listener.settimeout(.1)
        self.port = self.listener.getsockname()[1]
        self.stop = threading.Event()
        self.state = {"connected": False, "columns": None, "rows": None, "phase": None, "records": []}
        self.thread = threading.Thread(target=self._serve, name="TermForge-owned-mode-peer", daemon=False)
        self.thread.start()

    def _record(self):
        temporary = self.directory / "peer.tmp"
        temporary.write_text(json.dumps(self.state), encoding="utf-8")
        try:
            temporary.replace(self.directory / "peer.json")
        except PermissionError:
            pass

    def _serve(self):
        connection = None
        try:
            while not self.stop.is_set():
                try:
                    connection, address = self.listener.accept()
                    break
                except socket.timeout:
                    pass
            if connection is None:
                return
            if address[0] != "127.0.0.1":
                raise RuntimeError("Non-loopback client")
            self.state["connected"] = True
            connection.settimeout(.05)
            connection.sendall(bytes([255,251,1,255,251,3,255,253,31]))
            parse, verb, sub = "text", 0, bytearray()
            received = bytearray()
            index = -1
            reply_start = 0
            selected = None
            deadline = time.monotonic() + 43
            while not self.stop.is_set() and time.monotonic() < deadline:
                try:
                    block = connection.recv(4096)
                    if not block:
                        break
                except socket.timeout:
                    block = b""
                for byte in block:
                    if parse == "text":
                        if byte == 255:
                            parse = "iac"
                        else:
                            received.append(byte)
                    elif parse == "iac":
                        if byte in (251,252,253,254):
                            verb, parse = byte, "option"
                        elif byte == 250:
                            sub, parse = bytearray(), "sub"
                        else:
                            if byte == 255:
                                received.append(byte)
                            parse = "text"
                    elif parse == "option":
                        if verb == 253 and byte not in (1,3):
                            connection.sendall(bytes([255,252,byte]))
                        elif verb == 251 and byte != 31:
                            connection.sendall(bytes([255,254,byte]))
                        parse = "text"
                    elif parse == "sub":
                        if byte == 255:
                            parse = "sub-iac"
                        elif len(sub) < 256:
                            sub.append(byte)
                        else:
                            raise RuntimeError("Suboption bound")
                    elif parse == "sub-iac":
                        if byte == 240:
                            if len(sub) == 5 and sub[0] == 31:
                                self.state["columns"] = int.from_bytes(sub[1:3], "big")
                                self.state["rows"] = int.from_bytes(sub[3:5], "big")
                            parse = "text"
                        else:
                            if byte == 255:
                                sub.append(byte)
                            parse = "sub"
                if len(received) > 65536:
                    raise RuntimeError("Input bound")
                if self.state["rows"] and selected is None:
                    if not 10 <= self.state["rows"] <= 60 or not 40 <= self.state["columns"] <= 160:
                        raise RuntimeError("Unexpected controlled grid")
                    selected = phases(self.state["rows"])
                    self.state["phase_count"] = len(selected)
                if index >= 0:
                    matches = list(re.finditer(rb"\x1b\[(\??)(\d+);(\d+)R", bytes(received[reply_start:])))
                    self.state["records"][index]["cursor_reply"] = [int(matches[-1][2]),int(matches[-1][3])] if matches else None
                if selected and (index == -1 or (self.directory / f"ack-{index}").exists()):
                    index += 1
                    if index == len(selected):
                        self.state["complete"] = True
                        self._record()
                        break
                    label, content = selected[index]
                    wire = content+b"\x1b[6n"
                    reply_start = len(received)
                    connection.sendall(wire)
                    self.state["phase"] = index
                    self.state["phase_label"] = label
                    self.state["records"].append({"phase": index, "label": label, "source_hex": content.hex(), "source_bytes": len(content), "source_sha256": hashlib.sha256(content).hexdigest(), "dsr_suffix_hex": "1b5b366e", "cursor_reply": None})
                self.state["received_hex"] = received.hex()
                self._record()
        except (OSError, RuntimeError, ValueError) as error:
            if not self.stop.is_set():
                self.state["error"] = str(error)
        finally:
            if connection:
                connection.close()
            self.state["closed"] = True
            self._record()

    def close(self):
        self.stop.set()
        self.thread.join(3)
        self.listener.close()
        if self.thread.is_alive():
            raise RuntimeError("Owned loopback peer failed to stop")


def run(source: Path, out: Path, rea: Path):
    from research.ui import r1_launch
    launcher = Path(r1_launch.__file__)
    text = launcher.read_text(encoding="utf-8")
    replacements = {
        "from research.ui.r1_vt_fixture import Peer": "from research.ui.alternate_reference import Peer",
        'Path(__file__).with_name("r1_menu_wrap.js").read_text(encoding="utf-8")': 'Path(__file__).with_name("alternate_reference.js").read_text(encoding="utf-8")',
    }
    for old, new in replacements.items():
        if text.count(old) != 1:
            raise RuntimeError("Existing protected launcher source changed")
        text = text.replace(old, new)
    namespace = {"__file__": str(launcher), "__name__": "termforge_alternate_launcher"}
    exec(compile(text, str(launcher), "exec"), namespace)
    namespace["run"](source, out, rea, 45, False, "menu-wrap")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--rea", type=Path, required=True)
    args = parser.parse_args()
    run(args.source, args.out, args.rea)
