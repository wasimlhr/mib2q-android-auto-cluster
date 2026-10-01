# Licensing map

There is no single root license asserted over every inherited file in this fork.

- New Android Auto hook, transport, player, installer and tooling under `android_auto/` and the related
  new scripts are offered under GPL-3.0-or-later unless a file says otherwise.
- The protocol hook adapts GPL-3.0 work from OneB1t/chopinwong01's
  `mhi2-android-auto-video-vc`; the combined derivative remains GPL compatible.
- FFmpeg is LGPL-2.1-or-later in the configuration documented by the build scripts. Its exact source is
  downloaded and hash-verified during the build. Distributors of a statically linked player must also
  satisfy LGPL relinking requirements.
- OpenMAX IL headers retain the Khronos notices contained in those files.
- DejaVu-derived font data retains `android_auto/hook/vendor/LICENSE-DejaVu.txt`.
- Files inherited from `luka-dev/mib2q-carplay-rgi`, including the shared renderer and portions of the
  Java/HMI integration, retain LuKa's copyright and upstream history. At the time of this import the
  upstream repository did not contain a license file; this repository does not relicense that work.

Do not publish a binary release containing the inherited LuKa components until the upstream licensing
terms or written permission cover redistribution. The GitHub fork preserves attribution and history,
but the fork relationship is not itself a software license.
