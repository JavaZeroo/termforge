"""Synthetic, bounded output/input peer for the R1 reference probe."""
import ctypes
import json
import msvcrt
import os
from pathlib import Path
import sys
import time


def main(out: Path):
    ctypes.windll.kernel32.SetConsoleOutputCP(65001)
    ctypes.windll.kernel32.SetConsoleCP(65001)
    kernel = ctypes.windll.kernel32
    kernel.GetStdHandle.restype = ctypes.c_void_p
    kernel.GetConsoleMode.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)]
    kernel.SetConsoleMode.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    output = kernel.GetStdHandle(-11)
    mode = ctypes.c_uint32()
    if kernel.GetConsoleMode(output, ctypes.byref(mode)):
        kernel.SetConsoleMode(output, mode.value | 4)
    def write(value):
        os.write(sys.stdout.fileno(), value.encode("utf-8"))
    write("\x1b[2J\x1b[H")
    for n in range(80):
        write(f"TF-HISTORY-{n:03d} alpha needle omega\r\n")
    write("TF-TEXT-BEGIN\r\nALPHA  tail   \r\nBETA 中文 e\u0301\r\n")
    write("SOFT:" + "s" * 180 + ":END\r\n")
    write("TF-TEXT-END\r\n")
    received = []
    appended = False
    last_state = None
    deadline = time.monotonic() + 200
    while time.monotonic() < deadline and not (out / "PEER_STOP").exists():
        if not appended and (out / "APPEND").exists():
            for n in range(20):
                write(f"TF-APPEND-{n:03d}\r\n")
            appended = True
        while msvcrt.kbhit():
            ch = msvcrt.getwch()
            received.append(ch)
            if ch == "~":
                for n in range(20):
                    write(f"TF-APPEND-{n:03d}\r\n")
                appended = True
        state = json.dumps({"received": "".join(received), "appended": appended}, ensure_ascii=True)
        if state != last_state:
            temporary = out / "peer.tmp"
            temporary.write_text(state, encoding="utf-8")
            try:
                temporary.replace(out / "peer.json")
                last_state = state
            except PermissionError:
                pass  # A bounded concurrent observer may briefly hold the file.
        time.sleep(.05)


if __name__ == "__main__":
    main(Path(sys.argv[1]))
