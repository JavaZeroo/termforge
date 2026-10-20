"""Visible-desktop ROI capture and guarded F13 injection for owned fixtures."""
import ctypes as C
from ctypes import wintypes as W
import os
import time

COLS, ROWS, CELL = 8, 4, 16
WIDTH, HEIGHT = COLS * CELL, ROWS * CELL


def template_matches(actual, expected, tolerance=4):
    """Fixed ROI, all RGB channels within tolerance; alpha is unspecified by GDI."""
    if len(actual) != len(expected) or not actual or len(actual) % 4:
        raise ValueError("template shape mismatch")
    if not 0 <= tolerance <= 16:
        raise ValueError("invalid tolerance")
    return all(abs(a - b) <= tolerance for index, (a, b) in enumerate(zip(actual, expected))
               if index % 4 != 3)


def marker_matches(bgra, width, height, code, tolerance=4):
    """Check 3x3 centers of all 32 cells; not OCR or whole-screen validation."""
    if (width, height) != (WIDTH, HEIGHT) or len(bgra) != width * height * 4:
        raise ValueError("unexpected marker frame shape")
    if not 0 <= code < 2**32 or not 0 <= tolerance <= 16:
        raise ValueError("invalid code/tolerance")
    for bit in range(32):
        expected = 240 if (code >> bit) & 1 else 16
        x, y = (bit % COLS) * CELL + CELL // 2, (bit // COLS) * CELL + CELL // 2
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                offset = ((y + dy) * width + x + dx) * 4
                if any(abs(value - expected) > tolerance for value in bgra[offset:offset + 3]):
                    return False
    return True


def clock_bounds(exchanges):
    """Remote-minus-local offset interval from a timestamp within each RTT."""
    if not exchanges:
        raise ValueError("no clock exchanges")
    for row in exchanges:
        if row["after_ns"] < row["before_ns"]:
            raise ValueError("nonmonotonic host clock")
    best = min(exchanges, key=lambda row: row["after_ns"] - row["before_ns"])
    return {
        "lower_ns": best["remote_ns"] - best["after_ns"],
        "upper_ns": best["remote_ns"] - best["before_ns"],
        "rtt_ns": best["after_ns"] - best["before_ns"],
        "method": "minimum-RTT interval; no symmetric-delay assumption",
        "exchanges": exchanges,
    }


class BITMAPINFOHEADER(C.Structure):
    _fields_ = [("size", W.DWORD), ("width", W.LONG), ("height", W.LONG),
                ("planes", W.WORD), ("bits", W.WORD), ("compression", W.DWORD),
                ("image_size", W.DWORD), ("xppm", W.LONG), ("yppm", W.LONG),
                ("used", W.DWORD), ("important", W.DWORD)]


class BITMAPINFO(C.Structure):
    _fields_ = [("header", BITMAPINFOHEADER), ("colors", W.DWORD * 1)]


class KEYBDINPUT(C.Structure):
    _fields_ = [("vk", W.WORD), ("scan", W.WORD), ("flags", W.DWORD),
                ("time", W.DWORD), ("extra", C.c_size_t)]


class MOUSEINPUT(C.Structure):
    _fields_ = [("dx", W.LONG), ("dy", W.LONG), ("data", W.DWORD),
                ("flags", W.DWORD), ("time", W.DWORD), ("extra", C.c_size_t)]


class HARDWAREINPUT(C.Structure):
    _fields_ = [("message", W.DWORD), ("low", W.WORD), ("high", W.WORD)]


class INPUTUNION(C.Union):
    _fields_ = [("keyboard", KEYBDINPUT), ("mouse", MOUSEINPUT), ("hardware", HARDWAREINPUT)]


class INPUT(C.Structure):
    _fields_ = [("type", W.DWORD), ("value", INPUTUNION)]


