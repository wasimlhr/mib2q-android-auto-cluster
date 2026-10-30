#!/usr/bin/env python3
"""Mirror mpl1_int_ent's six-channel record onto mpl6_gal_ent, deva 0.84.0.

The same change as patch_deva_mpl6_mirror_mpl1_mu0918.py, for the
deva-ctrl-qc.so "version 0.84.0 on [Jan 30 2018]" build.  That is the driver in
Luka's MU1316 tree (deploy/deva-ctrl-qc/deva-ctrl-qc.so.stock), and the MU1201
(MHI2Q_KR_AUG22_P1201) unit's slog reports the same version and build date.
Only that exact file (SHA-256 below) is accepted; any other input is refused
without changing a byte.  The installer checks the unit's own driver against
the same stock checksum before replacing it.

mpl6 is stock S16 stereo here too, so the mirror also changes the format
index (+0x04, 4 -> 12) and the bits field (+0x2c, 16 -> 24), giving the record
mpl1 attaches with: 6 voices, S32 with 24 useful bits, 768-frame periods.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import struct
import sys
import tempfile
from pathlib import Path

STOCK_SIZE = 694_904
STOCK_SHA256 = "58dddcabaa94c2fecf989855a6f810ec219926f49669a0ed9482ed629fdc64b9"
MIRROR_SHA256 = "c03b090993470dab5c2cb4dceb22eb6685413c02e67e2b5ec62dba052b5033cf"

DEVICE_NAMES_BASE = 0xA4C54
DEVICE_NAME_OFFSET = 0x10
DEVICE_STRIDE = 0x110
CAPABILITY_RECORD_SIZE = 0x30
MPL1_RECORD_OFFSET = 0xA6D74
MPL6_RECORD_OFFSET = 0xA6E34

MPL1_REFERENCE = (
    0x2B3, 0xC, 0x80, 0xBB80, 0xBB80,
    0x6, 0x6, 0x4800, 0x4800, 0x2D000, 0xA, 0x18,
)
MPL6_STOCK = (
    0x2B3, 0x4, 0x80, 0xBB80, 0xBB80,
    0x2, 0x2, 0x0C00, 0x0C00, 0x07800, 0xA, 0x10,
)
EXPECTED_NAMES = {1: b"mpl1_int_ent", 4: b"mpl5_dio_ent", 5: b"mpl6_gal_ent"}

# The only bytes which differ between the verified stock and mirror hashes.
EXPECTED_CHANGED_OFFSETS = {
    0xA6E38,  # format index: 4 (S16_LE) -> 12 (S32_LE)
    0xA6E48,  # min voices: 2 -> 6
    0xA6E4C,  # max voices: 2 -> 6
    0xA6E51,  # min fragment: 0x0c00 -> 0x4800
    0xA6E55,  # max fragment: 0x0c00 -> 0x4800
    0xA6E59,  # max buffer: 0x07800 -> 0x2d000
    0xA6E5A,
    0xA6E60,  # bits: 16 -> 24
}


class PatchError(RuntimeError):
    """The input is not the one exact driver build this patch supports."""


def sha256(data: bytes | bytearray) -> str:
    return hashlib.sha256(data).hexdigest()


def _record(data: bytes | bytearray, offset: int) -> tuple[int, ...]:
    return struct.unpack_from("<12I", data, offset)


def _device_name(data: bytes | bytearray, index: int) -> bytes:
    offset = DEVICE_NAMES_BASE + index * DEVICE_STRIDE + DEVICE_NAME_OFFSET
    return bytes(data[offset : offset + 32]).split(b"\0", 1)[0]


def validate_layout(data: bytes | bytearray, *, expect_mirror: bool) -> None:
    if len(data) != STOCK_SIZE:
        raise PatchError(f"wrong file size: {len(data)} (expected {STOCK_SIZE})")
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        raise PatchError("not the expected ELF32 little-endian QNX ARM image")
    for index, expected in EXPECTED_NAMES.items():
        actual = _device_name(data, index)
        if actual != expected:
            raise PatchError(f"device table mismatch at index {index}: {actual!r} != {expected!r}")
    if _record(data, MPL1_RECORD_OFFSET) != MPL1_REFERENCE:
        raise PatchError("MPL1 six-channel reference record does not match deva 0.84.0")
    expected_mpl6 = MPL1_REFERENCE if expect_mirror else MPL6_STOCK
    if _record(data, MPL6_RECORD_OFFSET) != expected_mpl6:
        state = "mirror" if expect_mirror else "stock"
        raise PatchError(f"MPL6 capability record is not the verified {state} record")


def patch_verified_stock(data: bytes) -> bytes:
    """Mirror a caller-verified stock image, with structural and diff checks."""
    validate_layout(data, expect_mirror=False)
    patched = bytearray(data)
    patched[MPL6_RECORD_OFFSET:MPL6_RECORD_OFFSET + CAPABILITY_RECORD_SIZE] = \
        data[MPL1_RECORD_OFFSET:MPL1_RECORD_OFFSET + CAPABILITY_RECORD_SIZE]
    changed = {i for i, (before, after) in enumerate(zip(data, patched)) if before != after}
    if changed != EXPECTED_CHANGED_OFFSETS:
        raise PatchError(
            f"unexpected patch footprint: {sorted(changed)}; "
            f"expected {sorted(EXPECTED_CHANGED_OFFSETS)}"
        )
    validate_layout(patched, expect_mirror=True)
    return bytes(patched)


def build_output(data: bytes) -> tuple[bytes, str]:
    actual_hash = sha256(data)
    if actual_hash == MIRROR_SHA256:
        validate_layout(data, expect_mirror=True)
        return data, "already-mirrored"
    if actual_hash != STOCK_SHA256:
        raise PatchError(
            "unsupported deva-ctrl-qc.so: "
            f"size={len(data)} sha256={actual_hash}; no bytes were changed"
        )
    patched = patch_verified_stock(data)
    if sha256(patched) != MIRROR_SHA256:
        raise PatchError(f"internal output hash mismatch: {sha256(patched)} != {MIRROR_SHA256}")
    return patched, "mirrored"


def write_atomic(path: Path, data: bytes, mode: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(fd, "wb") as fh:
            fh.write(data)
            fh.flush()
            os.fsync(fh.fileno())
        os.chmod(temporary, mode)
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Mirror mpl1_int_ent's six-channel record onto mpl6_gal_ent (deva 0.84.0 only)"
    )
    parser.add_argument("input", type=Path, help="stock deva-ctrl-qc.so, version 0.84.0")
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
