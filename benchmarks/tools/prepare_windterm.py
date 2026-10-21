"""Prepare a fresh local lab from the pinned official WindTerm 2.7.0 x64 archive."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ARCHIVE_SHA256 = "b05c3c7371340ff1302967247ffcd3a3ba27b237f72010e9157086143c453e50"
EXE_SHA256 = "7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0"
URL = "https://github.com/kingToolbox/WindTerm/releases/download/2.7.0/WindTerm_2.7.0_Windows_Portable_x86_64.zip"


def prepare(archive, output):
    archive, output = Path(archive), Path(output).resolve()
    if hashlib.sha256(archive.read_bytes()).hexdigest() != ARCHIVE_SHA256:
        raise ValueError("Archive differs from pinned official 2.7.0 x64 release")
    output.mkdir(parents=True, exist_ok=False)
    with zipfile.ZipFile(archive) as package:
        for entry in package.infolist():
            if not (output / entry.filename).resolve().is_relative_to(output):
                raise ValueError("Archive member escapes lab directory")
        package.extractall(output)
    app_dir = output / "WindTerm_2.7.0"
    exe = app_dir / "WindTerm.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != EXE_SHA256:
        raise ValueError("Executable differs from pinned release")
    profiles = output / "profile-home"
    profiles.mkdir()
    # Inspectable locator only; never read/copy user sessions, history or secrets.
    with (app_dir / "profiles.config").open("x", encoding="utf-8") as locator:
        json.dump({"path": profiles.as_posix()}, locator)
        locator.write("\n")
    metadata = {"kind": "windterm_isolated_lab", "release": "2.7.0",
                "source_url": URL, "archive_sha256": ARCHIVE_SHA256,
                "exe_sha256": EXE_SHA256, "exe_bytes": exe.stat().st_size,
                "profile_locator": "WindTerm_2.7.0/profiles.config",
                "profile_home": "profile-home", "runtime_isolation_verified": False,
                "settings_verified": False, "baseline_measured": False}
    (output / "lab.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    return metadata


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.archive, args.out), indent=2))
