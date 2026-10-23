"""One bounded loopback Telnet peer; only original VT bytes and captured input."""
import hashlib
import json
import socket
import threading
import time


def payload(columns):
    if not 40 <= columns <= 300:
        raise ValueError("Unsupported controlled column count")
    soft = "TF-SOFT:" + "s" * (columns - 12) + "LEFTRIGHT:END"
    hard = "TF-HARD:" + "h" * (columns - 12) + "LEFT\r\nRIGHT:END"
    return ("\x1b[2J\x1b[H\x1b[?7h" + soft + "\r\n" + hard +
            "\r\nTF-TAIL:two  \r\nTF-UNICODE:中文 e\u0301\r\nTF-VT-END\r\n").encode("utf-8")


class Peer:
    def __init__(self, directory):
        self.directory = directory
        self.listener = socket.socket()
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.listener.settimeout(.2)
        self.port = self.listener.getsockname()[1]
        self.stop = threading.Event()
        self.state = {"connected": False, "columns": None, "rows": None, "received_hex": "", "sent_sha256": None}
        self.thread = threading.Thread(target=self._serve, name="TermForgeR1-owned-loopback", daemon=False)
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
            connection.settimeout(.1)
            # WILL ECHO, WILL SUPPRESS-GO-AHEAD, DO NAWS. No credentials/commands.
            connection.sendall(bytes([255, 251, 1, 255, 251, 3, 255, 253, 31]))
            parse, verb, sub = "text", 0, bytearray()
            received = bytearray()
            sent = False
            deadline = time.monotonic() + 150
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
                        if byte in (251, 252, 253, 254):
                            verb, parse = byte, "option"
                        elif byte == 250:
                            sub, parse = bytearray(), "sub"
                        else:
                            if byte == 255:
                                received.append(byte)
                            parse = "text"
                    elif parse == "option":
                        if verb == 253 and byte not in (1, 3):
                            connection.sendall(bytes([255, 252, byte]))
                        elif verb == 251 and byte != 31:
                            connection.sendall(bytes([255, 254, byte]))
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
                if self.state["columns"] and not sent:
                    content = payload(self.state["columns"])
                    connection.sendall(content)
                    self.state["sent_sha256"] = hashlib.sha256(content).hexdigest()
                    self.state["sent_columns"] = self.state["columns"]
                    sent = True
                self.state["received_hex"] = received.hex()
                self._record()
        except (OSError, RuntimeError, ValueError) as error:
            if not self.stop.is_set():
                self.state["error"] = type(error).__name__
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
