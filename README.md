# MIB2Q Android Auto Cluster

> [!IMPORTANT]
> This is an **Android Auto** project built from two earlier open-source foundations:
>
> - **[LuKa's stack](https://github.com/luka-dev/mib2q-carplay-rgi)** is the foundation for the HUD,
>   turn arrows, lane guidance, maneuver renderer and Virtual Cockpit window/context handling.
> - **[OneB1t/chopinwong01's approach](https://github.com/chopinwong01/mhi2-android-auto-video-vc)**
>   came first for Android Auto cluster video: hook the stock receiver, advertise a second cockpit
>   display and accept the phone's independent H.264 stream.
>
> This fork combines and ports those foundations to Audi MHI2Q MU0918, then adds the Qualcomm decoder,
> 1080p 1:1 viewport, live cockpit layouts, Android Auto event bridge, steering-wheel controls and a
> firmware-gated M.I.B. installer. Both upstream projects retain full credit and history.

This project provides native Android Auto map video, route guidance and HUD integration for Audi
MHI2Q. It was developed and validated on a 2018 B9 SQ5 with `MHI2Q_US_AUG22_P3639`, MU0918 and the
first-generation Virtual Cockpit.

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

## Gallery

**Android Auto's independent map stream on the SQ5**

<p align="center">
  <img src="assets/gallery/android_auto_large.png" height="300" alt="Android Auto Google Maps cluster stream in the Large cockpit view" />
  <img src="assets/gallery/android_auto_sport.png" height="300" alt="Android Auto Google Maps cluster stream in the Sport cockpit view" />
</p>

More verified Android Auto screenshots and GIFs—including HUD, lane guidance, album art, cockpit
layouts and parking behavior—will be added from this car. The inherited CarPlay gallery remains in
the repository as upstream history but is intentionally not presented here as Android Auto evidence.

## Comparison

The figures for this project are measured on the SQ5 or verified in its source and logs. RoadKernel
details are limited to the package and demonstrations available during this research; values that were
not published are identified as such. LuKa's column describes the upstream CarPlay project rather than
the Android Auto additions in this fork.

| Capability | This project | RoadKernel | [LuKa (CarPlay)](https://github.com/luka-dev/mib2q-carplay-rgi) |
| --- | --- | --- | --- |
| **Phone system** | Android Auto | Android Auto | CarPlay |
| **Live map in the cockpit** | Yes. Android Auto's own cockpit stream, independent of the centre screen | Yes, independent of the centre screen | No projected map; turn guidance is drawn over Audi's native map |
| **Stream** | 1080p at 30 fps, with a 1440×540 viewport shown 1:1 | 720p at 30 fps, scaled | n/a |
| **Video decoding** | Qualcomm hardware decoder with automatic FFmpeg fallback | Hardware decoder | n/a |
| **Frames shown** | Approximately 24–27 of the 30 frames sent each second | Not published | n/a |
| **Sharpness and UI size** | 95/110/125 DPI at 1080p for Small/Medium/Large; 125 DPI matches the cockpit's physical ~125 PPI | Not published | n/a |
| **Cockpit views** | Large, Classic and Sport, each with its own layout and live switching | Large, Classic and Sport | Guidance overlay follows the supported Audi layouts |
| **Google card beside Audi's arrow tile** | Yes | Yes | n/a |
| **HUD** | Turn arrows, distance and lanes | No | Turn arrows and distance from CarPlay |
| **Cockpit arrow tile** | Turn arrow, street and 16-step distance bars; selectable always-on or Audi-style behavior | Stock Audi tile | Stock Audi tile |
| **Lane guidance** | Cockpit and HUD | Not published | Yes, from CarPlay |
| **Steering-wheel menu** | Size, map position, arrow tile, resolution and Day/Night/Auto theme; opens with a two-second hold | No | No |
| **Day/night map** | Follows the headlights or can be fixed to Day or Night | Not published | n/a |
| **Map roller** | Adjusts car position in Large and card layout in Sport | No zoom | n/a |
| **MMI touchpad** | Swipes navigate Android Auto menus while handwriting remains available | Not published | Not published |
| **Album art** | Yes. Android Auto cover art appears in Audi's media screens | Not available | Yes for CarPlay; this implementation builds on LuKa's work |
| **Navigation apps tested** | Google Maps and Waze | Google Maps | Apple Maps |
| **Firmware represented here** | MU0918, tested on the car | MU1316 package examined | MU1316 releases; firmware-specific rebuilds may be required |
| **Installation** | SD card through M.I.B.; automatic logs, audit, off switch and exact uninstall rollback | SD card and rollback package; package observed tied to the vehicle VIN | SD card through M.I.B. |
| **Price and licence** | Free, GPL-3.0-or-later | Commercial, €100 at the time examined | Free; GPL-3.0 announced upstream and publication permission granted |

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

## License

This project is published under GPL-3.0-or-later, with the third-party exceptions and notices described
in [`LICENSES/README.md`](LICENSES/README.md). LuKa gave written permission on 2026-09-30 to publish and
distribute these Android Auto modifications as a fork of his project; the repository records the scope
without publishing the private correspondence or email addresses.

## Safety boundary

The installer does not replace `gal`, `libautoreceiver.so`, `lsd.jxe` or `gal.json`. It adds an overlay
JAR and runtime files, then changes only `children.gal.exec` and `children.gal.path` in
`smartphone_integrator.json`. Unknown firmware, an existing third-party GAL launcher, a damaged
payload or an externally edited installed configuration causes a refusal before activation.
