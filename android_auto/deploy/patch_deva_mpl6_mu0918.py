#!/usr/bin/env python3
"""Create the exact MU0918 six-channel GAL QSA driver variant.

This tool never patches an unknown file and never modifies its input.  It is
deliberately tied to the deva-ctrl-qc.so collected from
MHI2Q_US_AUG22_P3639 MU0918.  Other firmware needs its own verified profile.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import struct
import sys
import tempfile
from pathlib import Path

STOCK_SIZE = 690_332
STOCK_SHA256 = "5cc4c61e2e3a771dd21ff000ae1cd12eebebfa006bd7879df93f30b3b1fc0c63"
PATCHED_SHA256 = "e92d2efff1dc2dd83df1879a7a2bea01569b45741ae88ba691cdf7d9b71f506f"

DEVICE_NAMES_BASE = 0xA40E4
DEVICE_NAME_OFFSET = 0x10
DEVICE_STRIDE = 0x110
MPL1_INDEX = 1
MPL5_INDEX = 4
MPL6_INDEX = 5

CAPABILITY_RECORD_SIZE = 0x30
MPL1_RECORD_OFFSET = 0xA5CB4
MPL6_RECORD_OFFSET = 0xA5D74

MPL1_REFERENCE = (
    0x2B3, 0xC, 0x80, 0xBB80, 0xBB80,
    0x6, 0x6, 0x4800, 0x4800, 0x2D000, 0xA, 0x18,
)
MPL6_STOCK = (
    0x2B3, 0x4, 0x80, 0xBB80, 0xBB80,
    0x2, 0x2, 0x0C00, 0x0C00, 0x07800, 0xA, 0x10,
)
MPL6_PATCHED = (
    0x2B3, 0x4, 0x80, 0xBB80, 0xBB80,
    0x6, 0x6, 0x2400, 0x2400, 0x16800, 0xA, 0x10,
)

# These are the only bytes which differ between the verified stock and output
# hashes.  Keeping the byte-level list makes an accidental broader edit fatal.
EXPECTED_CHANGED_OFFSETS = {
    0xA5D88,  # min voices: 2 -> 6
    0xA5D8C,  # max voices: 2 -> 6
    0xA5D91,  # min fragment: 0x0c00 -> 0x2400
    0xA5D95,  # max fragment: 0x0c00 -> 0x2400
    0xA5D99,  # max buffer: 0x07800 -> 0x16800
    0xA5D9A,
}


class PatchError(RuntimeError):
    """The input is not the one exact firmware image this patch supports."""


def sha256(data: bytes | bytearray) -> str:
    return hashlib.sha256(data).hexdigest()


def _record(data: bytes | bytearray, offset: int) -> tuple[int, ...]:
    return struct.unpack_from("<12I", data, offset)


def _device_name(data: bytes | bytearray, index: int) -> bytes:
    offset = DEVICE_NAMES_BASE + index * DEVICE_STRIDE + DEVICE_NAME_OFFSET
    return bytes(data[offset : offset + 32]).split(b"\0", 1)[0]


def validate_layout(data: bytes | bytearray, *, expect_patched: bool) -> None:
    if len(data) != STOCK_SIZE:
        raise PatchError(f"wrong file size: {len(data)} (expected {STOCK_SIZE})")
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        raise PatchError("not the expected ELF32 little-endian QNX ARM image")

    expected_names = {
        MPL1_INDEX: b"mpl1_int_ent",
        MPL5_INDEX: b"mpl5_dio_ent",
        MPL6_INDEX: b"mpl6_gal_ent",
    }
    for index, expected in expected_names.items():
        actual = _device_name(data, index)
        if actual != expected:
            raise PatchError(
                f"device table mismatch at index {index}: {actual!r} != {expected!r}"
            )

    if _record(data, MPL1_RECORD_OFFSET) != MPL1_REFERENCE:
        raise PatchError("MPL1 six-channel reference record does not match MU0918")
    expected_mpl6 = MPL6_PATCHED if expect_patched else MPL6_STOCK
    if _record(data, MPL6_RECORD_OFFSET) != expected_mpl6:
        state = "patched" if expect_patched else "stock"
        raise PatchError(f"MPL6 capability record is not the verified {state} record")


def patch_verified_stock(data: bytes) -> bytes:
    """Patch a caller-verified stock image, with structural and diff checks."""
    validate_layout(data, expect_patched=False)
    patched = bytearray(data)
    for index in (5, 6, 7, 8, 9):
        struct.pack_into("<I", patched, MPL6_RECORD_OFFSET + index * 4, MPL6_PATCHED[index])

    changed = {i for i, (before, after) in enumerate(zip(data, patched)) if before != after}
    if changed != EXPECTED_CHANGED_OFFSETS:
        raise PatchError(
            f"unexpected patch footprint: {sorted(changed)}; "
            f"expected {sorted(EXPECTED_CHANGED_OFFSETS)}"
        )
    validate_layout(patched, expect_patched=True)
    return bytes(patched)


def build_output(data: bytes) -> tuple[bytes, str]:
    actual_hash = sha256(data)
    if actual_hash == PATCHED_SHA256:
        validate_layout(data, expect_patched=True)
        return data, "already-patched"
    if actual_hash != STOCK_SHA256:
        raise PatchError(
            "unsupported deva-ctrl-qc.so: "
            f"size={len(data)} sha256={actual_hash}; no bytes were changed"
        )
    patched = patch_verified_stock(data)
    actual_patched_hash = sha256(patched)
    if actual_patched_hash != PATCHED_SHA256:
        raise PatchError(
            f"internal output hash mismatch: {actual_patched_hash} != {PATCHED_SHA256}"
        )
    return patched, "patched"


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
        description="Create a six-channel mpl6_gal_ent driver for exact MU0918 firmware"
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
