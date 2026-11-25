"""Compare native B/C/D writes under the WinPTY pair shipped with WindTerm."""
from __future__ import annotations

import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import msvcrt
from pathlib import Path
import subprocess
import sys
import time


KERNEL = c.WinDLL("kernel32", use_last_error=True)
PHASES = (
    ((80, 24), b"\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J\x1b[8;80HB\x1b[10;80HC\x1b[11;1HD\x1b[6n"),
    ((60, 12), b"\x1b[6n"),
    ((60, 12), b"\x1b[12;1HRESIZED\x1b[6n"),
    ((80, 24), b"\x1b[6n"),
)
LEGACY_PHASES = (
    ((80, 24), b"\x1b[0m\x1b[2J\x1b[3;3H\x1b[31;44mMAIN\x1b[0m"
     b"\x1b[5;3H\xe4\xb8\xade\xcc\x81\x1b[6;3HBASE\x1b[1;1HS1"),
    ((80, 24), b"\x1b[?1049h\x1b[0m\x1b[2;3HALT\x1b[3;3H\x1b[32;45mSIDE\x1b[0m\x1b[1;1HS2"),
    ((80, 24), b"\x1b[?1049h\x1b[2;3HNEW\x1b[1;1HS3"),
    ((80, 24), b"\x1b[?1049l!\x1b[1;1HS4"),
    ((80, 24), b"\x1b[0m\x1b[?7l\x1b[8;80HAB\x1b[?7h\x1b[10;80HCD\x1b[1;1HS5"),
    ((60, 12), b"\x1b[12;1HRESIZED\x1b[1;1HS6"),
)


def bind(library, name, result, *args):
    fn = getattr(library, name)
    fn.restype, fn.argtypes = result, args
    return fn


class Coord(c.Structure):
    _fields_ = [("x", c.c_short), ("y", c.c_short)]


class Rect(c.Structure):
    _fields_ = [(key, c.c_short) for key in ("left", "top", "right", "bottom")]


class ConsoleInfo(c.Structure):
    _fields_ = [
        ("size", Coord), ("cursor", Coord), ("attributes", w.WORD),
        ("window", Rect), ("maximum", Coord),
    ]


class Cell(c.Structure):
    _fields_ = [("text", w.WCHAR), ("attributes", w.WORD)]


def check(ok, action):
    if not ok:
        raise RuntimeError(f"{action} failed (win32={c.get_last_error()})")


def atomic_json(path, data):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(data, ensure_ascii=True), encoding="utf-8")
    temporary.replace(path)


def is_owned(process):
    owned = w.BOOL()
    api = bind(KERNEL, "IsProcessInJob", w.BOOL, w.HANDLE, w.HANDLE, c.POINTER(w.BOOL))
    check(api(process, None, c.byref(owned)) and owned.value, "owned Job check")


def wait_file(path, deadline, poll=lambda: None):
    while time.monotonic() < deadline:
        poll()
        if path.exists():
            return json.loads(path.read_text(encoding="utf-8"))
        time.sleep(0.025)
    raise TimeoutError(f"{path.name} did not arrive")


