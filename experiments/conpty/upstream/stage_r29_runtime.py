"""Stage one verified source-built pair in an isolated runtime CI checkout."""
from __future__ import annotations

import argparse
import hashlib
import io
import json
from pathlib import Path
import subprocess
import zipfile


def api(path: str) -> bytes:
    return subprocess.check_output(["gh", "api", path])


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=int, required=True)
    parser.add_argument("--build-head", required=True)
    parser.add_argument("--variant", choices=("baseline", "candidate"), required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    base = "repos/JavaZeroo/termforge/actions"
    run = json.loads(api(f"{base}/runs/{args.run}"))
    if (
        run["head_branch"] != "codex/r29-native-build"
        or run["head_sha"] != args.build_head
        or run["event"] != "workflow_dispatch"
        or run["conclusion"] != "success"
    ):
        raise ValueError("Unexpected build run identity or outcome")
    artifacts = json.loads(api(f"{base}/runs/{args.run}/artifacts"))["artifacts"]
    artifact, = [entry for entry in artifacts if entry["name"] == f"r29-native-depth-{args.variant}"]
    archive = api(f"{base}/artifacts/{artifact['id']}/zip")
    if f"sha256:{sha(archive)}" != artifact["digest"]:
        raise ValueError("Variant archive digest differs")
    names = ("conpty.dll", "OpenConsole.exe")
    with zipfile.ZipFile(io.BytesIO(archive)) as zipped:
        data = {name: zipped.read(name) for name in names}
        summary = json.loads(zipped.read("summary.json").decode("utf-8-sig"))
    if (
        summary["termforge_source"] != args.build_head
        or int(summary["workflow_run"]) != args.run
        or summary["upstream_source"] != "dc8ae365847d41f62d1ebca93815bb00959bc12f"
        or summary["status"] != "built"
    ):
        raise ValueError("Source build metadata differs")
    actual = {name: sha(data[name]) for name in names}
    if any(actual[name] != summary[f"{args.variant}_files"][name]["sha256"] for name in names):
        raise ValueError("Variant binary digest differs")

    # This branch workflow changes an ephemeral build checkout only. Both
    # CMake and the original runtime loader still enforce the selected hashes.
    manifest_path = Path("third_party/conpty/manifest.json")
    backend_path = Path("experiments/conpty/backend.hpp")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    backend = backend_path.read_text(encoding="utf-8")
    for name, old in manifest["files"].items():
        if backend.count(old) != 1:
            raise ValueError(f"Unexpected runtime hash anchor for {name}")
        backend = backend.replace(old, actual[name])
    manifest["files"] = actual
    args.out.mkdir(exist_ok=False, parents=True)
    for name in names:
        (args.out / name).write_bytes(data[name])
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    backend_path.write_text(backend, encoding="utf-8")
    print(json.dumps({"files": actual, "artifact": artifact["id"], "archive_sha256": sha(archive)}))


if __name__ == "__main__":
    main()
