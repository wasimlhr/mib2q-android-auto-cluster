#!/usr/bin/env python3
"""Stage the Android Auto 5.1 M.I.B. package (install_AndroidAutoUpmix_MoreIncredibleBash).

Builds both mirror drivers from their exact stock inputs with the reviewed
patch tools, pins the hook binary proven on the car, and writes the manifest
the on-car installer verifies.  Never touches an SD card.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from stage_android_auto import cksum  # noqa: E402

DEPLOY = ROOT / "android_auto/deploy"
# Hook build sq5-cluster-live-aa-mirror-v1: 5.1 by ear on MU0918 (2026-10-29);
# carries the MU1201 GAL fingerprint.
HOOK_SHA256 = "ef33bdf4aa0f6d9f9d85a5374fcb2a309cce597771dc1f879b10c21ba5998ea9"


def load(name: str):
    spec = importlib.util.spec_from_file_location(name, DEPLOY / f"{name}.py")
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(DEPLOY))
    spec.loader.exec_module(module)
    return module


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--output", type=Path, default=ROOT / "dist/android-auto-upmix-card")
    ap.add_argument("--hook", type=Path,
                    default=ROOT / "build/android_auto/libsq5_cluster_live.mpl6-mirror.so")
    ap.add_argument("--stock-080", type=Path, required=True,
                    help="stock deva-ctrl-qc.so 0.80.0 (MU0918)")
    ap.add_argument("--stock-084", type=Path, required=True,
                    help="stock deva-ctrl-qc.so 0.84.0 (MU1316/MU1201)")
    args = ap.parse_args()

    hook = args.hook.read_bytes()
    if hashlib.sha256(hook).hexdigest() != HOOK_SHA256:
        raise SystemExit(f"hook is not the reviewed build {HOOK_SHA256}")
    mirror_080, _ = load("patch_deva_mpl6_mirror_mpl1_mu0918").build_output(args.stock_080.read_bytes())
    mirror_084, _ = load("patch_deva_mpl6_mirror_mpl1_084").build_output(args.stock_084.read_bytes())

    out = args.output.resolve()
    if out.exists():
        shutil.rmtree(out)
    mod = out / "install_AndroidAutoUpmix_MoreIncredibleBash/mod"
    payload = mod / "upmix"
    payload.mkdir(parents=True)
    shutil.copy2(ROOT / "install_AndroidAutoUpmix_MoreIncredibleBash/mod/custom.sh", mod / "custom.sh")
    (payload / "libsq5_cluster_live.so").write_bytes(hook)
    shutil.copy2(DEPLOY / "gal_startup_standalone.sh", payload / "gal_startup.sh")
    (payload / "deva-ctrl-qc.so.mirror-080").write_bytes(mirror_080)
    (payload / "deva-ctrl-qc.so.mirror-084").write_bytes(mirror_084)
    for path in (mod / "custom.sh", payload / "gal_startup.sh"):
        if b"\r" in path.read_bytes():
            raise SystemExit(f"CRLF in {path}")

    names = ["deva-ctrl-qc.so.mirror-080", "deva-ctrl-qc.so.mirror-084",
             "gal_startup.sh", "libsq5_cluster_live.so"]
    manifest = "".join(f"{c} {s} {n}\n" for n in names for c, s in [cksum(payload / n)])
    (payload / "manifest.cksum").write_text(manifest, encoding="ascii", newline="\n")
    (out / "README.txt").write_text(
        "Android Auto 5.1 (stereo -> 5.1 upmix) for an installed mib2q-android-auto-cluster.\n"
        "Needs the external B&O/Bose MOST amplifier; refused on the internal amplifier.\n"
        "Copy this folder's contents into the M.I.B. SD-card root and run the\n"
        "AndroidAutoUpmix custom script once with no marker: read-only audit plus logs.\n"
        "Create mib2q_aa_upmix_install in the SD-card root and run it again to install,\n"
        "then reboot the MMI. mib2q_aa_upmix_remove restores the stock driver, hook and\n"
        "startup script from the backup. sq5_aa_upmix_off on the card plays stock stereo\n"
        "for one boot. Logs go to mib2q-aa-logs/upmix-* on the card.\n"
        "Run remove before uninstalling or reinstalling the cluster mod.\n",
        encoding="utf-8", newline="\n",
    )
    print(f"Staged {out}")
    print(manifest, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
