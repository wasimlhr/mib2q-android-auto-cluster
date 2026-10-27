#!/usr/bin/env python3
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "probe_AndroidAutoAudio_MoreIncredibleBash/mod/custom.sh"
STOCK = ROOT.parent / "car_data/drive/34_19700101_000305/gal.json"
BASH = next(
    (path for path in (
        Path(r"C:\Program Files\Git\bin\bash.exe"),
        Path(r"C:\Program Files\Git\usr\bin\bash.exe"),
        Path("bash"),
    ) if path.is_file() or str(path) == "bash"),
)
TEST_SHELL = os.environ.get("SQ5_TEST_SHELL", str(BASH))


def run(root: Path, *markers: str) -> subprocess.CompletedProcess[str]:
    sd = root / "fs/sda0"
    sd.mkdir(parents=True, exist_ok=True)
    for name in ("sq5_aa_audio_24_on", "sq5_aa_audio_24_once", "sq5_aa_audio_restore"):
        (sd / name).unlink(missing_ok=True)
    for name in markers:
        (sd / name).touch()
    env = os.environ.copy()
    env["MIB2Q_AA_TEST_ROOT"] = str(root).replace("\\", "/")
    return subprocess.run(
        [TEST_SHELL, str(SCRIPT)], env=env, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
    )


def main() -> int:
    if not STOCK.is_file():
        raise SystemExit(f"missing stock fixture: {STOCK}")
    with tempfile.TemporaryDirectory() as raw:
        root = Path(raw)
        config = root / "mnt/system/etc/eso/production/gal.json"
        config.parent.mkdir(parents=True)
        shutil.copy2(STOCK, config)
        original = config.read_bytes()

        result = run(root)
        assert result.returncode == 0 and "AUDIT ONLY" in result.stdout, result.stdout
        assert config.read_bytes() == original

        result = run(root, "sq5_aa_audio_24_on")
        assert result.returncode == 0 and "ENABLED negotiation probe" in result.stdout, result.stdout
        candidate = config.read_bytes()
        assert candidate != original
        assert candidate.count(b'"bits":24,') == 1
        assert candidate.count(b'"bits":16,') + 1 == original.count(b'"bits":16,')
        backup = Path(str(config) + ".sq5audio24bak")
        assert backup.read_bytes() == original

        result = run(root, "sq5_aa_audio_24_on")
        assert result.returncode == 0 and "Already requesting" in result.stdout, result.stdout
        assert config.read_bytes() == candidate and backup.read_bytes() == original

        result = run(root, "sq5_aa_audio_restore")
        assert result.returncode == 0 and "RESTORED exact" in result.stdout, result.stdout
        assert config.read_bytes() == original and not backup.exists()

        result = run(root, "sq5_aa_audio_24_once")
        assert result.returncode == 0 and "ONE-SHOT ARMED" in result.stdout, result.stdout
        assert config.read_bytes() != original and backup.read_bytes() == original
        result = run(root, "sq5_aa_audio_24_once")
        assert result.returncode == 0 and "ONE-SHOT COMPLETE" in result.stdout, result.stdout
        assert config.read_bytes() == original and not backup.exists()

        result = run(root, "sq5_aa_audio_24_on", "sq5_aa_audio_restore")
        assert result.returncode != 0 and "multiple" in result.stdout, result.stdout
        assert config.read_bytes() == original

        damaged = original.replace(b'"channels":2,', b'"channels":6,', 1)
        config.write_bytes(damaged)
        result = run(root, "sq5_aa_audio_24_on")
        assert result.returncode != 0 and "refusing to edit" in result.stdout, result.stdout
        assert config.read_bytes() == damaged

    print("Android Auto audio negotiation probe tests PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
