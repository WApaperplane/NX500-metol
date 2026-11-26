"""Build an isolated Log117 payload from the pinned public Unified R1 ZIP.

No camera operation is performed. Upstream bytes and internal hash bindings are
preserved. The shell-reference closure is conservative (includes diagnostic
branches); it does not include the upstream installer, key listener or core lib.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import zipfile

ZIP_SHA = "49de7be7e19899526556178e386b5beafb43c2049f43ebf51b7b41e33e5fe766"
COMMIT = "eafc274e0ef0b6cb95b5ad958b2f1ac86dfc3562"
PREFIX = "module/nx-lut-demo/nx-lut-lite/"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def build(archive, output):
    if sha(archive.read_bytes()) != ZIP_SHA:
        raise ValueError("UPSTREAM_ARCHIVE_IDENTITY_FAILED")
    if output.exists():
        raise ValueError("OUTPUT_MUST_BE_NEW")
    with zipfile.ZipFile(archive) as z:
        pins = {}
        for line in z.read("module/nx-lut-demo/module.manifest").decode().splitlines():
            digest, name = line.split()
            p = PurePosixPath(name)
            if p.is_absolute() or ".." in p.parts or "\\" in name or ":" in name:
                raise ValueError("UNSAFE_UPSTREAM_PATH")
            pins[name] = digest
        todo = ["gamma-reload.sh", "LICENSE", "NOTICE.md", "STRENGTHS_NOTICE.md",
                "data/clean-log/NXL_STILL_FLAT117.bin"]
        payload = {}
        runtime = set()
        while todo:
            name = todo.pop()
            if name in payload:
                continue
            if name not in pins:
                raise ValueError(f"DEPENDENCY_NOT_PINNED: {name}")
            data = z.read(PREFIX + name)
            if sha(data) != pins[name]:
                raise ValueError(f"UPSTREAM_FILE_IDENTITY_FAILED: {name}")
            payload[name] = data
            if name.endswith(".sh"):
                for dep in re.findall(r"\$D/([A-Za-z0-9_./-]+)", data.decode()):
                    if dep == "state" or dep.startswith("state/"):
                        runtime.add(dep)
                    else:
                        todo.append(dep)
        # Validate every included original hash binding that names an included
        # artifact. They remain byte-for-byte identical to the release.
        known = set(pins.values())
        for name, data in payload.items():
            if name.endswith(".sh"):
                for pin in re.findall(rb"(?<![a-f0-9])[a-f0-9]{64}(?![a-f0-9])", data):
                    if pin.decode() in known:
                        matches = [n for n, h in pins.items() if h == pin.decode()]
                        if not any(n in payload for n in matches):
                            raise ValueError(f"HASH_DEPENDENCY_NOT_INCLUDED: {name}: {matches}")
        # Rootfs has no bare sha256sum. Keep the shim private to this engine.
        payload["compat/sha256sum"] = b'#!/bin/sh\nexec /opt/usr/nx-ks/busybox sha256sum "$@"\n'
        checksums = "".join(f"{sha(b)}  {n}\n" for n, b in sorted(payload.items())).encode()
        manifest = {
            "schema": 1, "schema_version": 1, "profile": "log117", "model": "NX500", "firmware": "1.12",
            "upstream": {"repository": "NoireSchwarz/NX500_SRW_reprocessing_6.5K-oversampling",
                         "commit": COMMIT, "archive_sha256": ZIP_SHA},
            "target_root": "/opt/usr/nx-ks/gamma",
            "files": [{"path": n, "sha256": sha(b),
                       "mode": "0755" if n.endswith(".sh") or n.startswith(("bin/", "compat/")) else "0644"}
                      for n, b in sorted(payload.items())],
            "runtime_paths": sorted(runtime),
            "validation": "host-identity-only; camera activation not verified",
            "camera_install_requested": False,
        }
        # The camera installer needs this pinned table too, so PC staging must
        # carry it as a verified payload file (it does not hash itself).
        manifest["files"].append({"path": "payload.sha256", "sha256": sha(checksums), "mode": "0644"})
        output.mkdir(parents=True)
        for name, data in payload.items():
            path = output / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        (output / "payload.sha256").write_bytes(checksums)
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        return manifest


if __name__ == "__main__":
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("archive", type=Path)
    cli.add_argument("output", type=Path)
    args = cli.parse_args()
    print(json.dumps(build(args.archive, args.output), indent=2))
