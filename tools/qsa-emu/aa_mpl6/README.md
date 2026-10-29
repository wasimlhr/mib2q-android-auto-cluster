# Android Auto on mpl6_gal_ent under QEMU

Runs MU0918's real `deva-ctrl-qc.so` (stock, or with mpl6's capability record
mirrored from mpl1) inside the SDP `io-audio` under QEMU, then drives
`mpl6_gal_ent` the way GAL does, with and without the Android Auto upmix
interposer (`android_auto/hook/src/audio_upmix.c`).

    tools/qsa-emu/aa_mpl6/run_audio.sh stock
    tools/qsa-emu/aa_mpl6/run_audio.sh mirror
    # serial log: build/qsa-emu/aa-mpl6-<variant>/qemu.log

Before the clients run, `route MS_ENT 0=6` switches the ENT route to Input=6,
the switch the audio manager flips when AA media plays (car slog, run 319). That
makes deva build the CSD session for mpl6 at route attach.

| piece | role |
|---|---|
| `gal_probe.c` | GAL-shaped client: open by name, size from `plugin_info`, S16/48k/2ch params, SAD SET, 8192-byte packets (left tone, then right tone) |
| `route.c` | lists mixer switches; writes raw bytes into one (`MS_ENT` byte 0 = input) |
| `ringstat.c` | per-channel RMS of the DMA ring `csd_stub` saves under `/tmp` |
| `extra_stubs.c` | MU0918 deva imports the SDP runtime lacks (`ado_pcm_subchn_state_*`, `ppsparse`, `rtpd_*`) |
| `probe_log.c` | the hook's logger, for the standalone interposer build |

Harness-only differences from the car: SDP `io-audio` and `libasound` (the
firmware's were not available), `csd_stub.c` instead of the DSP (its serial
base64 dump is disabled; the raw ring files are read instead), and the mmap
transport disabled as in `qsa_probe.c`.

## Measured (2026-10-29)

- Stock: mpl6 is 2..2 voices, 3072-byte fragments, S16; routing Input=6 builds a
  `channels=2, bit_per_sample=16` CSD session, as on the car (runs 316/317).
- Mirror: mpl6 is 6..6 voices, 18432-byte fragments, S32; routing Input=6 builds
  `channels=6, bit_per_sample=24`, mpl1's session.
- `snd_pcm_plugin_info` keeps fragment sizes in hardware bytes and widens the
  formats mask (0x100ffff) and rates (4000..192000).
- Interposer on the mirror: GAL is shown 2 voices / 3072 bytes, params expand to
  6ch / 9216, all 2048-frame packets are accepted whole, and the ring carries
  L on FL and R on FR, centre 0, surrounds from the upmix.
- Without the interposer, QSA's own 2 -> 6 conversion copies L to slots 0/2/4 and
  R to 1/3/5, which is why the fallback must keep a stereo SAD header.
- SAD SET payload as deva decodes it (one distinct byte per offset):
  media_type +0, asad +1, codec_type +3, sample_width +4, bitrate +16..19.
  GAL's own struct printer has the same order. With the interposer deva logs
  `asad=0xfc, sample_width=0x18, bitrate=0x697800`.
