# MIB2Q Android Auto Cluster

Native Android Auto map video, route guidance and HUD integration for Audi MHI2Q. This project was
developed and validated on a 2018 B9 SQ5 with `MHI2Q_US_AUG22_P3639`, MU0918 and the first-generation
Virtual Cockpit.

The phone creates a real second Android Auto display. It sends an independent H.264 cluster stream on
channel 64 while the centre display remains available for music, calls, messages or Android Auto's
guidance list. This is not a copy of the centre-screen framebuffer.

## Current result

- Separate 1920x1080 Android Auto cluster stream, negotiated with GAL protocol 4.3.
- Google Maps and Waze rendered natively for the cockpit.
- Qualcomm `OMX.qcom.video.decoder.avc` hardware decoding with automatic FFmpeg fallback.
- 1440x540 phone viewport presented 1:1 in the Virtual Cockpit.
- Live Large, Classic and Sport layouts through Android Auto message `0x8009`.
- Steering-wheel map zoom, cockpit options menu, map theme and UI-size controls.
- Turn arrows, distance and lane information in the Virtual Cockpit and HUD.
- Android Auto cover art, touchpad navigation and parking-popup integration.
- Feature off switches, conservative installation, log collection and exact uninstall rollback.

The most recent on-car validation sustained approximately 29–30 decoded frames per second and
24–27 displayed frames per second in the normal 1:1 view.

## Supported firmware

| Firmware | Status |
| --- | --- |
| `MHI2Q_US_AUG22_P3639` MU0918 | Validated on the car |
| Other MHI2Q versions | Port required until their GAL ABI and HMI classes are verified |

The native hook contains firmware-specific C++ ABI seams. Similar hardware is not enough to make an
unknown binary safe. Run the checker against files obtained from your own unit:

```bash
python scripts/check_firmware.py \
  --gal /path/to/gal \
  --receiver /path/to/libautoreceiver.so \
  --lsd-jxe /path/to/lsd.jxe \
  --stock-jar /path/to/lsd.jar \
  --compile-jar /path/to/lsd_ic.jar
```

The checker is read-only. It reports `validated-profile`, `port-required`, `unsupported` or
`incomplete`, and lists every change the installer would make. Firmware binaries and converted stock
JARs are never stored in this repository.

## Repository layout

| Path | Purpose |
| --- | --- |
| `android_auto/hook/` | GAL/libautoreceiver hook, channel transport, OMX/FFmpeg player and host tests |
| `android_auto/java/` | Android Auto event bridge, cockpit/HUD integration, menu and firmware Java seams |
| `android_auto/deploy/` | Standalone GAL launcher and renderer supervision |
| `firmware/profiles/` | Public hashes and sizes for validated firmware; no firmware content |
| `install_AndroidAuto_MoreIncredibleBash/` | M.I.B. audit/install/uninstall entrypoint |
| `scripts/` | Compatibility checker, reproducible builds and SD-card staging |
| `common/`, `maneuver_render/`, `toolchain/` | Shared LuKa renderer foundation retained from upstream |
| `docs/android-auto/` | Architecture, installation, porting and validation notes |

The inherited CarPlay implementation remains in the working tree as upstream reference so fixes can
still be contributed cleanly to LuKa. Android Auto development and packaging are isolated under the
paths above on `android-auto-mu0918`; the Android Auto installer never deploys the CarPlay hook.

## Build

The build uses the QNX 6.5 ARMv7 Docker toolchain from
[`luka-dev/qnx65-armv7-toolchain`](https://github.com/luka-dev/qnx65-armv7-toolchain). Build that image
once, then run:

```bash
./scripts/build_android_auto_native.sh
./scripts/build_renderers.sh
./scripts/build_android_auto_java.sh \
  --stock-jar /private/path/lsd.jar \
  --compile-jar /private/path/lsd_ic.jar
python scripts/stage_android_auto.py
```

`lsd.jar` and `lsd_ic.jar` must be created from firmware owned by the builder. The Java build checks
every replaced class against that input and fails if a public/protected member or class header differs.
Compile-only stubs are checked so they cannot leak into the shipped overlay.

The generated SD folder is `dist/android-auto-card`. Build outputs and private firmware inputs are
ignored by Git.

## Installation

1. Make a complete M.I.B. backup of the unit.
2. Copy `install_AndroidAuto_MoreIncredibleBash` from the staged output to the M.I.B. SD card.
3. Run the custom script with no marker. It performs a **read-only audit** and prints the exact plan.
4. If the result says `VALIDATED MU0918 PROFILE`, create an empty `mib2q_aa_install` file in the SD root
   and run the script again.
5. Remove the marker, disconnect the phone and reboot the MMI.

To uninstall, use `mib2q_aa_uninstall` instead. The installer restores the configuration it backed up
and removes only files it added. See [installation details](docs/android-auto/installation.md) and
[on-unit changes](docs/android-auto/on-unit-changes.md).

## Upstream and attribution

This repository is a fork of [`luka-dev/mib2q-carplay-rgi`](https://github.com/luka-dev/mib2q-carplay-rgi).
LuKa's project supplied the MHI2Q cockpit integration, renderer, build foundation and selected assets.
The Android Auto cluster protocol work, GAL integration, channel 64 video pipeline, Qualcomm decoder,
MU0918 port, dynamic layouts and Android Auto controls were developed for this project.

The Android Auto hook also adapts GPL-3.0 work from OneB1t/chopinwong01's
`mhi2-android-auto-video-vc`. FFmpeg is used under LGPL-2.1-or-later. See
[`android_auto/THIRD_PARTY.md`](android_auto/THIRD_PARTY.md) and [`LICENSES/README.md`](LICENSES/README.md).

## Safety boundary

The installer does not replace `gal`, `libautoreceiver.so`, `lsd.jxe` or `gal.json`. It adds an overlay
JAR and runtime files, then changes only `children.gal.exec` and `children.gal.path` in
`smartphone_integrator.json`. Unknown firmware, an existing third-party GAL launcher, a damaged
payload or an externally edited installed configuration causes a refusal before activation.
