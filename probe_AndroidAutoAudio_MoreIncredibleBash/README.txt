Android Auto 48 kHz / 24-bit negotiation probe
================================================

This is an isolated experiment. It does not install or replace the cockpit,
HUD, arrow, album-art, video, decoder, upmix, GAL, or libautoreceiver files.
It changes only audio.sink.media.bits in gal.json, from 16 to 24, after making
an exact backup on the unit.

The experiment asks one narrow question: will the current Android Auto phone
accept PCM 48 kHz / 24-bit / stereo from this receiver? Generic Android 24-bit
API support does not answer that projection-protocol question.

Enable
------
1. Copy this package to the M.I.B. SD card.
2. For the recommended one-trip workflow, put an empty file named
   sq5_aa_audio_24_once in the SD-card root.
3. Run the M.I.B. custom script and reboot the MMI.
4. Connect Android Auto and play music for two minutes. Trigger one navigation
   prompt. Note whether Android Auto connects and whether media is audible.
5. Run the same custom script again. It first saves gal.json, the Android Auto
   hook/startup logs, sloginfo and pidin under mib2q-aa-audio-probe/run-* on
   the SD card, then restores the exact pre-probe gal.json.
6. Reboot the MMI. The car is back on its original audio configuration and the
   SD card contains the evidence.

For a persistent laboratory test, sq5_aa_audio_24_on still enables 24-bit and
leaves it active until the explicit restore procedure below is run.

Restore
-------
1. Put an empty file named sq5_aa_audio_restore in the SD-card root.
2. Run the custom script. It restores the exact pre-probe gal.json.
3. Remove the marker and reboot the MMI.

Interpretation
--------------
- Phone rejects setup/session: stock Android Auto projection does not accept
  this candidate on the tested phone/build.
- Session connects but media is silent: phone negotiation may have succeeded,
  but the MHI2Q GAL/QSA renderer path did not accept the 24-bit stream.
- Music plays: logs must still prove 24-bit at the phone, GAL/QSA and CSD
  boundaries before this can be called genuine 24-bit playback.

This probe deliberately does not test 96 kHz: the audited MHI2Q downstream
transport is 48 kHz. It also does not test six phone channels because that is
a separate surround/driver experiment.
