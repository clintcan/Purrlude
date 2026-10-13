# Purrlude - A Digital Audio Workstation Cross Platform Software

Purrlude is a cross-platform digital audio workstation (DAW) for composing, recording, arranging and mixing music. It aims to be **easy for beginners and deep enough for power users**: a clean default UI that hides complexity until it's needed, with full control (automation, routing, keyboard-driven workflows) available underneath.

> Status: early stage. No code or tech stack has been committed yet. When a stack decision is made, record it in the "Tech Stack" section below and remove the "undecided" markers.

## Target Platforms

- Windows, macOS, Linux (desktop, primary)
- iOS / Android are possible later targets; avoid design choices that rule them out (e.g. keep the audio engine and core model UI-independent).

## Signature Features

These are what set Purrlude apart. Treat them as first-class, not add-ons.

### 1. Arrangement → Sheet Music
- Render a live music score (staff notation) directly from the arrangement: every instrument track maps to a staff/part.
- Score view stays in sync with the piano roll / arrangement; edits in either are reflected in both.
- Instrument-aware notation: correct clefs, transposing instruments, drum notation (percussion clef + drum map), tablature as a stretch goal.
- Quantization/notation cleanup layer that turns performed MIDI (humanized timing, overlapping notes) into readable notation without altering the underlying MIDI.
- Export: **PDF** (full score and individual parts), plus **MusicXML** and MIDI for interchange.

### 2. Audio → MIDI Conversion
- Convert recorded or imported audio into MIDI notes.
- Modes: monophonic melody (voice, single-line instruments), polyphonic (piano, guitar), and drum/percussion transcription (onsets → drum map).
- Output lands on a new MIDI track; user can review, correct, then view it as sheet music.
- Should run offline/locally; no audio is sent to external services without explicit user consent.

### 3. Flexible Input & Bluetooth MIDI
- Audio inputs: any device exposed by the platform audio API, multi-channel interfaces, per-track input selection, input monitoring.
- MIDI inputs: USB MIDI, virtual MIDI ports, multiple simultaneous controllers, MIDI learn for mapping knobs/faders.
- **Bluetooth LE MIDI**: scan for nearby BLE MIDI devices, show them in a device browser, pair/connect, remember paired devices and auto-reconnect.
  - Implement per platform behind a common interface (e.g. CoreMIDI BLE on macOS/iOS, Windows MIDI/WinRT BLE APIs on Windows, BlueZ on Linux, Android MidiManager).
  - Surface latency/connection state clearly in the UI; handle disconnects gracefully mid-session.

### 4. Software Instruments, Drum Loops & Accompaniment
- Built-in software instruments (synths, sampler, drum machine) available out of the box.
- Plugin hosting for third-party instruments/effects (VST3 / AU / CLAP / LV2, as supported per platform).
- **Built-in content library** of drum loops and accompaniment patterns (styles such as pop, rock, jazz, latin, etc.) sourced from **free/openly licensed sources**.
  - Every bundled asset must have its source, author and license recorded in a manifest (e.g. `content/LICENSES` or a metadata file per pack).
  - Only include content whose license permits redistribution inside the app (e.g. CC0, CC-BY with attribution shown in-app). Do not bundle CC-NC/ND or unclear-license material without flagging it.
  - Loops should be tempo/key-aware (stretch to project tempo, transpose accompaniment to project key).

## Core DAW Features

- Multi-track arrangement view (audio + MIDI tracks), clips, loops, markers, tempo and time-signature changes.
- Piano roll, drum editor, and score editor.
- Recording (audio and MIDI) with count-in, metronome, punch-in/out, loop/take recording.
- Mixer with sends, buses, groups, built-in effects (EQ, compressor, reverb, delay, etc.), and automation.
- Undo/redo for every edit; non-destructive editing.
- Project save/load with autosave and crash recovery.
- Export/bounce: WAV, FLAC, MP3/OGG, stems, MIDI, MusicXML, PDF score.

## UX Principles