class Windows:
    def __init__(self):
        if os.name != "nt":
            raise RuntimeError("Windows required")
        self.user = C.WinDLL("user32", use_last_error=True)
        self.gdi = C.WinDLL("gdi32", use_last_error=True)
        signatures = [
            (self.user, "GetDC", [W.HWND], W.HDC),
            (self.user, "ReleaseDC", [W.HWND, W.HDC], C.c_int),
            (self.user, "GetClientRect", [W.HWND, C.POINTER(W.RECT)], W.BOOL),
            (self.user, "ClientToScreen", [W.HWND, C.POINTER(W.POINT)], W.BOOL),
            (self.user, "GetWindowThreadProcessId", [W.HWND, C.POINTER(W.DWORD)], W.DWORD),
            (self.user, "GetAncestor", [W.HWND, W.UINT], W.HWND),
            (self.user, "GetForegroundWindow", [], W.HWND),
            (self.user, "IsWindow", [W.HWND], W.BOOL),
            (self.user, "IsWindowVisible", [W.HWND], W.BOOL),
            (self.user, "IsIconic", [W.HWND], W.BOOL),
            (self.user, "GetAsyncKeyState", [C.c_int], W.SHORT),
            (self.user, "SendInput", [W.UINT, C.POINTER(INPUT), C.c_int], W.UINT),
            (self.gdi, "CreateCompatibleDC", [W.HDC], W.HDC),
            (self.gdi, "CreateDIBSection", [W.HDC, C.POINTER(BITMAPINFO), W.UINT,
                                         C.POINTER(C.c_void_p), W.HANDLE, W.DWORD], W.HBITMAP),
            (self.gdi, "SelectObject", [W.HDC, W.HANDLE], W.HANDLE),
            (self.gdi, "DeleteObject", [W.HANDLE], W.BOOL),
            (self.gdi, "DeleteDC", [W.HDC], W.BOOL),
            (self.gdi, "BitBlt", [W.HDC, C.c_int, C.c_int, C.c_int, C.c_int,
                                W.HDC, C.c_int, C.c_int, W.DWORD], W.BOOL),
            (self.gdi, "GdiFlush", [], W.BOOL),
        ]
        for dll, name, arguments, result in signatures:
            function = getattr(dll, name)
            function.argtypes, function.restype = arguments, result

    def require_target(self, hwnd, pid, foreground=True):
        owner = W.DWORD()
        if not self.user.IsWindow(hwnd):
            raise RuntimeError("target window closed")
        self.user.GetWindowThreadProcessId(hwnd, C.byref(owner))
        if owner.value != pid:
            raise RuntimeError("window ownership changed")
        root = self.user.GetAncestor(hwnd, 2)  # GA_ROOT
        if not self.user.IsWindowVisible(root) or self.user.IsIconic(root):
            raise RuntimeError("target hidden or minimized")
        if foreground and self.user.GetForegroundWindow() != root:
            raise RuntimeError("target lost foreground; refusing input/capture")
        return root

    def inject_f13(self, hwnd, pid):
        self.require_target(hwnd, pid)
        for vk in (0x10, 0x11, 0x12, 0x5B, 0x5C):  # shift, ctrl, alt, win
            if self.user.GetAsyncKeyState(vk) & 0x8000:
                raise RuntimeError("modifier held; refusing injection")
        events = (INPUT * 2)()
        for index, flags in enumerate((0, 2)):
            events[index].type = 1
            events[index].value.keyboard = KEYBDINPUT(0x7C, 0, flags, 0, 0)
        started = time.perf_counter_ns()
        count = self.user.SendInput(2, events, C.sizeof(INPUT))
        ended = time.perf_counter_ns()
        if count != 2:
            # Release an injected key-down if Windows accepted only one event.
            if count == 1:
                release = (INPUT * 1)(events[1])
                self.user.SendInput(1, release, C.sizeof(INPUT))
            raise RuntimeError(f"SendInput accepted {count}/2 events")
        self.require_target(hwnd, pid)
        return {"submit_ns": started, "return_ns": ended, "accepted_events": count}


class Capture:
    """Reusable top-down BGRA DIB. Captures only the chosen client ROI."""
    def __init__(self, api, hwnd, pid, x=0, y=0, width=WIDTH, height=HEIGHT):
        if min(x, y) < 0 or not 1 <= width <= 4096 or not 1 <= height <= 4096:
            raise ValueError("invalid ROI")
        self.api, self.hwnd, self.pid = api, hwnd, pid
        self.x, self.y, self.width, self.height = x, y, width, height
        self.screen = self.memory = self.bitmap = self.old = None
        try:
            api.require_target(hwnd, pid)
            self.screen = api.user.GetDC(None)
            self.memory = api.gdi.CreateCompatibleDC(self.screen)
            info = BITMAPINFO()
            info.header = BITMAPINFOHEADER(C.sizeof(BITMAPINFOHEADER), width, -height, 1, 32,
                                          0, width * height * 4, 0, 0, 0, 0)
            self.bits = C.c_void_p()
            self.bitmap = api.gdi.CreateDIBSection(self.screen, C.byref(info), 0,
                                                  C.byref(self.bits), None, 0)
            if not all((self.screen, self.memory, self.bitmap, self.bits.value)):
                raise C.WinError(C.get_last_error())
            self.old = api.gdi.SelectObject(self.memory, self.bitmap)
            if not self.old or self.old == C.c_void_p(-1).value:
                self.old = None
                raise RuntimeError("SelectObject failed")
        except BaseException:
            self.close()
            raise

    def grab(self):
        self.api.require_target(self.hwnd, self.pid)
        rect, origin = W.RECT(), W.POINT(self.x, self.y)
        if not self.api.user.GetClientRect(self.hwnd, C.byref(rect)):
            raise C.WinError(C.get_last_error())
        if self.x + self.width > rect.right or self.y + self.height > rect.bottom:
            raise RuntimeError("ROI no longer fits client area")
        if not self.api.user.ClientToScreen(self.hwnd, C.byref(origin)):
            raise C.WinError(C.get_last_error())
        before = time.perf_counter_ns()
        if not self.api.gdi.BitBlt(self.memory, 0, 0, self.width, self.height,
                                  self.screen, origin.x, origin.y, 0x40CC0020):
            raise C.WinError(C.get_last_error())
        self.api.gdi.GdiFlush()
        data = C.string_at(self.bits, self.width * self.height * 4)
        after = time.perf_counter_ns()
        self.api.require_target(self.hwnd, self.pid)
        return data, {"capture_before_ns": before, "capture_after_ns": after}

    def close(self):
        if self.old:
            self.api.gdi.SelectObject(self.memory, self.old)
            self.old = None
        if self.bitmap:
            self.api.gdi.DeleteObject(self.bitmap)
            self.bitmap = None
        if self.memory:
            self.api.gdi.DeleteDC(self.memory)
            self.memory = None
        if self.screen:
            self.api.user.ReleaseDC(None, self.screen)
            self.screen = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()
