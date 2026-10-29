#!/usr/bin/env python3
"""Host tests for the strict MU0918 mpl6_gal_ent patch."""

from __future__ import annotations

import importlib.util
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATCHER = ROOT / "android_auto/deploy/patch_deva_mpl6_mu0918.py"
SPEC = importlib.util.spec_from_file_location("patch_deva_mpl6_mu0918", PATCHER)
assert SPEC and SPEC.loader
patcher = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(patcher)


def fixture() -> bytes:
    data = bytearray(patcher.STOCK_SIZE)
    data[:6] = b"\x7fELF\x01\x01"
    for index, name in {
        patcher.MPL1_INDEX: b"mpl1_int_ent",
        patcher.MPL5_INDEX: b"mpl5_dio_ent",
        patcher.MPL6_INDEX: b"mpl6_gal_ent",
    }.items():
        offset = patcher.DEVICE_NAMES_BASE + index * patcher.DEVICE_STRIDE + patcher.DEVICE_NAME_OFFSET
        data[offset : offset + len(name) + 1] = name + b"\0"
    struct.pack_into("<12I", data, patcher.MPL1_RECORD_OFFSET, *patcher.MPL1_REFERENCE)
    struct.pack_into("<12I", data, patcher.MPL6_RECORD_OFFSET, *patcher.MPL6_STOCK)
    return bytes(data)


def main() -> int:
    stock = fixture()
    result = patcher.patch_verified_stock(stock)
    patcher.validate_layout(result, expect_patched=True)
    changed = {i for i, (before, after) in enumerate(zip(stock, result)) if before != after}
    assert changed == patcher.EXPECTED_CHANGED_OFFSETS

    # Luka's step-1 DMA helper is required only while the capability minima
    # remain stereo. This profile fixes the attach-time and DMA geometry at
    # the source: 0x2400 bytes / (6 voices * 2 bytes) = 768 frames per 16 ms,
    # exactly 48 kHz, with a ten-period ring. A 0x0c00 fragment here would be
    # 256 frames per tick and reproduce the one-third-speed failure.
    record = struct.unpack_from("<12I", result, patcher.MPL6_RECORD_OFFSET)
    voices, fragment, buffer_bytes = record[5], record[7], record[9]
    frames_per_tick = fragment // (voices * 2)
    assert voices == 6
    assert fragment == 0x2400
    assert frames_per_tick * 1000 // 16 == 48000
    assert buffer_bytes == fragment * 10

    bad = bytearray(stock)
    bad[patcher.MPL6_RECORD_OFFSET] ^= 1
    try:
        patcher.patch_verified_stock(bytes(bad))
    except patcher.PatchError:
        pass
    else:
        raise AssertionError("mismatched capability record was accepted")

    try:
        patcher.build_output(stock)
    except patcher.PatchError as exc:
        assert "unsupported" in str(exc)
    else:
        raise AssertionError("synthetic image bypassed the exact SHA-256 gate")

    print("PASS: MU0918 GAL six-channel patch is exact and fail-closed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
