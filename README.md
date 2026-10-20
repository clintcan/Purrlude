<p align="center"><img src="docs/user-guide/images/Purrlude-Cat-Logo.png" alt="The Purrlude logo: a cat in headphones riding a sound wave above the word Purrlude" width="320"></p>

# Purrlude

Purrlude is a cross-platform music workstation for writing, recording, arranging and mixing songs, and for seeing them as sheet music as you go. It aims to be easy for beginners and deep enough for power users.

<img src="docs/user-guide/images/overview.png" alt="Purrlude: the arrangement with melody, piano, bass and drum tracks, the live score with chord symbols below, and the Loops panel on the right" width="100%">

> **Status: 1.0 on macOS and Windows.** Everything below works and has been used for real on macOS (Apple Silicon) and Windows. On Linux it builds, passes the tests in CI, and has been tried on Ubuntu 24.04 (ARM): importing, the score, separating instruments and playback work, but it hasn't seen everyday use, and Bluetooth MIDI isn't there yet. **Download:** a Linux AppImage (x86-64) is on the [releases page](https://github.com/clintcan/Purrlude/releases/latest); on macOS and Windows, build from source (below). The project format is frozen: songs saved by 1.0 open in every later version.

## What it does

- **Sheet music from your song.** Every track becomes a part in a live score that follows playback. It includes chord symbols, drum notation and transposing instruments, and exports to PDF (full score or parts) and MusicXML.
- **Audio to MIDI.** It turns a recorded melody, chords or drums into notes, and can separate a mixed song into drums, bass, vocals, guitar, piano and the rest. Everything runs on your computer; no audio is sent anywhere.
- **Recording.** It records MIDI and audio, with count-in, metronome, loop recording into takes, and punch in/out.
- **Arranging and editing.** Clips can be split, moved, copied and resized, and there's a piano roll. Songs can change tempo (suddenly or gradually) and time signature partway through. Every edit can be undone, and projects autosave with crash recovery.
- **Built-in sounds and plugins.** It has the 128 General MIDI instruments with many of their variations, nine drum kits, drum loops in 14 styles, and bass and chord accompaniment in any key; it can also play any SoundFont (.sf2) you choose. It loads third-party instruments and effects (VST3, Audio Units, LV2), and MIDI files in General MIDI 2, GS and XG keep their drum kits and variations.
- **Mixing.** Each track has volume, pan, mute and solo, plus reverb, delay, EQ and a compressor, and there's a shared reverb and a master with its own effects. Volume, pan, the reverb send, every effect control and any plugin control can be automated, by drawing or by recording the moves you make, and knobs on a MIDI controller can be learnt to control them. Export as WAV, FLAC or Ogg, or as stems.
- **Devices.** It works with any audio interface and any USB or Bluetooth MIDI keyboard. It can play a keyboard's own sounds, and play songs through multi-timbral sound modules and software synths.

The **[user guide](docs/user-guide/README.md)** covers all of it, with screenshots.

## What's next

- **Releases:** a signed, notarised macOS app and a Windows installer (Linux has an AppImage).
- **Testing:** everyday use on Linux, Bluetooth MIDI there, and testers on other machines and devices.

**The project format is stable:** songs record their format version, and a saved song using every feature is tested on every build, so songs saved by 1.0 keep opening ([format](docs/project-format.md)).

## Building

Purrlude is C++20 on [JUCE](https://juce.com) 8 and [Tracktion Engine](https://github.com/Tracktion/tracktion_engine) 3.2. You need CMake 3.22 or later and a C++20 compiler (Xcode or Clang on macOS, Visual Studio 2022 on Windows, GCC or Clang on Linux). Dependencies are downloaded on first configure into `.cpm-cache/`, which is slow the first time.

```sh
# macOS, Linux, or any platform with Ninja
cmake --preset ninja
cmake --build --preset ninja-debug
ctest --preset ninja-debug

# Windows (Visual Studio 2022)
cmake --preset vs2022
cmake --build --preset vs2022-debug
ctest --preset vs2022-debug
```

**A release build** (optimised, for everyday use) builds just the app, without the tests:

```sh
# macOS, Linux, or any platform with Ninja
cmake --preset ninja
cmake --build --preset ninja-release --target Purrlude

# Windows (Visual Studio 2022)
cmake --preset vs2022
cmake --build --preset vs2022-release --target Purrlude
```

The app is in `build/Purrlude_artefacts/Release`: `Purrlude.app` on macOS, `Purrlude.exe` on Windows and `Purrlude` on Linux. On Windows and Linux it needs the files built next to it (libraries, `verovio/`, `soundfonts/`, `licenses/`), so copy or move the whole folder. To package it:
- **Windows installer:** `python tools/packaging/make_windows_installer.py build/Purrlude_artefacts/Release out/`. It needs Pillow and Inno Setup 6.
- **Linux AppImage:** `tools/packaging/make_appimage.sh build/Purrlude_artefacts/Release out`. The `appimage` workflow runs this on Ubuntu 22.04, so the AppImage works on older distributions.

On Linux, install JUCE's system packages first:

```sh
sudo apt-get install ninja-build libasound2-dev libjack-jackd2-dev ladspa-sdk libcurl4-openssl-dev \
  libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
  libxinerama-dev libxrandr-dev libxrender-dev libwebkit2gtk-4.1-dev libglu1-mesa-dev mesa-common-dev
```

**Separating instruments** needs the Demucs model, fine-tuned for Purrlude (`htdemucs_6s-purrlude-1.onnx`, 115 MB). The first time you separate, Purrlude offers to download it from this repository's `models-2` release (checked against its SHA-256). Unlike Purrlude, the model isn't under an open-source licence: it's built from Meta's Demucs weights, which Meta provides only for scientific purposes. `tools/models/export_demucs.py --weights` makes the same file from that release's weights. Everything else works without it.

## Licence

Copyright © 2026 Clint Christopher Canada. Purrlude is free software under the **GNU Affero General Public License v3.0**; see [LICENSE](LICENSE). It's built on JUCE (AGPLv3) and Tracktion Engine (GPLv3).

It also uses:
- **[Verovio](https://www.verovio.org)** (LGPLv3) for score engraving
- **[ONNX Runtime](https://onnxruntime.ai)** (MIT), with telemetry switched off
- Spotify's **[basic-pitch](https://github.com/spotify/basic-pitch)** model (Apache 2.0)
- Meta's **[Demucs](https://github.com/facebookresearch/demucs)** (code MIT; its trained weights, which the separation model is built from, aren't MIT-licensed: Meta provides them only for scientific purposes, see [demucs#327](https://github.com/facebookresearch/demucs/issues/327))
- **[TinySoundFont](https://github.com/schellingb/TinySoundFont)** (MIT) with the **GeneralUser GS** SoundFont
- drum loops from Google Magenta's **[Groove MIDI Dataset](https://magenta.tensorflow.org/datasets/groove)** (CC BY 4.0)

[content/LICENSES.md](content/LICENSES.md) lists every bundled asset with its source, author and licence.
