# Bundled content

Every asset that ships inside Purrlude is listed here with its source, author and license.
Only content whose license permits redistribution inside the app may be added.

| Asset | Used for | Source | Author | License |
|---|---|---|---|---|
| GeneralUser GS v2.0.3 (`GeneralUser-GS.sf2`) | Built-in General MIDI instruments | [github.com/mrbumpy409/GeneralUser-GS](https://github.com/mrbumpy409/GeneralUser-GS) at `684543d`; [schristiancollins.com](https://www.schristiancollins.com) | S. Christian Collins | GeneralUser GS License v2.0 (copied next to the app as `LICENSE-GeneralUser-GS.txt`) |
| GO:KEYS 3 / GO:KEYS 5 tone names and numbers (`devices/roland-go-keys-3-5.json`) | Choosing the keyboard's tones by name | Roland's published "GO:KEYS 3 / GO:KEYS 5 Tone List" (2024) | Roland Corporation | Factual reference data (tone names and MIDI addresses), used for interoperability; the PDF itself is not included |
| Drum loops (`loops/groove.json`) | Built-in drum loops (Loops panel) | [Groove MIDI Dataset](https://magenta.tensorflow.org/datasets/groove) v1.0.0 (MIDI only), extracted by `tools/content/build_groove_loops.py` | Jon Gillick, Adam Roberts, Jesse Engel, Douglas Eck, David Bamman (Google Magenta) | CC BY 4.0; attribution shown in the Loops panel |
| Accompaniment progressions and patterns (`accompaniment/patterns.json`) | Built-in bass and chord parts (Loops panel) | Written for Purrlude | Purrlude contributors | Original work, under Purrlude's license |
| Verovio fonts and glyph data (`data/`) | Score engraving | [github.com/rism-digital/verovio](https://github.com/rism-digital/verovio) 6.3.0 | RISM Digital Center and contributors; SMuFL fonts by their respective authors | LGPLv3 (Verovio); fonts under SIL OFL 1.1 |
| basic-pitch model ("nmp", ICASSP 2022, `nmp.onnx`) | Converting notes and chords in audio to MIDI | [github.com/spotify/basic-pitch](https://github.com/spotify/basic-pitch) v0.4.0 (`9991303`) | Spotify AB | Apache 2.0 (`LICENSE-basic-pitch.txt` and `NOTICE-basic-pitch.txt` in the app's licenses folder) |
| ONNX Runtime 1.30.0 (shared library) | Running the basic-pitch model | [github.com/microsoft/onnxruntime](https://github.com/microsoft/onnxruntime) release binaries | Microsoft Corporation | MIT (`LICENSE-onnxruntime.txt` and `ThirdPartyNotices-onnxruntime.txt` in the app's licenses folder) |

## Notes

- **Device definitions** (`content/devices/*.json`) are compiled into the app. Each lists a device's tone names with their Bank Select (MSB/LSB) and Program Change numbers.
  - **GO:KEYS:** bank and program are derived from Roland's tone numbers, which also corrects three misprinted entries (PR018–PR020).

- **GeneralUser GS:**
  - The license allows use for music creation, private or commercial, and allows use in software projects, including modifying it.
  - The author notes he can't be 100% certain of the origin of every sample, though none came from commercial sample libraries. Review this before any closed-source commercial release.
  - The license asks not to link directly to the author's download files. Purrlude fetches the file from the GitHub repository at a pinned commit, verified by SHA-256.

- **Drum loops:**
  - They are 4-bar excerpts of human performances on a Roland TD-11 kit, with its drum notes mapped to General MIDI. CC BY 4.0 requires noting changes, so the JSON records them.
  - The extraction script downloads the archive at a fixed URL and checks its SHA-256.
  - The credit, with a link, is shown at the bottom of the Loops panel.

- **basic-pitch and ONNX Runtime:**
  - The model is downloaded at a pinned commit, checked by SHA-256 and compiled into the app. Apache 2.0 requires its license and NOTICE to ship with it; both are copied into the app's licenses folder.
  - ONNX Runtime's release binaries are downloaded at a pinned version and checked by SHA-256.
  - **Telemetry:** official ONNX Runtime builds send usage telemetry to Microsoft by default. Purrlude turns it off before the runtime starts (`ORT_DISABLE_TELEMETRY=1`, then `DisableTelemetryEvents`). No audio or data leaves the machine.