def child(directory, phases, await_ui=False):
    is_owned(bind(KERNEL, "GetCurrentProcess", w.HANDLE)())
    std_handle = bind(KERNEL, "GetStdHandle", w.HANDLE, w.DWORD)
    output = std_handle(-11)
    get_mode = bind(KERNEL, "GetConsoleMode", w.BOOL, w.HANDLE, c.POINTER(w.DWORD))
    set_mode = bind(KERNEL, "SetConsoleMode", w.BOOL, w.HANDLE, w.DWORD)
    get_info = bind(KERNEL, "GetConsoleScreenBufferInfo", w.BOOL, w.HANDLE, c.POINTER(ConsoleInfo))
    get_cells = bind(KERNEL, "ReadConsoleOutputW", w.BOOL, w.HANDLE, c.POINTER(Cell),
                     Coord, Coord, c.POINTER(Rect))
    write = bind(KERNEL, "WriteFile", w.BOOL, w.HANDLE, c.c_char_p, w.DWORD, c.POINTER(w.DWORD), w.LPVOID)
    mode = w.DWORD()
    check(get_mode(output, c.byref(mode)), "child output mode")
    check(set_mode(output, mode.value | 0xF), "child VT output mode")
    check(bind(KERNEL, "SetConsoleOutputCP", w.BOOL, w.UINT)(65001), "child UTF-8 output")
    info = ConsoleInfo()
    deadline = time.monotonic() + (27 if await_ui else 20)
    last_grid = None

    def current_grid():
        nonlocal last_grid
        check(get_info(output, c.byref(info)), "child console geometry")
        box = info.window
        actual = box.right - box.left + 1, box.bottom - box.top + 1
        if await_ui and actual != last_grid:
            atomic_json(directory / "ui-native-grid.json", {"grid": actual})
            last_grid = actual
        return actual

    def wait_grid(expected, limit):
        while time.monotonic() < limit:
            if current_grid() == expected:
                return
            time.sleep(0.025)
        raise TimeoutError(f"child viewport did not reach {expected}")

    wait_grid(phases[0][0], deadline)
    atomic_json(directory / "ready.json", {"ready": True})
    for phase, (expected, source) in enumerate(phases):
        request = wait_file(directory / f"phase-{phase}-request.json", deadline, current_grid if await_ui else lambda: None)
        if request != {"phase": phase}:
            raise ValueError("unexpected phase control")
        wait_grid(expected, min(deadline, time.monotonic() + 4))
        amount = w.DWORD()
        check(write(output, source, len(source), c.byref(amount), None) and amount.value == len(source),
              "child fixed VT write")
        wait_grid(expected, min(deadline, time.monotonic() + 4))
        cols, rows = expected
        cells = (Cell * (cols * rows))()
        box = info.window
        target = Rect(box.left, box.top, box.right, box.bottom)
        check(get_cells(output, cells, Coord(cols, rows), Coord(0, 0), c.byref(target)),
              "child native cells")
        if any(getattr(target, name) != getattr(box, name) for name, _ in Rect._fields_):
            raise RuntimeError("child native capture was clipped")
        text = ["".join(cell.text for cell in cells[row * cols:(row + 1) * cols]).rstrip(" \0")
                for row in range(rows)]
        atomic_json(directory / f"phase-{phase}.json", {
            "phase": phase, "source_hex": source.hex(), "size": [info.size.x, info.size.y],
            "window": [box.left, box.top, box.right, box.bottom], "cursor_rc": [info.cursor.y, info.cursor.x],
            "native_rows": text,
        })
    if await_ui and wait_file(directory / "ui-release.json", deadline) != {"observed": len(phases)}:
        raise ValueError("unexpected UI release")
    return 37


