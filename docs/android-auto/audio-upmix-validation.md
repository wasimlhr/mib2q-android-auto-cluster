# Android Auto surround validation

## Status and safety boundary

The Android Auto hook contains an experimental, opt-in car-side stereo-to-5.1 path. It is **off by
default** and does not change Android Auto negotiation: the phone still sends 48 kHz, 16-bit, stereo
linear PCM. Guidance, calls and alerts use other QSA devices and are not intercepted.

The interposer opens only `mpl6_gal_ent` and activates only when every checked boundary agrees:

- GAL asks for playback, block mode, S16 little-endian, 48 kHz, two channels;
- the fragment is aligned and contains no more than 1024 frames;
- QSA reports at least six voices and enough space for the corresponding six-channel fragment;
- QSA accepts the expanded request and reads it back as exactly six channels, S16, 48 kHz;
- the read-back fragment is exactly three times the caller's stereo fragment.

Any failed check passes GAL's original parameters through. No driver file is replaced by this build.
The stock MU0918 MPL6 capability record is two channels with a 3072-byte client period. The returned
read-only collector run has now fingerprinted that exact driver and produced a strict, separate patch
which exposes a 9216-byte six-channel period. Nothing is installed merely by running the patcher.

## SD-card controls

Place marker files in the SD-card root, then reboot or restart the Android Auto session:

| Marker | Effect |
| --- | --- |
| no marker | Upmix code is dormant; current cockpit/video behavior is unchanged |
| `sq5_aa_upmix_probe` | Logs MPL6 QSA capabilities; cannot alter audio parameters or samples |
| `sq5_aa_upmix` | Requests upmix, but only after all capability and read-back checks pass |
| `sq5_aa_upmix_level` | Optional one-line value `0`–`4`; default `2` (normal) |

The MMI Sound effects row **Phone UpMix 5.1** writes the same persisted level used by the
CarPlay renderer. Android Auto polls that shared file during playback, so level changes apply
without reconnecting. The older `/tmp/carplay_upmix_level` filename is retained for compatibility.

If both mode markers exist, probe wins. Existing `mib2q_aa_off` and `sq5_hook_off` still launch stock
GAL with all upmix variables cleared.

## Collected MU0918 probe evidence

The read-only SD collector completed all six files with byte comparison. On the tested
`MHI2Q_US_AUG22_P3639 MU0918` unit, `mpl6_gal_ent` reported:

```text
voices_min=2
voices_max=2
rate_min=48000
rate_max=48000
fragment_min=3072
fragment_max=3072
formats=0x10
```

The exact stock driver is 690,332 bytes with SHA-256
`5cc4c61e2e3a771dd21ff000ae1cd12eebebfa006bd7879df93f30b3b1fc0c63`.

`android_auto/deploy/patch_deva_mpl6_mu0918.py` accepts only that input (or its already-patched
output), validates the ELF/device/capability tables and changes only six bytes. The deterministic
output SHA-256 is `e92d2efff1dc2dd83df1879a7a2bea01569b45741ae88ba691cdf7d9b71f506f`.
It preserves the 48 kHz S16 format and changes only the GAL endpoint geometry:

| Field | Stock | Six-channel output |
| --- | ---: | ---: |
| voices min/max | 2 / 2 | 6 / 6 |
| fragment min/max | `0x0c00` / `0x0c00` | `0x2400` / `0x2400` |
| maximum buffer | `0x07800` | `0x16800` |

The 9216-byte period is exactly 768 frames x 6 channels x 2 bytes. This is not the MU1316 CarPlay
MPL5 record: that route uses an S32 container and a `0x4800` period.

## Completed first on-car run: probe only

1. Put only `sq5_aa_upmix_probe` on the SD-card root.
2. Run the M.I.B. custom script to install the staged build, then reboot.
3. Start Android Auto and play music for at least two minutes.
4. During playback, trigger one navigation instruction and, while parked, place a short test call.
5. Run the same M.I.B. script again to collect logs. No terminal typing is required.

Expected and collected stock evidence:

```text
aa_upmix.mode value=probe device=mpl6_gal_ent
aa_upmix.open ...
aa_upmix.caps result=0 voices=2..2 rate=48000..48000 frag=3072..3072 ...
aa_upmix.close ...
```

Acceptance for the probe run:

- normal stereo music, navigation prompts and calls;
- no GAL restart, no audio dropout and no new `io-audio` fault;
- the reported device/rate/format/voice/fragment values are saved with the run.

## Activation run after the driver gate

Do not create `sq5_aa_upmix` while the stock driver is installed. Generate the firmware-exact output
with the strict patcher, validate it in the QSA harness, then install it only through a backup-first,
hash-gated and reversible package. The existing CarPlay MPL5 patch cannot be reused blindly: its
six-channel internal fragment is S32 `0x4800`; Android Auto MPL6 is an S16 client and needs a
9216-byte (`0x2400`) period for 768 frames.

Once QSA safely reports `max_voices >= 6` and `max_fragment_size >= 9216`, use `sq5_aa_upmix`. Expected
activation evidence is:

```text
aa_upmix.params active source=2ch/48000Hz/3072 target=6ch/48000Hz/9216 frags=4..6
aa_upmix.dsp ready=1 level=2 mask=1 init=0
aa_upmix.write n=1 frames=768 result=9216/9216 dsp_fail=0 short=0
```

The final acceptance run must prove discrete channel identity at the B&O outputs, correct pitch and
tempo, zero DSP failures/short writes, stable guidance ducking, working calls and no underruns while
the 1080p cockpit stream is active. Until that car run passes, this remains an experimental feature,
not a release claim.

## Host evidence

- `android_auto_upmix_policy_test`: stock limits refuse activation; 6ch/9216 geometry activates; byte
  translation and malformed/unsupported formats are rejected.
- `android_auto_upmix_interposer_test`: executes the real interposer against fake QSA boundaries,
  proves stock pass-through, 2ch-to-6ch parameter expansion, stereo setup translation and 3x write
  expansion.
- The production hook is cross-compiled with the QNX 6.5 ARM toolchain and inspected for all five QSA
  interposer exports.
- `deva_mpl6_patch_test.py`: verifies the MU0918 table structure, exact six-byte footprint, fail-closed
  behavior and refusal to bypass the production SHA-256 gate.
- The exact collected MU0918 driver produces the documented deterministic patched hash; the proprietary
  input and output binaries are not committed to the repository.
