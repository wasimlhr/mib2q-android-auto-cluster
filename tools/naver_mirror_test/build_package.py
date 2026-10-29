#!/usr/bin/env python3
"""Build the shareable MU1201 Naver mirror test ZIP."""

from __future__ import annotations

import shutil
import stat
import tempfile
import zipfile
from pathlib import Path


HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
WORKSPACE = REPO.parent
OUT = REPO / "dist" / "MU1201_Naver_Main_Mirror_Test_v1.zip"


def posix_cksum(data: bytes) -> int:
    crc = 0
    for value in data:
        crc ^= value << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if crc & 0x80000000 else (crc << 1) & 0xFFFFFFFF
    length = len(data)
    while length:
        value = length & 0xFF
        length >>= 8
        crc ^= value << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if crc & 0x80000000 else (crc << 1) & 0xFFFFFFFF
    return (~crc) & 0xFFFFFFFF


def copy_executable(src: Path, dst: Path) -> None:
    if not src.is_file():
        raise FileNotFoundError(src)
    shutil.copy2(src, dst)
    dst.chmod(dst.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def main() -> None:
    sources = {
        "libsq5_main_mirror.so": WORKSPACE / "galhook" / "out" / "libsq5_galhook_v3.so",
        "sq5_naver_mirror": WORKSPACE / "galhook" / "out" / "sq5_mirror",
        "gal_startup_naver_mirror.sh": HERE / "payload" / "gal_startup_naver_mirror.sh",
        "sq5_naver_mirror_supervisor.sh": HERE / "payload" / "sq5_naver_mirror_supervisor.sh",
    }
    with tempfile.TemporaryDirectory(prefix="naver-mirror-package-") as raw:
        root = Path(raw)
        mod = root / "mod"
        payload = mod / "naver-mirror-test"
        payload.mkdir(parents=True)
        copy_executable(HERE / "mod" / "custom.sh", mod / "custom.sh")
        copy_executable(HERE / "mod" / "command.sh", mod / "command.sh")
        shutil.copy2(HERE / "README.md", root / "README_MU1201_NAVER_MIRROR_TEST.md")
        manifest = []
        for name, src in sources.items():
            copy_executable(src, payload / name)
            data = (payload / name).read_bytes()
            manifest.append(f"{posix_cksum(data)} {len(data)} {name}\n")
        (payload / "manifest.cksum").write_text("".join(manifest), encoding="ascii", newline="\n")

        OUT.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(OUT, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for path in sorted(root.rglob("*")):
                if path.is_file():
                    info = zipfile.ZipInfo(path.relative_to(root).as_posix())
                    info.date_time = (2026, 10, 29, 0, 0, 0)
                    mode = 0o755 if path.suffix in {".sh", ".so"} or path.name in {"custom.sh", "command.sh", "sq5_naver_mirror"} else 0o644
                    info.external_attr = (stat.S_IFREG | mode) << 16
                    info.compress_type = zipfile.ZIP_DEFLATED
                    archive.writestr(info, path.read_bytes(), compresslevel=9)
    print(OUT)


if __name__ == "__main__":
    main()
