# Metol Eye AF integration — development prototype

2026-11-26 device preparation update: `tools/extensions/build_eye_prepare.py`
now generates the pinned prepare/check/rollback installer. It has been tested
against a temporary fake root and installed on the development NX500 without
arming capture or restarting its current App. It preserves the exact audited T2
premount and uses a service drop-in rather than replacing the original unit.
`tools/extensions/build_eye_capture.py` derives protected OEM writers from the
verified backup. `capture/verify-before-capture.sh` checks the real capture
manager before preparation and before the header write; `capture/arm-rescue.sh`
requires explicit operator readiness and an unchanged backed-up snapshot.
These additions supersede the earlier statements below that no preparation
installer exists. They do not establish successful capture/resume or optical AF.

Withdrawing prepared files must follow this order before any capture is armed:
withdraw the writer guards with `capture/withdraw-guards.sh`, then run the pinned
prepare installer's `rollback BACKUP_DIRECTORY`. The prepare installer refuses
rollback while the header guard still references the runtime. After arming or
capture, do not run these preparation rollback commands blindly; inspect that
generation and restore this camera's verified snapshot as a separate operation.

Source: WApaperplane/NX500-metol nxwa commit 4a64b061d41da0701f303646cea096daf61c0b93, contrib/nx500-eye-af; capture and resume protocol adapted from NoireSchwarz/NX500_SRW_reprocessing_6.5K-oversampling commit eafc274e0ef0b6cb95b5ad958b2f1ac86dfc3562, release/0.4.0. Eye source is AGPL-3.0-only; model notice is in model/YUNET.md and model/YUNET-LICENSE.txt.

This is an uninstalled prototype, not an installer and not ready for camera use. launch.sh invokes claim.arm only during an eligible OEM capture boot and otherwise executes the original camera application. claim.arm creates an exclusive launch claim including the candidate library device and inode. wake.sh checks the generation, restored process, executable mapping and claim before publishing the existing PID token. Neither starts a key listener or restarts the application. The service file and di-camera-app drop-in are examples only: After= does not activate the service. An installer must establish and verify hib_post.service.wants linkage, preserve the actual original ExecStart and arguments, check the service graph before capture, and provide rollback.

The capture setup must atomically create identical armed and capture-origin records (first lines: previous boot ID, EYE), maintain the claim across capture and verify snapshot origin. It must also back up and byte-verify the actual device snapshot before requesting the OEM rescue flow. No such setup, write, rollback or uninstall implementation is provided. disable and restoring an old snapshot are separate operations. Do not create these proofs manually on a camera.

build_metol_bundle.py generates a verifier with static SHA256 values for the library, helper, scripts, service examples and licenses, and a manifest with the verifier hash. The deployment process must independently verify the manifest and verifier before enabling either service; a mutable verifier is not its own trust anchor. This packaging does not implement installation, capture preparation or snapshot recovery. Hardware state and service graph are unknown. Low-light hunting is a user report, not a verified fix. A token does not prove optical focus.

WSL build and replay (Python 3.10+, network for dependencies):

```
python3 -m venv /tmp/nx-eye-venv
/tmp/nx-eye-venv/bin/pip install ziglang==0.13.0 onnx==1.17.0 unicorn pyelftools numpy
cd /mnt/e/NXWA/nx500-modding-work/contrib/nx500-eye-af
/tmp/nx-eye-venv/bin/python build.py --zig /tmp/nx-eye-venv/lib/python3.12/site-packages/ziglang/zig --yunet --test
/tmp/nx-eye-venv/lib/python3.12/site-packages/ziglang/zig cc -target arm-linux-gnueabi.2.13 -mcpu=cortex_a9 -O2 -Wall -Wextra -Werror metol/claim.c -o out/claim.arm
python3 ../../test_server/eye-af/test_resume.py
python3 ../../test_server/eye-af/test_claim.py
python3 build_metol_bundle.py --output /tmp/nx-eye-bundle
python3 ../../test_server/eye-af/test_bundle.py
```

Adjust the zig path to the installed Python version. On the development machine the library SHA256 is 0955e5d90a61d33740c4fa18150e7499db4e959a536cd4c84a1eb04b3e587cbb and the claim helper is c2f141a7c6b0f81e585f6de73db0715a8db3ba6e399232c3e5729ad6d77b9f25. These hashes are evidence of this build, not an installation manifest or authorization to install. Tests use fake root and native host claim execution; ARM claim behavior and the complete capture/resume sequence have not been tested on hardware.
