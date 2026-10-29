# Android Auto audio-quality roadmap

This project treats audio fidelity and surround presentation as separate goals. A convincing six-speaker
upmix is useful, but it does not restore resolution discarded before the head unit receives the stream.

## Confirmed boundaries

- The stock MU0918 `gal.json` requests Android Auto media as uncompressed PCM at 48 kHz, 16-bit,
  two-channel. Guidance is a separate 16 kHz mono stream.
- Current Android Auto phone builds validate media audio configurations to 48 kHz or 16 kHz, 16-bit,
  and one or two channels. Advertising 24-bit, 96 kHz or six channels is therefore not a safe
  configuration-only change: the phone can reject the AV setup before playback begins.
- The audited MHI2Q audio path has a 48 kHz transport ceiling, a 24-bit effective depth and a six-channel
  output path. Material above 48 kHz must ultimately be sample-rate converted on this hardware.

The existing Android Auto stream is not low-bitrate compressed audio: 48,000 x 16 x 2 is 1.536 Mbit/s
of linear PCM. The fidelity loss for high-resolution sources comes from conversion to 16-bit/48 kHz,
not from an MP3-style encoder. A 24-bit or 96/192 kHz source is not delivered bit-perfectly.

## Track A: compatible surround path

**Implementation status (2026-10-27): host-validated; stock car probe complete; activation pending.** The GAL preload now contains
an opt-in `mpl6_gal_ent` PCM interposer. It keeps Android Auto's standard 48 kHz/S16/stereo negotiation,
reuses the SCENE stereo-to-5.1 renderer, and exposes probe/on controls through SD-card markers. The
interposer is fail-closed: the collected stock MU0918 endpoint advertises exactly two voices and a
3072-byte period, so it logs the limitation and leaves the caller's stereo request unchanged. The exact
MU0918 driver is now fingerprinted and its MPL6 record has a deterministic, six-byte, hash-gated patch
to six voices and a 9216-byte S16 period. That output is not installed yet; QSA-harness validation and
a reversible on-car activation run remain. See
[Android Auto upmix validation](audio-upmix-validation.md).

Keep the standard Android Auto PCM negotiation so every supported phone continues to connect, then:

1. Interpose the GAL media renderer at the 48 kHz/16-bit/stereo PCM boundary.
2. Convert to the existing float DSP domain without another lossy encode.
3. Reuse the audited stereo-to-5.1 engine and six-channel QSA/CSD route.
4. Use the receiver's S16 QSA boundary without an additional encode; any later 24-bit output-container
   path must be measured separately and must not be described as restoring discarded source bits.
5. Use the shared `Phone UpMix 5.1` MMI control for both CarPlay and Android Auto, with Off and four
   bounded intensity levels. Android Auto applies changes live from the existing persisted level file;
   audible activation remains gated on the firmware-exact MPL6 driver profile.

This improves spatial presentation and uses the B&O system's discrete channels, but it must be labelled
**car-side upmix**, not native Dolby Atmos or restored high-resolution audio.

## Track B: genuine 24-bit source path

### Projection negotiation gate

Before building the companion transport, the repository carries a separate, reversible
`probe_AndroidAutoAudio_MoreIncredibleBash` experiment. It changes only the Android Auto media sink
advertisement from 48 kHz/16-bit/stereo to 48 kHz/24-bit/stereo, captures the resulting GAL/QSA/CSD
evidence on the next M.I.B. rerun, and restores the exact pre-probe `gal.json`. The probe is not part
of the cockpit installer and cannot by itself prove end-to-end 24-bit playback.

This is the only useful higher-fidelity stock-projection candidate on the audited hardware. A 96 kHz
request would still be converted at the MHI2Q 48 kHz transport boundary, and six phone channels test
surround negotiation rather than source precision. Generic Android `AudioFormat` support for packed
24-bit PCM proves the phone platform can represent the samples; it does not prove that Android Auto's
projection endpoint accepts them.

For local high-resolution files, add an optional companion player and receiver side-channel beside the
Android Auto session:

1. The Android player reads the source itself and sends FLAC or framed PCM to a head-unit daemon.
2. The head unit decodes to 24-bit PCM, performs one high-quality conversion to 48 kHz when required,
   and writes through the validated media-focus and six-channel output path.
3. Android Auto remains connected for maps, controls, metadata and navigation. The receiver owns
   ducking/mixing so guidance and safety audio remain audible.
4. Preserve stereo as stereo or apply the same optional upmixer after the final 48 kHz conversion.

This is the practical route to 24-bit playback on MHI2Q. It cannot transparently capture protected
audio from arbitrary streaming applications; the companion must own playback or use an authorized
provider API. A rooted-phone Android Auto patch is a separate research experiment, not a public
installation path.

## Acceptance evidence

Do not advertise a fidelity mode until tests prove all of the following:

- negotiated Android Auto format and codec are captured from the live session;
- no unintended AAC or other lossy transcode occurs;
- source, receiver and QSA sample formats are logged at every boundary;
- channel identity reaches the correct B&O speakers with no fold-down;
- navigation ducking, calls and safety announcements still work;
- underruns remain zero during simultaneous cockpit video and audio playback;
- spectral, noise-floor and level measurements are saved alongside an on-car listening result.

