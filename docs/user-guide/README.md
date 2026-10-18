<p align="center"><img src="images/Purrlude-Cat-Logo.png" alt="The Purrlude logo: a cat in headphones riding a sound wave above the word Purrlude" width="240"></p>

# Purrlude User Guide

Purrlude is a music workstation for writing, recording, arranging and mixing songs, and for seeing them as sheet music. This guide covers what it can do today, one topic per page.

> **Status: 1.0 on macOS and Windows; Linux works in first tests but hasn't seen everyday use yet, and has no Bluetooth MIDI.** On Linux (x86-64) you can download it as an AppImage from the [releases page](https://github.com/clintcan/Purrlude/releases/latest); on macOS and Windows it's built from source ([README](../../README.md#building)). Songs saved by 1.0 open in every later version.

<img src="images/overview.png" alt="The Purrlude window: the transport bar at the top, the arrangement with four tracks, the score below, and the Loops panel on the right" width="100%">

To make a PDF of the whole guide, run `python3 tools/docs/build_user_guide_pdf.py`. It writes `Purrlude User Guide.pdf` here.

## Contents

| | Section | What's in it |
|---|---|---|
| 1 | [Getting Started](01-getting-started.md) | The window, and a first song in a few minutes |
| 2 | [Projects](02-projects.md) | New, open, save, autosave and recovering unsaved work |
| 3 | [Transport, Tempo and Looping](03-transport.md) | Play, record, tempo, time signature, metronome, count-in and loop playback |
| 4 | [Tracks and Mixing](04-tracks.md) | Instrument and audio tracks, sounds, volume, pan, mute, solo, the master strip and automation |
| 5 | [Arranging Clips](05-arrangement.md) | Moving, copying, resizing and deleting clips, zooming and scrolling |
| 6 | [Recording](06-recording.md) | Recording MIDI and audio, inputs, monitoring, and loop recording with takes |
| 7 | [Piano Roll](07-piano-roll.md) | Adding and editing notes, quantizing |
| 8 | [Sheet Music](08-score.md) | The live score, transposing instruments, and exporting PDF and MusicXML |
| 9 | [Loops and Accompaniment](09-loops-and-accompaniment.md) | Drum loops, bass lines and chord parts in any key |
| 10 | [Effects, Reverb and Plugins](10-effects.md) | Reverb, delay, EQ, compressor, the shared reverb, the master, and VST3/AU/LV2 plugins |
| 11 | [Audio to MIDI](11-audio-to-midi.md) | Turning melodies, chords and drums into notes, and separating a song into instruments |
| 12 | [Importing and Exporting](12-import-export.md) | MIDI and audio files in; audio, stems, PDF and MusicXML out |
| 13 | [Audio and MIDI Devices](13-devices.md) | Audio interfaces, MIDI keyboards, MIDI learn, Bluetooth MIDI and playing a keyboard's own sounds |
| 14 | [Keyboard Shortcuts](14-shortcuts.md) | Every shortcut and mouse gesture in one place |

## Conventions

- Shortcuts are written for macOS. On Windows and Linux, use **Ctrl** where this guide says **Cmd**, and **Alt** for **Option**.
- **Right-click** means a right-click, a two-finger click on a trackpad, or Ctrl-click on a Mac.

---

<sub>The PDF is built from these pages by `tools/docs/build_user_guide_pdf.py`. The screenshots are taken by the app itself: run `Purrlude --doc-screenshots docs/user-guide/images` from a development build (see `src/app/DocScreenshots.cpp`). It builds the demo song from the built-in loops and accompaniment, so they can be retaken whenever the interface changes.</sub>
