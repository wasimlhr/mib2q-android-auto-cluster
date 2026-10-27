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
The stock MU0918 MPL6 capability record is two channels with a 3072-byte client period, so active mode
will deliberately remain stereo until an exact, separately verified driver profile exposes a 9216-byte
six-channel period.

## SD-card controls

Place marker files in the SD-card root, then reboot or restart the Android Auto session:

| Marker | Effect |
| --- | --- |
| no marker | Upmix code is dormant; current cockpit/video behavior is unchanged |
| `sq5_aa_upmix_probe` | Logs MPL6 QSA capabilities; cannot alter audio parameters or samples |
| `sq5_aa_upmix` | Requests upmix, but only after all capability and read-back checks pass |
| `sq5_aa_upmix_level` | Optional one-line value `0`–`4`; default `2` (normal) |

If both mode markers exist, probe wins. Existing `mib2q_aa_off` and `sq5_hook_off` still launch stock
GAL with all upmix variables cleared.

## First on-car run: probe only

1. Put only `sq5_aa_upmix_probe` on the SD-card root.
2. Run the M.I.B. custom script to install the staged build, then reboot.
3. Start Android Auto and play music for at least two minutes.
4. During playback, trigger one navigation instruction and, while parked, place a short test call.
5. Run the same M.I.B. script again to collect logs. No terminal typing is required.

Expected stock log evidence:

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

Do not create `sq5_aa_upmix` merely to force the current stock driver. First port and verify the MPL6
capability/timing change against the unit's exact `deva-ctrl-qc.so` hash. The existing CarPlay MPL5
patch cannot be reused blindly: its six-channel internal fragment is S32 `0x4800`; Android Auto MPL6
is an S16 client and needs a 9216-byte (`0x2400`) period for 768 frames.

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
