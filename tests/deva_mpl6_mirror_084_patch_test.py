#!/usr/bin/env python3
"""Host tests for the deva 0.84.0 mpl6_gal_ent <- mpl1_int_ent record mirror."""

from __future__ import annotations

import importlib.util
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATCHER = ROOT / "android_auto/deploy/patch_deva_mpl6_mirror_mpl1_084.py"
SPEC = importlib.util.spec_from_file_location("patch_deva_mpl6_mirror_mpl1_084", PATCHER)
assert SPEC and SPEC.loader
mirror = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(mirror)


def fixture() -> bytes:
    data = bytearray(mirror.STOCK_SIZE)
    data[:6] = b"\x7fELF\x01\x01"
    for index, name in mirror.EXPECTED_NAMES.items():
        offset = mirror.DEVICE_NAMES_BASE + index * mirror.DEVICE_STRIDE + mirror.DEVICE_NAME_OFFSET
        data[offset : offset + len(name) + 1] = name + b"\0"
    struct.pack_into("<12I", data, mirror.MPL1_RECORD_OFFSET, *mirror.MPL1_REFERENCE)
    struct.pack_into("<12I", data, mirror.MPL6_RECORD_OFFSET, *mirror.MPL6_STOCK)
    return bytes(data)


def main() -> int:
    stock = fixture()
    result = mirror.patch_verified_stock(stock)
    mirror.validate_layout(result, expect_mirror=True)
    changed = {i for i, (before, after) in enumerate(zip(stock, result)) if before != after}
    assert changed == mirror.EXPECTED_CHANGED_OFFSETS

    record = struct.unpack_from("<12I", result, mirror.MPL6_RECORD_OFFSET)
    assert record == mirror.MPL1_REFERENCE
    assert 1 << record[1] == 0x1000  # SND_PCM_FMT_S32_LE
    assert record[11] == 24
    assert record[7] // (record[5] * 4) * 1000 // 16 == 48000
    assert record[9] == record[7] * 10

    for offset in (mirror.MPL6_RECORD_OFFSET, mirror.MPL1_RECORD_OFFSET):
        bad = bytearray(stock)
        bad[offset] ^= 1
        try:
            mirror.patch_verified_stock(bytes(bad))
        except mirror.PatchError:
            pass
        else:
            raise AssertionError(f"mismatched record at {offset:#x} was accepted")

    try:
        mirror.build_output(stock)
    except mirror.PatchError as exc:
        assert "unsupported" in str(exc)
    else:
        raise AssertionError("synthetic image bypassed the exact SHA-256 gate")

    print("PASS: deva 0.84.0 mpl6 mirror of mpl1 is exact and fail-closed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
