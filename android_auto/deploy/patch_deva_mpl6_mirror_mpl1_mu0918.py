#!/usr/bin/env python3
"""Make MU0918's mpl6_gal_ent capability record identical to mpl1_int_ent.

This is Luka's proven step 3 (deploy/deva-ctrl-qc/patch_deva_mirror_mpl1.sh)
applied to the device GAL actually plays through.  Run 319 showed Android Auto
media on mpl6 (router Input=6 -> Output=128) and mpl1 connected only after AA
stopped, so the six-channel geometry has to live on mpl6 itself.

Luka's mpl5 record already carried mpl1's S32 format index and 24-bit width,
so his chain only changed voices and fragment sizes.  mpl6 is stock S16, so a
true mirror also changes those two fields:

  +0x04 format index 4 (S16_LE) -> 12 (S32_LE)   deva_mib2_capabilities sets
                                                 info.formats = 1 << this
  +0x2c bits         16         -> 24            deva_update_session_info_from_list
                                                 copies it to the CSD session

The result is the record mpl1 attaches with at every boot on this unit
("pcm format => ... channels=6, bit_per_sample=24").  This tool never patches
an unknown file and never modifies its input.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

from patch_deva_mpl6_mu0918 import (
    CAPABILITY_RECORD_SIZE,
    MPL1_RECORD_OFFSET,
    MPL1_REFERENCE,
    MPL6_RECORD_OFFSET,
    MPL6_STOCK,
    STOCK_SHA256,
    PatchError,
    _record,
    sha256,
    validate_layout,
    write_atomic,
)

MIRROR_SHA256 = "a7a4746fd4c56712c204e130ea3647b0766d2f32c938855ebaefc7256ea6de68"
MPL6_MIRROR = MPL1_REFERENCE

# The only bytes which differ between the verified stock and mirror hashes.
EXPECTED_CHANGED_OFFSETS = {
    0xA5D78,  # format index: 4 (S16_LE) -> 12 (S32_LE)
    0xA5D88,  # min voices: 2 -> 6
    0xA5D8C,  # max voices: 2 -> 6
    0xA5D91,  # min fragment: 0x0c00 -> 0x4800
    0xA5D95,  # max fragment: 0x0c00 -> 0x4800
    0xA5D99,  # max buffer: 0x07800 -> 0x2d000
    0xA5D9A,
    0xA5DA0,  # bits: 16 -> 24
}


def validate_mirror(data: bytes | bytearray) -> None:
    # Reuse the stock layout check for size, ELF header, device names and
    # mpl1's record by checking a copy whose mpl6 record is put back to stock.
    shadow = bytearray(data)
    struct.pack_into("<12I", shadow, MPL6_RECORD_OFFSET, *MPL6_STOCK)
    validate_layout(shadow, expect_patched=False)
    if _record(data, MPL6_RECORD_OFFSET) != MPL6_MIRROR:
        raise PatchError("MPL6 capability record is not the mpl1 mirror")
    if (bytes(data[MPL6_RECORD_OFFSET:MPL6_RECORD_OFFSET + CAPABILITY_RECORD_SIZE]) !=
            bytes(data[MPL1_RECORD_OFFSET:MPL1_RECORD_OFFSET + CAPABILITY_RECORD_SIZE])):
        raise PatchError("MPL6 record bytes differ from MPL1")


def patch_verified_stock(data: bytes) -> bytes:
    """Mirror a caller-verified stock image, with structural and diff checks."""
    validate_layout(data, expect_patched=False)
    patched = bytearray(data)
    patched[MPL6_RECORD_OFFSET:MPL6_RECORD_OFFSET + CAPABILITY_RECORD_SIZE] = \
        data[MPL1_RECORD_OFFSET:MPL1_RECORD_OFFSET + CAPABILITY_RECORD_SIZE]

    changed = {i for i, (before, after) in enumerate(zip(data, patched)) if before != after}
    if changed != EXPECTED_CHANGED_OFFSETS:
        raise PatchError(
            f"unexpected patch footprint: {sorted(changed)}; "
            f"expected {sorted(EXPECTED_CHANGED_OFFSETS)}"
        )
    validate_mirror(patched)
    return bytes(patched)


def build_output(data: bytes) -> tuple[bytes, str]:
    actual_hash = sha256(data)
    if actual_hash == MIRROR_SHA256:
        validate_mirror(data)
        return data, "already-mirrored"
    if actual_hash != STOCK_SHA256:
        raise PatchError(
            "unsupported deva-ctrl-qc.so: "
            f"size={len(data)} sha256={actual_hash}; no bytes were changed"
        )
    patched = patch_verified_stock(data)
    actual_patched_hash = sha256(patched)
    if actual_patched_hash != MIRROR_SHA256:
        raise PatchError(
            f"internal output hash mismatch: {actual_patched_hash} != {MIRROR_SHA256}"
        )
    return patched, "mirrored"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Mirror mpl1_int_ent's six-channel record onto mpl6_gal_ent (MU0918 only)"
    )
    parser.add_argument("input", type=Path, help="collected stock deva-ctrl-qc.so")
    parser.add_argument("output", type=Path, help="separate output path")
    parser.add_argument("--force", action="store_true", help="replace an existing output")
    args = parser.parse_args()

    try:
        input_path = args.input.resolve(strict=True)
        output_path = args.output.resolve(strict=False)
        if input_path == output_path:
            raise PatchError("refusing in-place patch; choose a separate output path")
        if output_path.exists() and not args.force:
            raise PatchError(f"output already exists: {output_path} (use --force to replace)")
        source = input_path.read_bytes()
        output, result = build_output(source)
        write_atomic(output_path, output, input_path.stat().st_mode & 0o777)
        print(f"result={result}")
        print(f"input_sha256={sha256(source)}")
        print(f"output_sha256={sha256(output)}")
        print(f"output={output_path}")
        return 0
    except (OSError, PatchError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
