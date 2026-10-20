"""Owned visible calibration fixture. stdin/stdout carry timing records only."""
import ctypes
import json
import os
import queue
import sys
import threading
import time
import tkinter as tk

from visual import WIDTH, HEIGHT, CELL, COLS


def main():
    ctypes.windll.user32.SetProcessDPIAware()
    root = tk.Tk()
    root.title("TermForge calibration — temporary test window")
    root.geometry("420x160+80+80")
    root.resizable(False, False)
    tk.Label(root, text="TermForge visual calibration\nDo not type; changing focus stops the test.").pack()
    canvas = tk.Canvas(root, width=WIDTH, height=HEIGHT, highlightthickness=0, borderwidth=0)
    canvas.pack()
    blocks = [canvas.create_rectangle((i % COLS) * CELL, (i // COLS) * CELL,
                                       (i % COLS + 1) * CELL, (i // COLS + 1) * CELL,
                                       width=0, fill="#101010") for i in range(32)]
    commands, lock = queue.Queue(), threading.Lock()
    state = {"nonce": 0, "sequence": 0, "delay_ms": 0, "pending": False}

    def emit(row):
        with lock:
            print(json.dumps(row), flush=True)

    def draw(code):
        for bit, block in enumerate(blocks):
            canvas.itemconfigure(block, fill="#f0f0f0" if (code >> bit) & 1 else "#101010")
        root.update_idletasks()

    def reader():
        for line in sys.stdin:
            command = json.loads(line)
            if command["op"] == "ping":
                emit({"type": "pong", "id": command["id"], "remote_ns": time.perf_counter_ns()})
            else:
                commands.put(command)
        commands.put({"op": "quit"})

    def received(event):
        if state["pending"]:
            return
        state["pending"] = True
        state["sequence"] += 1
        sequence = state["sequence"]
        received_ns = time.perf_counter_ns()

        def paint():
            draw(state["nonce"] ^ sequence)
            emit({"type": "paint_submitted", "sequence": sequence,
                  "input_received_ns": received_ns, "paint_submitted_ns": time.perf_counter_ns()})
            state["pending"] = False

        root.after(state["delay_ms"], paint)

    def poll():
        while not commands.empty():
            command = commands.get()
            if command["op"] == "quit":
                root.destroy()
                return
            if command["op"] == "configure":
                state.update(nonce=command["nonce"], sequence=0, delay_ms=command["delay_ms"])
                draw(state["nonce"])
                emit({"type": "configured", "id": command["id"]})
        root.after(2, poll)

    root.bind("<F13>", received)
    root.update()
    root.lift()
    root.focus_force()  # Only this owned fixture; harness never steals focus back.
    root.update()
    emit({"type": "ready", "pid": os.getpid(), "canvas_hwnd": canvas.winfo_id(),
          "width": WIDTH, "height": HEIGHT})
    threading.Thread(target=reader, daemon=True).start()
    root.after(2, poll)
    root.after(600000, root.destroy)  # Covers 200 events per delay; orphan watchdog.
    root.mainloop()


if __name__ == "__main__":
    main()
