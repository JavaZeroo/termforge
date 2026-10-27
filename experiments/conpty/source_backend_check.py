"""Actual identity, API lifetime and rejection checks for fixed source pairs."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil

from experiments.conpty.margin_run import backend_path, sha256
from experiments.conpty.source_margin_run import reviewed_pair, identity, RUN
from experiments.conpty.test_backend import BackendTests


def run(baseline, patched, official, probe_directory, output):
    pairs = {name: reviewed_pair(folder, name)[0] for name, folder in
             (("baseline", baseline), ("patched", patched))}
    official = Path(backend_path(str(Path(official).resolve() / "conpty.dll"))).parent
    probes = Path(probe_directory).resolve(strict=True)
    source_probe = probes / "conpty_source_backend_probe.exe"
    official_probe = probes / "conpty_backend_probe.exe"
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    subject = BackendTests()
    subject.out, subject.serial = output / "_private", 0
    result = {"schema": "termforge-source-conpty-backend-check-v1", "build_run": RUN,
              "probe_sha256": {p.name: sha256(p) for p in (source_probe, official_probe)},
              "native_cases": {}, "receipt_rejections": [], "passed": False,
              "product_backend_selected": False, "full_r2_contract_verified": False}

    def invoke(name, directory, accepted, *, variant=None, lifecycle=False, executable=source_probe):
        row = subject.invoke(directory, "--host" if lifecycle else "--identity", executable=executable)
        result["native_cases"][name] = row
        if not accepted:
            subject.assertEqual(row["returncode"], 2)
            subject.assertIsNone(row["native"])
            # Job totals can include the hidden console host; retain the
            # observed count rather than infer child creation from it.
            subject.assertEqual(row["outer_job_after"]["active"], 0)
            subject.assertEqual(row["outer_job_after"]["terminated"], 0)
            subject.assertGreaterEqual(row["outer_job_after"]["total"], 1)
            return
        expected_identity = "system" if variant is None else identity(variant)
        subject.assertEqual(row["native"]["backend_identity"], expected_identity)
        if lifecycle:
            checked = dict(row, native={k: v for k, v in row["native"].items() if k != "backend_identity"})
            subject.check_lifecycle(checked, variant is not None)
        else:
            subject.assertEqual(row["returncode"], 0)
            subject.assertEqual(row["native"], {"identity_accepted": True, "backend_identity": expected_identity})

    def reject_receipt(name, directory, variant):
        try:
            reviewed_pair(directory, variant)
        except ValueError:
            result["receipt_rejections"].append(name)
        else:
            raise AssertionError("changed receipt accepted")

    try:
        for variant, directory in pairs.items():
            invoke(f"{variant}-identity", directory, True, variant=variant)
            invoke(f"{variant}-lifecycle", directory, True, variant=variant, lifecycle=True)
            invoke(f"official-rejects-{variant}", directory, False, executable=official_probe)
            for name in ("conpty.dll", "OpenConsole.exe"):
                changed = output / "_private" / f"{variant}-tamper-{name}"
                changed.mkdir(parents=True)
                for member in ("conpty.dll", "OpenConsole.exe"):
                    shutil.copyfile(directory / member, changed / member)
                payload = bytearray((changed / name).read_bytes())
                payload[-1] ^= 1
                (changed / name).write_bytes(payload)
                invoke(f"{variant}-rejects-{name}-tamper", changed, False)
            reject_receipt(f"{variant}-wrong-variant", directory,
                           "patched" if variant == "baseline" else "baseline")
            changed = output / "_private" / f"{variant}-receipt"
            shutil.copytree(directory, changed)
            receipt = json.loads((changed / "variant.json").read_text(encoding="utf-8-sig"))
            receipt["production_patch_applied"] = int(receipt["production_patch_applied"])
            (changed / "variant.json").write_text(json.dumps(receipt), encoding="utf-8")
            reject_receipt(f"{variant}-numeric-boolean", changed, variant)
            shutil.copyfile(directory / "variant.json", changed / "variant.json")
            with (changed / "summary.json").open("ab") as file:
                file.write(b" ")
            reject_receipt(f"{variant}-summary-bytes", changed, variant)
            shutil.copyfile(directory / "summary.json", changed / "summary.json")
            payload = bytearray((changed / "OpenConsole.exe").read_bytes())
            payload[-1] ^= 1
            (changed / "OpenConsole.exe").write_bytes(payload)
            reject_receipt(f"{variant}-binary-bytes", changed, variant)
        invoke("source-rejects-official", official, False)
        invoke("source-rejects-relative", "relative-directory", False)
        missing = output / "_private" / "empty"
        missing.mkdir()
        invoke("source-rejects-missing", missing, False)
        for name in ("conpty.dll", "OpenConsole.exe"):
            (missing / name).write_bytes(b"unreviewed")
        invoke("source-rejects-unknown", missing, False)
        invoke("system-lifecycle", "system", True, lifecycle=True)
        result["passed"] = True
    finally:
        (output / "summary.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("baseline", "patched", "official", "probe-directory", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    result = run(args.baseline, args.patched, args.official, args.probe_directory, args.output)
    print(json.dumps({"passed": result["passed"], "native_cases": len(result["native_cases"]),
                      "receipt_rejections": len(result["receipt_rejections"])}))
    return int(not result["passed"])


if __name__ == "__main__":
    raise SystemExit(main())