def host(dll_path, directory, legacy):
    phases = LEGACY_PHASES if legacy else PHASES
    directory.mkdir(exist_ok=False)
    is_owned(bind(KERNEL, "GetCurrentProcess", w.HANDLE)())
    dll = c.CDLL(str(dll_path.resolve()), winmode=0x1100)
    ptr, errptr = c.c_void_p, c.POINTER(c.c_void_p)
    config_new = bind(dll, "winpty_config_new", ptr, c.c_uint64, errptr)
    config_free = bind(dll, "winpty_config_free", None, ptr)
    set_initial = bind(dll, "winpty_config_set_initial_size", None, ptr, c.c_int, c.c_int)
    set_timeout = bind(dll, "winpty_config_set_agent_timeout", None, ptr, w.DWORD)
    start_agent = bind(dll, "winpty_open", ptr, ptr, errptr)
    free_agent = bind(dll, "winpty_free", None, ptr)
    agent_process = bind(dll, "winpty_agent_process", w.HANDLE, ptr)
    pipe_name = {
        key: bind(dll, f"winpty_con{key}_name", w.LPCWSTR, ptr) for key in ("in", "out")
    }
    config_spawn = bind(dll, "winpty_spawn_config_new", ptr, c.c_uint64, w.LPCWSTR,
                        w.LPCWSTR, w.LPCWSTR, w.LPCWSTR, errptr)
    free_spawn = bind(dll, "winpty_spawn_config_free", None, ptr)
    spawn = bind(dll, "winpty_spawn", w.BOOL, ptr, ptr, c.POINTER(w.HANDLE),
                 c.POINTER(w.HANDLE), c.POINTER(w.DWORD), errptr)
    set_size = bind(dll, "winpty_set_size", w.BOOL, ptr, c.c_int, c.c_int, errptr)
    error_code = bind(dll, "winpty_error_code", w.DWORD, ptr)
    error_free = bind(dll, "winpty_error_free", None, ptr)
    wait = bind(KERNEL, "WaitForSingleObject", w.DWORD, w.HANDLE, w.DWORD)
    close = bind(KERNEL, "CloseHandle", w.BOOL, w.HANDLE)
    get_exit = bind(KERNEL, "GetExitCodeProcess", w.BOOL, w.HANDLE, c.POINTER(w.DWORD))
    peek = bind(KERNEL, "PeekNamedPipe", w.BOOL, w.HANDLE, w.LPVOID, w.DWORD,
                w.LPVOID, c.POINTER(w.DWORD), w.LPVOID)
    error = ptr()

    def require_winpty(value, action):
        code = error_code(error) if error.value else None
        if error.value:
            error_free(error)
            error.value = None
        if not value:
            raise RuntimeError(f"{action} failed (winpty={code})")
        return value

    config = require_winpty(config_new(0, c.byref(error)), "winpty config")
    try:
        set_initial(config, *phases[0][0])
        set_timeout(config, 3000)
        agent = require_winpty(start_agent(config, c.byref(error)), "winpty open")
    finally:
        config_free(config)
    child_handle = w.HANDLE()
    try:
        agent_handle = agent_process(agent)
        is_owned(agent_handle)
        executable = sys.executable
        command = [executable, str(Path(__file__).resolve()), str(directory), "--child"]
        if legacy:
            command.append("--legacy")
        args = subprocess.list2cmdline(command)
        with open(pipe_name["in"](agent), "wb", buffering=0) as input_pipe, \
                open(pipe_name["out"](agent), "rb", buffering=0) as pipe:
            process_config = require_winpty(
                config_spawn(1, executable, args, str(directory), None, c.byref(error)), "winpty process config")
            try:
                create_error = w.DWORD()
                require_winpty(spawn(agent, process_config, c.byref(child_handle), None,
                                     c.byref(create_error), c.byref(error)), "winpty spawn")
            finally:
                free_spawn(process_config)
            is_owned(child_handle)
            native = msvcrt.get_osfhandle(pipe.fileno())
            stream = bytearray()
            deadline = time.monotonic() + 25

            def drain():
                available = w.DWORD()
                if not peek(native, None, 0, None, c.byref(available), None):
                    if c.get_last_error() in (109, 233):
                        return False
                    check(False, "winpty output pipe poll")
                if available.value:
                    stream.extend(pipe.read(min(available.value, 8192)))
                if len(stream) > 65536:
                    raise RuntimeError("winpty output exceeds capture bound")
                return True

            wait_file(directory / "ready.json", deadline, drain)
            observations = []
            current_grid = phases[0][0]
            for phase, (grid, _) in enumerate(phases):
                if grid != current_grid:
                    require_winpty(set_size(agent, *grid, c.byref(error)), "winpty resize")
                    current_grid = grid
                atomic_json(directory / f"phase-{phase}-request.json", {"phase": phase})
                observations.append(wait_file(directory / f"phase-{phase}.json", deadline, drain))
            input_pipe.close()
            while wait(child_handle, 0) == 258 and time.monotonic() < deadline:
                drain()
                time.sleep(0.025)
            code = w.DWORD()
            check(wait(child_handle, 0) == 0 and get_exit(child_handle, c.byref(code)) and code.value == 37,
                  "native child natural exit")
            # Drain the agent's output before releasing its control connection.
            # The agent may wait on that connection after the child has exited.
            while drain() and time.monotonic() < deadline:
                time.sleep(0.025)
            identity = {}
            for path in (dll_path, dll_path.with_name("winpty-agent.exe")):
                identity[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
            atomic_json(directory / "result.json", {
                "schema": "termforge-winpty-bcd-native-v1", "pair_sha256": identity, "legacy": legacy,
                "child_exit": code.value, "agent_shutdown_method": "owner_free",
                "agent_exited_before_free": wait(agent_handle, 0) == 0, "stream_hex": stream.hex(),
                "phases": observations,
            })
    finally:
        if child_handle:
            close(child_handle)
        free_agent(agent)
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--child", action="store_true")
    parser.add_argument("--legacy", action="store_true", help="send the original six-stage Qt resize workload")
    parser.add_argument("--await-ui", action="store_true")
    parser.add_argument("--dll", type=Path)
    args = parser.parse_args()
    if args.await_ui and not (args.child and args.legacy):
        parser.error("UI release applies only to the six-stage local shell child")
    if args.child:
        raise SystemExit(child(args.directory.resolve(), LEGACY_PHASES if args.legacy else PHASES, args.await_ui))
    if args.dll is None or args.dll.name.lower() != "winpty.dll":
        parser.error("the frozen local reference winpty.dll is required")
    raise SystemExit(host(args.dll.resolve(), args.directory.resolve(), args.legacy))
