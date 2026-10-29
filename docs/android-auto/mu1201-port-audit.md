# MHI2Q_KR_AUG22_P1201 MU1201 port audit

Status: **offline port verified; first on-car pilot pending**. The profile is intentionally
non-installable until that pilot confirms the Korean unit's cockpit terminal and window behavior.

## Supplied backup

The collector reported all five files copied and byte-compared successfully. `copy_errors.txt` was
empty.

| File | Bytes | SHA-256 | POSIX cksum |
| --- | ---: | --- | ---: |
| `gal` | 1,405,916 | `e35627a08a01980f9b56000b8fbd4b4fae5d9b4c8f7dcd22c4818124fc5f1ef5` | 180845437 |
| `libautoreceiver.so` | 1,069,943 | `6ef4abea8264ab7787b0ecb3fe278a65488301f703148d0175010f7b228bdaf6` | 1653920605 |
| `lsd.jxe` | 61,141,973 | `5bc537679c457f2f670966e25270d88612a23c4b9d91c65cdf4faea5a9f2ae7d` | 1468223964 |
| `gal.json` | 21,442 | `b737bbf4f4ddceb23dd73ba5cdadfa5a792f070f858efe18ff0133412e9a95e7` | not an install gate |
| `smartphone_integrator.json` | 7,391 | `ee5def106ec86db5297ad47cf426b16afb2fa70867990e16f6617e994a2e9c70` | not an install gate |

Firmware files and converted JARs are private inputs and are not committed.

## Native result

`libautoreceiver.so` is byte-for-byte identical to the validated MU0918 receiver. Consequently the
interposed receiver symbols, function offsets, C++ endpoint layout and callback ABI used by the hook
are identical. The receiver-side guard still checks all of them at runtime.

MU1201's `gal` is different, so the old GAL fingerprint would safely disable the hook. Its own dynamic
symbol table resolves:

- `gal::CGALController::s_instance` at `0x259598`;
- `gal::CVideoSinkImpl` vtable at `0x252b40`.

Those exact addresses are now accepted as the MU1201 GAL fingerprint. Unknown tuples remain disabled.
The QNX ARM hook rebuilt successfully after the change.

## Java result

The converted MU1201 stock JAR contains every required class. All 14 stock outer classes intentionally
replaced by the Android Auto overlay are byte-identical to MU1316 P5145, including the three generated
Android Auto seams. The profile therefore declares `java_source_profile` as the reviewed MU1316 source
profile instead of duplicating four files.

The build still compares the output with MU1201's own stock JAR. Results:

- 146 output classes and 18 stock-class replacements passed the leak/bytecode gate;
- every stock class header and public/protected member was preserved;
- roller binding gate passed;
- `ScreenModuleMirrorTest`: 23 checks;
- `VcMapViewGateTest`: 20 checks;
- `TurnCardTest`: 70 checks;
- `AaClusterMenuTest`: 49 checks.

The repository can deterministically restore the missing `InnerClasses` metadata with
`scripts/restore_innerclasses.py`, so no MU1316 JAR is required to build this profile.

## Remaining gate

Offline evidence cannot prove the external Virtual Cockpit attached to this particular Korean car uses
the same terminal, window 99, contexts 81/82 and view signals. The first card run must therefore be a
read-only profile audit, followed by one explicitly prepared pilot. Promote the profile to
`installable: true` only after logs show the MU1201 native guard enabled and the owner confirms normal
centre-screen Android Auto plus correct cockpit switching.

The supplied backup did not contain `deva-ctrl-qc.so`. This audit therefore covers the Android Auto
cluster, HUD and Java integration, not the optional external-amplifier 5.1/upmix driver experiment.
Keep that optional audio path disabled during the first MU1201 pilot.