- **Easy by default**: a new user should be able to pick an instrument, add a drum loop, record, and see the sheet music within minutes. Use sensible defaults and templates.
- **Powerful on demand**: advanced options live in inspectors, context menus and preferences, not in the user's face. Full keyboard shortcuts and customizable layouts for power users.
- Progressive disclosure over modes — avoid separate "beginner" and "pro" apps.
- Responsive and never blocks the audio thread; UI stays fluid during heavy projects.
- Accessible: scalable UI, keyboard navigation, colorblind-safe palettes.

## Architecture Guidelines

- Keep a clean separation between:
  - **Audio engine** (real-time, lock-free, no allocations/IO/locks on the audio thread)
  - **Project/document model** (tracks, clips, notes, automation — the single source of truth)
  - **Notation layer** (derives score from the model; never the source of truth for MIDI data)
  - **Analysis layer** (audio→MIDI, tempo/key detection; runs off the audio thread)
  - **Device layer** (audio, MIDI, Bluetooth MIDI — platform-specific backends behind shared interfaces)
  - **UI**
- Platform-specific code goes behind interfaces; the rest of the codebase must stay platform-neutral.
- Real-time safety is a hard rule: code reviews should flag any blocking call reachable from the audio callback.

## Tech Stack

**C++20 on JUCE 8, with Tracktion Engine as the DAW core.** Items marked _(planned)_ are not integrated yet.

| Area | Choice | License |
|---|---|---|
| Language / build | C++20, CMake ≥ 3.22, CPM.cmake (`cmake/CPM.cmake`) | — |
| Framework | JUCE 8 (pinned to the commit Tracktion Engine v3.2.0 ships with) | AGPLv3 / commercial |
| DAW engine | Tracktion Engine v3.2.0 (Edit model, tracks, clips, automation, recording, plugin hosting) | GPLv3 / commercial |
| UI | JUCE components with a custom LookAndFeel | — |
| Score rendering | Verovio 6.3.0 (MusicXML → SVG), built statically by `cmake/Verovio.cmake` | LGPLv3 |
| PDF export | In-house `notation::PdfDocument`: a JUCE `LowLevelGraphicsContext` that writes vector PDF | — |
| Notation cleanup | In-house (`src/notation/`): quantize, key estimate, spelling, staves, ties; MusicXML export | — |
| Audio → MIDI (polyphonic) | Spotify basic-pitch model on ONNX Runtime _(planned)_ | Apache 2.0 / MIT |
| Audio → MIDI (monophonic) | In-house pYIN _(planned)_ | — |
| Audio → MIDI (drums) | Onset detection + small ONNX classifier _(planned)_ | — |
| Built-in instruments | TinySoundFont (SF2) + GeneralUser GS v2.0.3 GM bank; sfizz (SFZ) _(planned)_ | MIT / GeneralUser GS License v2.0 |
| Time-stretch | Signalsmith Stretch _(planned)_; Tracktion's SoundTouch is used until then | MIT / LGPL |
| Plugin formats | VST3 / AU / LV2 via JUCE; CLAP via clap-juce-extensions _(planned)_ | — |
| Tests / CI | Catch2 v3, GitHub Actions (Windows/macOS/Linux) | BSL-1.0 |

**Licensing:** JUCE (AGPL) + Tracktion Engine (GPL) mean Purrlude must be released under AGPLv3, or commercial licenses for both must be purchased for closed-source distribution. **This decision is still open.** Every new dependency must be license-compatible; prefer permissive (MIT/BSD/Apache) or LGPL. Avoid aubio (GPL) and Rubber Band (GPL) unless the project commits to GPL.

### Bluetooth MIDI backends

All behind `purrlude::BleMidiService` (`src/devices/BleMidiService.h`). A connected device appears as an ordinary MIDI port to the rest of the app.

