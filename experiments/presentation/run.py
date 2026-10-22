"""Offscreen-only synthetic rendering checks; never capture the user's desktop."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from experiments.build_paths import binary


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--bin-dir", type=Path)
    cli.add_argument("--out", required=True, type=Path)
    args = cli.parse_args()
    exe = binary("presentation_probe", args.bin_dir)
    args.out.mkdir(parents=True, exist_ok=False)
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    metadata = dict(status="running", platform=platform.platform(), exe_sha256=sha(exe),
                    source_sha256=sha(Path(__file__).with_name("presentation.cpp")),
                    core_sources={name: sha(ROOT / "experiments/core" / name) for name in ("terminal.hpp", "screen.hpp", "unicode_width.hpp")},
                    target_font_family="Consolas", resolved_fallback_fonts=None,
                    capture_layer="owned offscreen DIB", window_verified=False, presented_done=None)
    try:
        with (args.out / "samples.jsonl").open("w") as output:
            for backend in ("gdi", "dwrite"):
                for dpi in (96, 144):
                    bmp = args.out / f"{backend}-{dpi}.bmp"
                    row = dict(backend=backend, dpi=dpi)
                    try:
                        result = subprocess.run([str(exe), backend, str(dpi), str(bmp.resolve())],
                                                capture_output=True, text=True, timeout=30)
                        row.update(exit_code=result.returncode, stderr=result.stderr, result=json.loads(result.stdout))
                        if result.returncode or not row["result"]["dirty_equals_full"] or row["result"]["lit_pixels"] <= 100:
                            raise RuntimeError("offscreen rendering probe failed")
                        row.update(final_bmp_sha256=sha(bmp), initial_bmp_sha256=sha(Path(str(bmp) + ".initial.bmp")))
                    except Exception as error:
                        row["error"] = str(error)
                        raise
                    finally:
                        output.write(json.dumps(row) + "\n"); output.flush()
        metadata["status"] = "complete"
    except Exception as error:
        metadata.update(status="failed", error=str(error))
        raise
    finally:
        (args.out / "manifest.json").write_text(json.dumps(metadata, indent=2) + "\n")


if __name__ == "__main__":
    main()
