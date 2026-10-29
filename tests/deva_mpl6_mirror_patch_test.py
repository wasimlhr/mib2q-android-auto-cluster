#!/usr/bin/env python3
"""Host tests for the MU0918 mpl6_gal_ent <- mpl1_int_ent record mirror."""

from __future__ import annotations

import importlib.util
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEPLOY = ROOT / "android_auto/deploy"
sys.path.insert(0, str(DEPLOY))
PATCHER = DEPLOY / "patch_deva_mpl6_mirror_mpl1_mu0918.py"
SPEC = importlib.util.spec_from_file_location("patch_deva_mpl6_mirror_mpl1_mu0918", PATCHER)
assert SPEC and SPEC.loader
mirror = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(mirror)
import patch_deva_mpl6_mu0918 as base  # noqa: E402


def fixture() -> bytes:
    data = bytearray(base.STOCK_SIZE)
    data[:6] = b"\x7fELF\x01\x01"
    for index, name in {
        base.MPL1_INDEX: b"mpl1_int_ent",
        base.MPL5_INDEX: b"mpl5_dio_ent",
        base.MPL6_INDEX: b"mpl6_gal_ent",
    }.items():
        offset = base.DEVICE_NAMES_BASE + index * base.DEVICE_STRIDE + base.DEVICE_NAME_OFFSET
        data[offset : offset + len(name) + 1] = name + b"\0"
    struct.pack_into("<12I", data, base.MPL1_RECORD_OFFSET, *base.MPL1_REFERENCE)
    struct.pack_into("<12I", data, base.MPL6_RECORD_OFFSET, *base.MPL6_STOCK)
    return bytes(data)


def main() -> int:
    stock = fixture()
    result = mirror.patch_verified_stock(stock)
    mirror.validate_mirror(result)
    changed = {i for i, (before, after) in enumerate(zip(stock, result)) if before != after}
    assert changed == mirror.EXPECTED_CHANGED_OFFSETS

    # Same geometry mpl1 attaches with on this unit: six S32 voices
    # (24 useful bits), 0x4800 / (6 * 4) = 768 frames per 16 ms, ten periods.
    record = struct.unpack_from("<12I", result, base.MPL6_RECORD_OFFSET)
    assert record == base.MPL1_REFERENCE
    fmt_index, voices, fragment, buffer_bytes, bits = (
        record[1], record[5], record[7], record[9], record[11])
    assert 1 << fmt_index == 0x1000  # SND_PCM_FMT_S32_LE
    assert bits == 24
    assert fragment // (voices * 4) * 1000 // 16 == 48000
    assert buffer_bytes == fragment * 10

    for offset in (base.MPL6_RECORD_OFFSET, base.MPL1_RECORD_OFFSET):
        bad = bytearray(stock)
        bad[offset] ^= 1
        try:
            mirror.patch_verified_stock(bytes(bad))
        except base.PatchError:
            pass
        else:
            raise AssertionError(f"mismatched record at {offset:#x} was accepted")

    try:
        mirror.build_output(stock)
    except base.PatchError as exc:
        assert "unsupported" in str(exc)
    else:
        raise AssertionError("synthetic image bypassed the exact SHA-256 gate")

    print("PASS: MU0918 mpl6 mirror of mpl1 is exact and fail-closed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