The app talks to `purrlude::BluetoothMidiManager` (`src/devices/BluetoothMidiManager.h`), not the service directly. It remembers devices the user connected (in the app's settings file), reconnects them at launch and after unexpected drops (waiting 5 s after a disconnect and 15 s between attempts; manual connects within 5 s of a disconnect are deferred too), runs scans only while needed, and supports multiple listeners. The UI is `src/ui/MidiDevicesPanel`.

**MIDI output and sound source:**
- **Output channels:** `MidiDeviceSettings` (`src/devices/`) remembers each output device's channel by name, because keyboards differ. A Roland GO:KEYS sends on channel 1 but plays its own sounds only on channel 4. Set it under MIDI outputs in the devices panel.
- **Sound source:** the Instrument track's Sound menu chooses the built-in synth or an external output (`LiveInstrument::setSoundSource`, stored on the track's state).
- **How external output works:** the synth is disabled (disabled plugins pass MIDI through), a MIDI Patch Bay at the front of the chain remaps every channel to the device's channel, and the track outputs to the MIDI device. Recorded clips keep their original channel.
- **No echo to the same keyboard:** when the output device is also connected as an input, "All MIDI Ins" monitoring is switched off. The keyboard already sounds what you play, so echoing would double every note.

**Instruments and device tones:**
- **Built-in GM instrument:** `SoundFontPlugin` (`src/instruments/`) plays GeneralUser GS (`cmake/SoundFonts.cmake`; downloaded at a pinned commit, SHA-256 checked, copied next to the app) with TinySoundFont.
  - It's registered by `purrlude::createEngine` (`src/model/EngineSetup.h`). Always create engines through it, in the app and in tests.
  - All TinySoundFont calls run on the audio thread, except `initialise()`, which pre-allocates voices and all 16 channels.
  - Program changes reach the audio thread through an atomic, and incoming Program Changes are ignored.
- **External devices:** use their own tone banks, not GM. The GO:KEYS 5 ignores GM Program Changes and needs Bank Select 71/96 (and others) first.
  - `LiveInstrument::setDeviceTone` stores a bank + program on the track and sends it when the tone is chosen, when the device becomes the sound source, and when playback starts.
  - `ToneListener` opens its own connection to each MIDI input, turns Bank Select + Program Change into tones (`ToneParser`), and learns them into `MidiDeviceSettings`. A tone picked on the keyboard becomes the track's tone.
- **Device definitions:** `content/devices/*.json` are compiled in via `juce_add_binary_data` and read by `DeviceLibrary`. They give the receive channel (the default output channel) and the full tone list.
  - The GO:KEYS 3/5 list (1,228 tones) comes from Roland's tone list PDF.
  - Bank and program are derived from the tone number, because three printed entries are wrong.

**Tracks:**
- **Instrument tracks:** every audio track is one. `TrackInstrument` is a light handle whose state lives on the track (sound source, GM program, device tone, channel, preview).
- **Live input:** `LiveInstrument` routes "All MIDI Ins" to the selected track, which is also the armed one. It adds and deletes tracks (undoable, never the last), and resends device tones on play.
- **Mixer:** each track has volume, pan, mute and solo. Volume and pan come from its `VolumeAndPanPlugin`, which `TrackInstrument::setUp` adds to every track; for external sources they're also sent as CC 7/10.
  - **Undo:** mixer changes go through `TrackMix` (`src/model/TrackMix.h`). Tracktion keeps mute/solo out of undo, and the plugin's setters would record every fader move. `TrackInstrument` therefore writes volume/pan to the plugin state without the UndoManager (and reads them back from the state, since the parameter updates asynchronously). `TrackMix::change` records one undo step per click or drag.
- **UI:** the arrangement has one `TrackHeader` per track and scrolls vertically in a Viewport.
- **Score:** the Score view and PDF/MusicXML export use `notation::buildEditScore` (a part per track with notes, one shared key, equal length). File > Export Parts as PDF writes one PDF per track.
- **Scene list:** `LiveInstrument` creates the Edit's scene list at setup. Tracktion otherwise creates it undoably inside the first track insertion, which breaks undoing that insertion.

