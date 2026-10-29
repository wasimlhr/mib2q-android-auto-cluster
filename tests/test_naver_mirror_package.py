from __future__ import annotations

import importlib.util
import os
import subprocess
import tempfile
import zipfile
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
SOURCE = REPO / "tools" / "naver_mirror_test"
BUILDER = SOURCE / "build_package.py"
SH = Path(r"C:\Program Files\Git\bin\sh.exe")


def invoke_script(script: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    absolute = script.resolve()
    posix_script = f"/{absolute.drive[0].lower()}{absolute.as_posix()[2:]}"
    return subprocess.run([str(SH), posix_script], env=env, text=True, capture_output=True)


def run_script(script: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    result = invoke_script(script, env)
    assert result.returncode == 0, f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    return result


def load_builder():
    spec = importlib.util.spec_from_file_location("naver_builder", BUILDER)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader
    spec.loader.exec_module(module)
    return module


def test_posix_cksum_reference() -> None:
    assert load_builder().posix_cksum(b"123456789") == 930766865


def test_package_contents_and_manifest() -> None:
    module = load_builder()
    module.main()
    published = SOURCE / "MU1201_Naver_Main_Mirror_Test_v1.zip"
    assert published.read_bytes() == module.OUT.read_bytes()
    with zipfile.ZipFile(module.OUT) as archive:
        names = set(archive.namelist())
        assert {
            "mod/custom.sh",
            "mod/command.sh",
            "mod/naver-mirror-test/libsq5_main_mirror.so",
            "mod/naver-mirror-test/sq5_naver_mirror",
            "mod/naver-mirror-test/gal_startup_naver_mirror.sh",
            "mod/naver-mirror-test/sq5_naver_mirror_supervisor.sh",
            "mod/naver-mirror-test/manifest.cksum",
            "README_MU1201_NAVER_MIRROR_TEST.md",
        } <= names
        manifest = archive.read("mod/naver-mirror-test/manifest.cksum").decode("ascii").splitlines()
        assert len(manifest) == 4
        for line in manifest:
            crc, size, name = line.split()
            data = archive.read(f"mod/naver-mirror-test/{name}")
            assert int(size) == len(data)
            assert int(crc) == module.posix_cksum(data)


def test_install_and_remove_round_trip() -> None:
    if not SH.is_file():
        return
    module = load_builder()
    module.main()
    with tempfile.TemporaryDirectory(prefix="naver-mirror-e2e-") as raw:
        top = Path(raw)
        card = top / "card"
        unit = top / "unit"
        with zipfile.ZipFile(module.OUT) as archive:
            archive.extractall(card)
        base = unit / "mnt/app/root/sq5_android_auto"
        sd = unit / "fs/sda0"
        tmp = unit / "tmp"
        base.mkdir(parents=True)
        sd.mkdir(parents=True)
        tmp.mkdir(parents=True)
        original = b"#!/bin/sh\necho cluster-player libsq5_cluster_live.so\n"
        (base / "gal_startup.sh").write_bytes(original)
        (base / "profile.id").write_text("mhi2q_kr_aug22_p1201_mu1201\n", encoding="ascii")
        for name in (
            "libsq5_cluster_live.so",
            "cluster-player",
            "maneuver_render",
            "sq5_luka_monitor.sh",
            "sq5_luka_processes.sh",
        ):
            (base / name).write_bytes(b"test\n")

        script = card / "mod/custom.sh"
        env = os.environ.copy()
        env["NAVER_MIRROR_TEST_ROOT"] = str(unit).replace("\\", "/")

        audit = run_script(script, env)
        assert "AUDIT/COLLECT ONLY" in audit.stdout
        assert (base / "gal_startup.sh").read_bytes() == original

        (sd / "naver_mirror_test_install").touch()
        install = run_script(script, env)
        assert "INSTALLED" in install.stdout
        assert (base / "naver-mirror-test-backup/gal_startup.original.sh").read_bytes() == original
        assert (base / "sq5_naver_mirror").is_file()
        assert (base / "gal_startup.sh").read_bytes() != original

        (sd / "naver_mirror_test_install").unlink()
        (sd / "naver_mirror_test_remove").touch()
        test_startup = (base / "gal_startup.sh").read_bytes()
        (base / "gal_startup.sh").write_bytes(test_startup + b"# unexpected edit\n")
        refused = invoke_script(script, env)
        assert refused.returncode != 0
        assert "changed after test installation" in refused.stdout
        assert (base / "gal_startup.sh").read_bytes().endswith(b"# unexpected edit\n")
        (base / "gal_startup.sh").write_bytes(test_startup)
        remove = run_script(script, env)
        assert "REMOVED" in remove.stdout
        assert (base / "gal_startup.sh").read_bytes() == original
        assert not (base / "sq5_naver_mirror").exists()
        assert not (base / "naver-mirror-test-backup").exists()
