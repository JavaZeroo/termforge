"""Resolve an explicit build's binaries without silently falling back to stale output."""
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def binary(name, directory=None):
    filename = name + (".exe" if os.name == "nt" else "")
    directory = directory if directory is not None else os.environ.get("TERMFORGE_BIN_DIR")
    directories = ([Path(directory)] if directory is not None else
                   [ROOT / "build/bin/Release", ROOT / "build/bin",
                    ROOT / "build/receive/Release", ROOT / "build/receive"])
    for folder in directories:
        candidate = folder / filename
        if candidate.is_file():
            return candidate.resolve()
    searched = ", ".join(str(folder / filename) for folder in directories)
    raise FileNotFoundError(f"Missing {name}; searched {searched}. Build with CMake and set TERMFORGE_BIN_DIR.")
