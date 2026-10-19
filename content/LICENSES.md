# Bundled content

Every asset that ships inside Purrlude is listed here with its source, author and license.
Only content whose license permits redistribution inside the app may be added.

| Asset | Used for | Source | Author | License |
|---|---|---|---|---|
| Purrlude logo (`docs/user-guide/images/Purrlude-Cat-Logo.png`), app icon (`branding/purrlude-icon.png`, cropped from the logo by `tools/branding/make_icon.py`) and the smaller logo it makes (`branding/purrlude-logo.jpg`) | The app icon, splash screen, About window, README and user guide | Made for Purrlude | Purrlude's author | Purrlude's own artwork, under Purrlude's license |
| GeneralUser GS v2.0.3 (`GeneralUser-GS.sf2`) | Built-in General MIDI instruments | [github.com/mrbumpy409/GeneralUser-GS](https://github.com/mrbumpy409/GeneralUser-GS) at `684543d`; [schristiancollins.com](https://www.schristiancollins.com) | S. Christian Collins | GeneralUser GS License v2.0 (copied next to the app as `LICENSE-GeneralUser-GS.txt`) |
| GO:KEYS 3 / GO:KEYS 5 tone names and numbers (`devices/roland-go-keys-3-5.json`) | Choosing the keyboard's tones by name | Roland's published "GO:KEYS 3 / GO:KEYS 5 Tone List" (2024) | Roland Corporation | Factual reference data (tone names and MIDI addresses), used for interoperability; the PDF itself is not included |
| Drum loops (`loops/groove.json`) | Built-in drum loops (Loops panel) | [Groove MIDI Dataset](https://magenta.tensorflow.org/datasets/groove) v1.0.0 (MIDI only), extracted by `tools/content/build_groove_loops.py` | Jon Gillick, Adam Roberts, Jesse Engel, Douglas Eck, David Bamman (Google Magenta) | CC BY 4.0; attribution shown in the Loops panel |
| Accompaniment progressions and patterns (`accompaniment/patterns.json`) | Built-in bass and chord parts (Loops panel) | Written for Purrlude | Purrlude contributors | Original work, under Purrlude's license |
| Verovio fonts and glyph data (`data/`) | Score engraving | [github.com/rism-digital/verovio](https://github.com/rism-digital/verovio) 6.3.0 | RISM Digital Center and contributors; SMuFL fonts by their respective authors | LGPLv3 (Verovio); fonts under SIL OFL 1.1 |
| basic-pitch model ("nmp", ICASSP 2022, `nmp.onnx`) | Converting notes and chords in audio to MIDI | [github.com/spotify/basic-pitch](https://github.com/spotify/basic-pitch) v0.4.0 (`9991303`) | Spotify AB | Apache 2.0 (`LICENSE-basic-pitch.txt` and `NOTICE-basic-pitch.txt` in the app's licenses folder) |
| Demucs v4 six-instrument model ("htdemucs_6s"), fine-tuned (`htdemucs_6s-purrlude-1.onnx`, downloaded on first use from the models-2 release) | Separating songs into instruments | Weights from [github.com/facebookresearch/demucs](https://github.com/facebookresearch/demucs) (`5c90dfd2-34c22ccb.th`), fine-tuned by `tools/training` and exported to ONNX by `tools/models/export_demucs.py --weights` with [sevagh/demucs.onnx](https://github.com/sevagh/demucs.onnx) at `81fa192` | Alexandre Défossez, Simon Rouard (Meta AI); ONNX adaptation by Sevag Hanssian; fine-tuning by Purrlude | MIT (code and weights) |
| Slakh2100 (training data for the separation model; not bundled) | Fine-tuning the separation model | [zenodo.org/records/4599666](https://zenodo.org/records/4599666) | Ethan Manilow, Gordon Wichern, Prem Seetharaman, Jonathan Le Roux | CC BY 4.0; credited in the acknowledgements and the models-2 release |
| FluidR3_GM (training renders only; not bundled) | Fine-tuning the separation model | Debian/Ubuntu's `fluid-soundfont-gm` | Frank Wen | MIT |
| ONNX Runtime 1.30.0 (shared library); 1.24.4 with DirectML on Windows x64 | Running the basic-pitch and Demucs models | [github.com/microsoft/onnxruntime](https://github.com/microsoft/onnxruntime) release binaries; on Windows x64 the `Microsoft.ML.OnnxRuntime.DirectML` NuGet package | Microsoft Corporation | MIT (`LICENSE-onnxruntime.txt` and `ThirdPartyNotices-onnxruntime.txt` in the app's licenses folder) |

## Notes

- **In the app:** `content/ACKNOWLEDGEMENTS.txt` is shown in About Purrlude and copied into the app's `licenses` folder with the full license texts. Keep it in step with this file when adding a dependency or asset.

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
  - **DirectML (Windows):** the DirectML build of ONNX Runtime (MIT) runs Demucs on the GPU through DirectML, which is proprietary (Microsoft's DirectML license) and isn't bundled. Purrlude uses the copy that comes with Windows (`System32\DirectML.dll`, part of the operating system since Windows 10 1903), so it's a system library; where it's missing or too old, separation runs on the CPU.
  - **Telemetry:** official ONNX Runtime builds send usage telemetry to Microsoft by default. Purrlude turns it off before the runtime starts (`ORT_DISABLE_TELEMETRY=1`, then `DisableTelemetryEvents`). No audio or data leaves the machine.

- **Demucs:**
  - The code and the pretrained weights are released under the MIT license. The weights were trained on MUSDB18-HQ (a research dataset) plus Meta's own songs. That's fine for an open-source release; review it before any closed-source commercial one.
  - At 115 MB the model isn't compiled in. It's looked for in the user's models folder, then beside the app, and the app offers to download it. Development builds copy it from the CPM cache into the app.
  - **Fine-tuning:** Purrlude's model is that network trained further on Slakh2100 (CC BY 4.0, which asks for credit, given) and on renders of Slakh's MIDI through GeneralUser GS and FluidR3_GM (whose licences allow rendering with them). The result keeps Demucs's MIT licence. Meta's original weights' training data (above) is still in it.