**Drum loops:**
- **Content:** `content/loops/groove.json` holds 37 4-bar loops in 14 styles from the Groove MIDI Dataset (CC BY 4.0). They're MIDI on the GM drum map, timed in quarter notes.
  - `tools/content/build_groove_loops.py <download-dir>` regenerates the file: it downloads the dataset (SHA-256 checked), picks the steadiest 4 bars of up to 3 beats per style, and maps the TD-11 kit to GM.
  - Hits just before a downbeat count as on it. Loops with a triplet feel are labelled "shuffle" and listed last, since triplets can't be notated yet.
- **Model:** `LoopLibrary` (`src/model/`) loads the compiled-in loops. `insertLoop` puts one on the Drums track (a built-in drum-kit track, created if needed, without moving the keyboard's track), repeated to fill the bars, as one clip and one undo step. An empty song takes the loop's tempo.
- **UI:** `LoopsPanel` (`src/ui/`), the Loops toolbar button, shares the side with MIDI Devices. It adds at the playhead's bar and shows the required credit.

**Piano roll:**
- **Editor:** `PianoRoll` (`src/ui/`) edits one MIDI clip of the instrument track. It's the Piano Roll tab of the lower panel; double-click a clip in the arrangement to open it there.
- **Edits:** they go through `notes::` operations in `src/model/NoteEditing.h` (add, move, resize, remove, quantize; all undoable). These are what's tested.
- **No stale pointers:** the editor holds the clip by `EditItemID` and notes by their `ValueTree` state, resolving them on each use.
- **Previews:** `LiveInstrument::previewNote` injects into "All MIDI Ins" for the built-in synth, stamped with `getMillisecondCounterHiRes() * 0.001`; Tracktion drops 0-stamped input. For an external sound source it sends straight to the device, since live echo may be off.

Live playing: `purrlude::LiveInstrument` (`src/model/LiveInstrument.h`) puts the built-in 4OSC synth on the first track and routes Tracktion's "All MIDI Ins" virtual device to it, so any connected keyboard, Bluetooth included, plays it with no per-device routing.

- macOS (`src/devices/BleMidiService_mac.mm`, implemented, macOS 13+): CoreBluetooth scans and connects, subscribes to the MIDI I/O characteristic, then calls `MIDIBluetoothDriverActivateAllConnections()` (on the message thread; it fails with -304 from a background queue) so Apple's Bluetooth MIDI driver takes over the link and publishes a CoreMIDI device. CoreMIDI devices are matched to peripherals by advertised name. No public API removes a pairing, so `forget()` only disconnects. The test runner embeds `tests/PurrludeTests-Info.plist` for Bluetooth permission. Tested with a Roland GO:KEYS 5: it sends on channel 1 but only plays incoming notes on channel 4 (run the hardware test with `PURRLUDE_BLE_CHANNEL=4`), and it can wedge (CoreMIDI shows it online with no ports, no advertising) after a quick disconnect/reconnect; power-cycling the keyboard recovers it. Its "pairing" mode is for Bluetooth audio; BLE MIDI connects without it, and leaving it in pairing mode may make the wedge more likely.
- iOS: JUCE's `BluetoothMidiDevicePairingDialogue` / the same CoreBluetooth + CoreMIDI flow
- Android: JUCE's pairing dialogue / Android MidiManager
- Windows (`src/devices/BleMidiService_windows.cpp`, implemented): C++/WinRT `BluetoothLEAdvertisementWatcher` for discovery, custom pairing (just-works/confirm-only; PIN devices must be paired in Windows Settings), and a `GattSession` with `MaintainConnection` to hold the link. MIDI I/O goes through JUCE with `JUCE_USE_WINRT_MIDI=1`, which exposes connected BLE devices as normal MIDI ports. Windows MIDI Services can replace this later.
- Linux: BlueZ over D-Bus for discovery/pairing; BlueZ's MIDI profile exposes connected devices as ALSA sequencer ports

### Content library sourcing

- Ship loops and accompaniment primarily as **MIDI patterns** played by built-in instruments (tempo/key-agnostic, notatable, fewer license issues).
- Sources: Magenta Groove MIDI Dataset (CC BY 4.0, attribution required), original/commissioned patterns, CC0 audio only (e.g. CC0-filtered Freesound).
- Sample sets: GeneralUser GS, FluidR3_GM, VSCO-2 CE, permissively licensed SFZ drum kits — verify each license before bundling.
- Do not use sites whose terms forbid redistribution as a loop library (e.g. Looperman).

## Project Layout

```
CMakeLists.txt          Top-level build; fetches JUCE + Tracktion Engine via CPM
CMakePresets.json       Configure/build/test presets (vs2022, xcode, ninja)
cmake/CPM.cmake         Vendored CPM.cmake v0.40.2
src/app/                Application shell (JUCEApplication, main window)
src/devices/            Audio/MIDI/Bluetooth device layer, device settings, tone learning, device library
src/instruments/        Built-in instruments (SoundFont GM synth)
content/                Bundled content: LICENSES.md manifest, devices/*.json definitions, loops/*.json drum loops
src/model/              Project model on top of tracktion::Edit (Project, LiveInstrument)
src/notation/           Notation layer: performed notes -> Score -> MusicXML (no UI)
src/ui/                 UI components (TransportBar, ArrangementView, ScoreView, PianoRoll, MidiDevicesPanel, LoopsPanel)
tools/content/          Scripts that generate bundled content (run by hand; output is committed)
tests/                  Catch2 test runner (PurrludeTests)
.github/workflows/      CI for Windows, macOS, Linux
```

Planned directories as the architecture layers land: `src/analysis/`.

### Projects

`purrlude::Project` (`src/model/Project.h`) owns one open song: its `tracktion::Edit`, the `LiveInstrument`, the file and the autosave.

- **File format:** projects are Tracktion Edit XML with a `.purrlude` extension, saved by writing a temp file and swapping it in. The default folder is `~/Documents/Purrlude`.
- **Autosave:** autosaves go to `Autosave/` next to the app's settings file, never next to the user's file. `MainComponent` writes one every 30 s (not while recording), and each records which project file it belongs to.
- **Recovery:** a clean close deletes the autosave. Anything left over at launch is offered for recovery.
- **Unsaved changes:** these come from `Edit::hasChangedSinceSaved()`. Tracktion starts tracking changes only after the message loop runs, so tests must pump it before editing.
- **New projects:** setup (inserting the synth, metronome and count-in defaults) doesn't count as a change.

### Notation

`src/notation/` turns a track's notes into a `notation::Score` and writes it as MusicXML. `NotationBuilder` has no Tracktion dependency; `TrackNotation` reads the notes from an `AudioTrack`.

**Pipeline:**
1. Snap note timing to a grid. `chooseGrid` picks 8ths unless at least a quarter of the onsets sit on off-beat 16ths, because a 16th grid turns loose playing into ties.
2. Merge repeated same-pitch notes.
3. Estimate the key (Krumhansl-Schmuckler).
4. Choose the staves: a single staff if every note is within two ledger lines of it, otherwise a grand staff split at middle C.
5. Lay out one voice per staff: each chord lasts until the next onset, and gaps become rests.
6. Split notes at barlines into readable values with ties. Dotted values start only on strong subdivisions.
7. Spell pitches for the key.

MusicXML uses 48 divisions per quarter and writes accidentals explicitly, once per measure and staff.

**Drums:** a built-in drum-kit track (`TrackInstrument::playsDrumKit`) becomes a percussion part.
- It has one staff with a percussion clef and no key signature, and is left out of key estimation.
- `drumPitch` places each GM drum (kick F4, snare C5, hi-hat G5 x, ride F5 x, crash A5 x), and drums sharing a position are written once.
- Each hit lasts until the next one in its measure, with no ties.
- MusicXML uses `<unpitched>`, `<notehead>` and stems up. An open hi-hat is an x with `<technical><open-string/>`, because Verovio draws `circle-x` as a slash.

**Beams:** notes shorter than a quarter are beamed within each beat (each dotted quarter in compound time), with hooks for lone 16ths. Rests break beams.

A note quantized past the end of the performance moves back onto the last grid step, so it can't create a bar of its own.

**Not handled yet:** triplets, multiple voices per staff, tempo or time-signature changes, and transposing instruments.

**On-screen score:** `ScoreView` (`src/ui/`) rebuilds the MusicXML twice a second on the message thread, skipping while recording. When it changes, a background thread engraves it with `notation::ScoreRenderer` (Verovio). Verovio has global state, so it is only ever used from that one thread.

JUCE's SVG parser can't draw Verovio's output directly. `prepareVerovioSvg` fixes it:
- Glyph `<use>` references become inline copies of their definitions.
- `currentColor` becomes the ink colour.
- `<style>` is replaced by a stroke attribute on the root.

The drawing is scaled to the SVG's declared pixel size (`getSvgDisplaySize`), because its coordinates are in Verovio's internal units.

The on-screen score leaves out the metronome mark, whose note symbol needs a SMuFL text font.

Verovio's `data/` folder (fonts and glyph files) is copied into the app (`Contents/Resources/verovio` on macOS, `verovio/` next to the exe elsewhere). Dev builds and tests fall back to the source tree via `PURRLUDE_VEROVIO_DATA`.

Verovio 6.3.0's `NO_EDIT_SUPPORT` doesn't compile, so leave it off.

Only shapes with a `stroke-width` (lines, stems, ties) get the ink stroke. Stroking glyph outlines thickens them, and JUCE flattens stroked outlines into thousands of segments, which made PDFs about 6× larger.

**PDF:** `exportScorePdf` (File → Export Score as PDF, Cmd-E) engraves A4 pages with Verovio, with the title as a header. It draws each page's Drawable into a `PdfDocument` page. Everything is vector and text is written as glyph outlines, so no fonts are embedded. There are no images or true transparency. Individual-part PDFs will come with multiple instrument tracks.

**MusicXML:** File → Export MusicXML writes the same score.

`ScoreRenderer` serializes all Verovio calls on a global lock, because Verovio's state is global. Renderers on different threads (the score view and an export) are therefore safe.

**Checking real recordings:** `PURRLUDE_PROJECT=/path/song.purrlude PurrludeTests "[takes]" -s` writes `song.musicxml` and prints each measure.

`purrlude_core` is a CMake **INTERFACE** library: JUCE modules are compiled into each final target, so core sources are compiled into both the app and the test runner. Add new non-UI-shell sources to `purrlude_core`, not to the `Purrlude` target.

## Build & Test

```sh
# Windows (Visual Studio 2022)
cmake --preset vs2022
cmake --build --preset vs2022-debug
ctest --preset vs2022-debug

# macOS
cmake --preset xcode && cmake --build --preset xcode-debug && ctest --preset xcode-debug

# Linux / any platform with Ninja
cmake --preset ninja && cmake --build --preset ninja-debug && ctest --preset ninja-debug
```

Dependencies download into `.cpm-cache/` on first configure (slow the first time). Linux needs the JUCE system packages listed in `.github/workflows/build.yml`.

## Conventions

- Namespace: `purrlude`. Code style follows JUCE/Tracktion (4-space indent, space before call parentheses, `camelCase` methods, `PascalCase` types).
- Use `tracktion::` (not the `te::` alias) in headers.
- Platform backends live in `*_windows.cpp` / `*_mac.mm` / `*_linux.cpp` files, added to `purrlude_core` with `$<PLATFORM_ID:...>` generator expressions.
- OS callbacks (WinRT, CoreBluetooth, D-Bus) run on background threads: copy plain data and post to the message thread via `juce::MessageManager::callAsync` + `juce::WeakReference`; never touch shared state from those threads.
- The `tracktion::Edit` is the project document; UI and analysis must never hold raw pointers into it across message-loop iterations without listening for changes.
- Add tests for model, notation and analysis logic; audio-engine code should have offline (non-real-time) render tests.
