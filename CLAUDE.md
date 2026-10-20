# Purrlude - A Digital Audio Workstation Cross Platform Software

Purrlude is a cross-platform digital audio workstation (DAW) for composing, recording, arranging and mixing music. It aims to be **easy for beginners and deep enough for power users**: a clean default UI that hides complexity until it's needed, with full control (automation, routing, keyboard-driven workflows) available underneath.

> **Status: 1.0 on macOS and Windows; Linux tried, not in everyday use** (the README says the same; keep them in step). Linux: CI, and a first real run on an Ubuntu 24.04 ARM VM (import, score, the model download, separation and playback work; no Bluetooth MIDI backend yet). Version 1.1.0 (`project()` in `CMakeLists.txt`).
> - **Works:** the signature features (score, audio to MIDI, separation, loops and accompaniment, devices) and recording, editing, mixing and export, in real use on macOS (Apple Silicon) and Windows.
> - **The project format is frozen** (format 1 in 1.0; format 2 from 1.1, which added drum kits and variations; `docs/project-format.md`): every later change needs an upgrade step.
> - **Still to do:**
>   - prebuilt releases for macOS (signed, notarised) and Windows (an installer can be made, `tools/packaging/make_windows_installer.py`, but isn't signed or published yet); Linux has an x86-64 AppImage since 1.0.1 (`appimage` workflow)
>   - everyday use on Linux, and its Bluetooth MIDI backend (BlueZ)
> - **Public:** github.com/clintcan/Purrlude, AGPLv3.

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
| Audio → MIDI (polyphonic) | Spotify basic-pitch model (v0.4.0) on ONNX Runtime 1.30.0, fetched by `cmake/OnnxRuntime.cmake` | Apache 2.0 / MIT |
| Audio → MIDI (monophonic) | In-house pYIN (`src/analysis/`) | — |
| Audio → MIDI (drums) | In-house band-energy onsets (`src/analysis/DrumTranscriber`); an ONNX classifier _(maybe later)_ | — |
| Instrument separation | Demucs v4 `htdemucs_6s` on ONNX Runtime (on the GPU through DirectML on Windows x64 and Core ML on macOS), exported by `tools/models/export_demucs.py` | MIT |
| Built-in instruments | TinySoundFont (SF2) + GeneralUser GS v2.0.3 GM bank; sfizz (SFZ) _(planned)_ | MIT / GeneralUser GS License v2.0 |
| Time-stretch | Signalsmith Stretch _(planned)_; Tracktion's SoundTouch is used until then | MIT / LGPL |
| Plugin formats | VST3 / AU / LV2 via JUCE and Tracktion (scan, instruments, effects, windows); CLAP via clap-juce-extensions _(planned)_ | — |
| Tests / CI | Catch2 v3, GitHub Actions (Windows/macOS/Linux) | BSL-1.0 |

**Licensing:** Purrlude is released under **AGPLv3** (`LICENSE`), as JUCE (AGPL) and Tracktion Engine (GPL) require; a closed-source version would need commercial licenses for both. Every new dependency must be license-compatible; prefer permissive (MIT/BSD/Apache) or LGPL. Avoid aubio (GPL) and Rubber Band (GPL) unless the project commits to GPL.

### Bluetooth MIDI backends

All behind `purrlude::BleMidiService` (`src/devices/BleMidiService.h`). A connected device appears as an ordinary MIDI port to the rest of the app.

The app talks to `purrlude::BluetoothMidiManager` (`src/devices/BluetoothMidiManager.h`), not the service directly. It remembers devices the user connected (in the app's settings file), reconnects them at launch and after unexpected drops (waiting 5 s after a disconnect and 15 s between attempts; manual connects within 5 s of a disconnect are deferred too), runs scans only while needed, and supports multiple listeners. The UI is `src/ui/MidiDevicesPanel`.

**MIDI output and sound source:**
- **Output channels:** `MidiDeviceSettings` (`src/devices/`) remembers each output device's channel by name, because keyboards differ. A Roland GO:KEYS sends on channel 1 but plays its own sounds only on channel 4. Set it under MIDI outputs in the devices panel.
- **Sound source:** the Instrument track's Sound menu chooses the built-in synth or an external output (`LiveInstrument::setSoundSource`, stored on the track's state).
- **How external output works:** the synth is disabled (disabled plugins pass MIDI through), a MIDI Patch Bay at the front of the chain remaps every channel to the device's channel, and the track outputs to the MIDI device. Recorded clips keep their original channel.
- **Multi-part devices** (`src/model/DeviceChannels.h`, namespace `device_channels`): a sound module or software synth (e.g. FluidSynth) plays a different sound on each channel. `MidiDeviceSettings::isMultitimbral` is set per device (the "Multi" switch under MIDI outputs, or `"multitimbral": true` in a device definition).
  - **Channels:** each track playing through such a device gets a channel of its own (`TrackInstrument::setOwnChannel`, marked on the track so it's kept): 10 for a drum kit, else the lowest free one, skipping 10; when all are taken, the least used. Change it from the track header's menu (MIDI Channel).
  - **Sounds:** a multi-part device without a tone list is General MIDI: the instrument menu lists the GM sounds (Bank Select 0/0 + Program Change) and the drum kits (channel 10; `sendToneToDevice` sends a kit's Program Change alone, since a Bank Select could take a GM2 or XG device's drum channel off its drum bank), and a track keeps its GM program when it moves there. `playsDrumKit` is also true for a track on channel 10 of such a device.
  - **Order matters:** set a track's channel before sending its tone or mix (`device_channels::playThrough`), or they'd change whatever track owns the device's default channel.
  - **Applying settings:** `applySettings` runs when device settings change. Turning Multi on gives each track its own channel; turning it off puts them all on the device's channel.
  - **Tests:** `[midi-out]` (macOS) checks what reaches a virtual MIDI port. The hidden `[.live-out]` plays a virtual keyboard through to a virtual multi-part device.
  - **If a keyboard plays nothing:** check it's sending at all before debugging. An Akai MPK mini 3 once stopped sending over USB with everything in Purrlude in order; unplugging and replugging it fixed it.
- **No echo to the same keyboard:** when the output device is also connected as an input, "All MIDI Ins" monitoring is switched off. The keyboard already sounds what you play, so echoing would double every note. On Windows a Bluetooth device's ports are "<name> (Bluetooth MIDI IN)" and "... OUT", and its input port is closed (read directly), so `LiveInstrument::isEchoingLiveInput` matches those names too.

**Instruments and device tones:**
- **Built-in GM instrument:** `SoundFontPlugin` (`src/instruments/`) plays GeneralUser GS (`cmake/SoundFonts.cmake`; downloaded at a pinned commit, SHA-256 checked, copied next to the app) with TinySoundFont.
  - It's registered by `purrlude::createEngine` (`src/model/EngineSetup.h`). Always create engines through it, in the app and in tests.
  - All TinySoundFont calls run on the audio thread, except `initialise()`, which pre-allocates voices and all 16 channels.
  - Program changes reach the audio thread through an atomic, and incoming Program Changes are ignored.
  - **Drum kits:** a program from 128 up is a drum kit: 128 + its Program Change number on the drum channel (`gm::drumKitProgram`, `isDrumKit`, `getKitNumber`). `gm::drumKits` lists General MIDI 2's nine (Standard 0, Room 8, Power 16, Electronic 24, Analog 25, Jazz 32, Brush 40, Orchestra 48, SFX 56; GS and XG number them the same), which GeneralUser GS has in bank 128. `gm::validProgram` keeps any kit number (another SoundFont may have more kits; one it hasn't got plays its standard kit), while MIDI import uses `gm::findDrumKit` (known kits, else Standard). Kits besides Standard came with project format 2.
  - **Variations:** a track also has a SoundFont bank (`TrackInstrument::setProgram (program, bank)`, `purrludeBank`, the plugin's `bank`; format 2). Bank and program reach the audio thread packed in one atomic; `applyProgram` uses `tsf_channel_set_bank_preset` and falls back to bank 0, so a bank the SoundFont hasn't got plays the GM sound. The instrument menu makes a GM sound with variations (banks 1-119 of the loaded SoundFont, `SoundFontPlugin::getPresets`) a submenu. MIDI import maps Bank Select to a bank (`getSoundFontBank`): GS's MSB 1-119, XG's LSB under MSB 0, GM2's LSB under MSB 121. GeneralUser GS is laid out like GS (bank 8 program 24 is Ukulele), so GS variations play as written; XG and GM2 ones only with a SoundFont laid out their way.
  - **Another SoundFont** (`SoundFontPlugin::useSoundFont`, `src/ui/SoundFontChooser` at the bottom of the Audio Settings window, the `soundFont` setting, applied in `PurrludeApplication::initialise` before the engine): for the whole app, not per song. `SharedFont` loads it on its thread (the bundled one instead if it won't load, with `getSoundFontError`), bumps a generation, and `fontChanged` (message thread) has every plugin rebuild its `tsf_copy` (`buildSynth`, as in `initialise`). Copies share the font's data until the last closes, so the old font is closed at once. Views that list presets (the instrument menu) watch `getSoundFontGeneration`.
  - **A track's own SoundFont** (`TrackInstrument::setSoundFont`, `purrludeSoundFont` and the plugin's `soundFont`, an absolute path; format 3): `FontCache` (in `SoundFontPlugin.cpp`) holds the app's `FontSource` and one per track SoundFont file, shared by tracks using the same file, loading each in the background when it's chosen or a song opens and unloading it when no plugin plays it (`releaseUnusedFonts`). A plugin plays its own once loaded, else the app's (`chooseFont`; it waits only for the app's), and rebuilds when either changes (`rebuildIfFontChanged`, compared by `builtFrom`; `getPlayingSoundFont` says which). One that's missing or won't load leaves the app's playing (`getState` gives the reason). **UI:** the track's sound menu lists *Built-in instruments* (the app's), the track's SoundFont and recent ones (`SoundFontChooser::getRecent`, the `recentSoundFonts` setting), and Choose a SoundFont... (`SoundFontChooser::chooseFile`); a missing one shows "(missing)" and the menu's tooltip says why. With its own SoundFont loaded, the instrument menu lists that SoundFont's presets by bank (it may not be General MIDI) instead of the GM list. Paths aren't gathered into the project folder (SoundFonts are large and their licences vary), so a song moved to another computer plays the app's SoundFont there until the track's is chosen again. Tests: `[instrument]`, and format 3's song.
- **External devices:** use their own tone banks, not GM. The GO:KEYS 5 ignores GM Program Changes and needs Bank Select 71/96 (and others) first.
  - `LiveInstrument::setDeviceTone` stores a bank + program on the track and sends it when the tone is chosen, when the device becomes the sound source, and when playback starts.
  - `ToneListener` opens its own connection to each MIDI input, turns Bank Select + Program Change into tones (`ToneParser`), and learns them into `MidiDeviceSettings`. A tone picked on the keyboard becomes the track's tone.
- **Device definitions:** `content/devices/*.json` are compiled in via `juce_add_binary_data` and read by `DeviceLibrary`. They give the receive channel (the default output channel) and the full tone list.
  - The GO:KEYS 3/5 list (1,228 tones) comes from Roland's tone list PDF.
  - Bank and program are derived from the tone number, because three printed entries are wrong.
  - **Not General MIDI over MIDI:** the GO:KEYS's tone list has GM2's sounds and drum kits (CM and CD banks), but it answers only its own banks, on channel 4 only: GM2's Bank Select 121/120, GM's 0/0 and a lone Program Change all leave its tone as it was, and other channels are silent (tested by ear with the hidden `[.gm2-probe]` and `[.gm2-probe-one-channel]`, `PURRLUDE_MIDI_OUT=GO:KEYS`; each step's number is spoken on a Mac).

**Tracks:**
- **Instrument tracks:** a tracktion `AudioTrack` is one unless it's marked as an audio track. `TrackInstrument` is a light handle whose state lives on the track (sound source, GM program, device tone, channel, preview).
- **Audio tracks** are marked by the `purrludeTrackKind="audio"` track property (`TrackInstrument::isAudioTrack`, `makeAudioTrack`).
  - `setUp` gives them only volume, pan and a meter, never a synth.
  - The keyboard never plays one: `LiveInstrument::selectTrack` ignores audio tracks, so the selected track is always an instrument track. There is always at least one (`canDeleteTrack`).
  - `clips::canHold` keeps MIDI clips on instrument tracks and audio clips on audio tracks, including when dragging.
  - Their headers have no arm or instrument menus, and a green stripe. The arrangement draws audio clips' waveforms with one `SmartThumbnail` per clip.
- **Audio import** (`src/model/AudioImport.h`, File > Import Audio File, Cmd-Shift-I, or drop files): each file is copied into the project's audio folder and goes on a new audio track at the playhead's bar, one undo step. `JUCE_USE_MP3AUDIOFORMAT=1` lets MP3s be read.
- **Recording audio** (`src/model/AudioInputs.h`):
  - Each audio track's header picks an input (a Tracktion wave input, e.g. "Input 1"; an input feeds one track), arms it, and toggles monitoring ("In").
  - Monitoring is off when an input is assigned (a mic through speakers feeds back); on means "automatic", heard while armed.
  - Record records every armed input along with the keyboard's track.
  - **Remembered inputs:** each track keeps its input by name (the `purrludeAudioInput` property). `restoreInputs`, run by `LiveInstrument`'s timer, gives it back after a device change recreates Tracktion's inputs, unless another track has taken it. A track that loses its input to another track forgets it.
- **Project audio folder** (`Project::getAudioFolder`): "<name> Audio" next to the project file, or `Unsaved Audio/<autosave id>` beside the autosaves until the first save (deleted on a clean close, kept after a crash for recovery).
  - `updateRecordingFolder` points every wave input's filename mask there, again when devices change.
  - **Saving gathers** every audio clip's file into the folder and writes its path relative to the project's folder. Tracktion's own relative paths depend on whether the project file exists yet, so we write them.
  - **Resolving:** Edits are created with a matching `filePathResolver` and `editFileRetriever` (sharing the project's file) before they load, so a project moved together with its folder still finds its audio.
- **Live input:** `LiveInstrument` routes "All MIDI Ins" to the selected track, which is also the armed one. It adds and deletes tracks (undoable, never the last instrument track), and resends device tones on play.
- **Mixer:** each track has volume, pan, mute and solo. Volume and pan come from its `VolumeAndPanPlugin`, which `TrackInstrument::setUp` adds to every track; for external sources they're also sent as CC 7/10.
  - **Undo:** mixer changes go through `TrackMix` (`src/model/TrackMix.h`). Tracktion keeps mute/solo out of undo, and the plugin's setters would record every fader move. `TrackInstrument` therefore writes volume/pan to the plugin state without the UndoManager (and reads them back from the state, since the parameter updates asynchronously). `TrackMix::change` records one undo step per click or drag.
  - **Parameters:** Tracktion doesn't pass a plugin state change on to the plugin's parameters, which are what's heard. So after writing volume or pan, `TrackInstrument` calls `updateFromAttachedValue()` on the plugin's parameters (`syncMixParameters`, also run by `setUp`). A render test checks that volume and pan change the audio.
- **Effects** (`src/model/TrackEffects.h`, namespace `track_effects`): Tracktion's reverb, delay, 4-band EQ and compressor, each with a few named controls (`getControls`).
  - They sit before the track's volume plugin, so the fader and meter show the final level. Add, remove and on/off are one undo step each.
  - `add` sets starting values (e.g. reverb wet 0.25, EQ bands at 150/500/2.5k/8k Hz, compressor attack 10 ms). The compressor's ratio is stored inverted (0.5 = 2:1), so its knob is reversed.
  - Delay times are note values at the song's tempo (`setDelayTime`), stored as `lengthMs`.
  - **Knob undo:** Tracktion records a parameter change in the song's state (through the UndoManager), but undo restores only the state, not the parameter that's heard. `track_effects::ParameterSync` (owned by `LiveInstrument`) listens to the Edit and, after any plugin state change, sets effects' and sends' parameters from their state (asynchronously: setting a parameter writes state, which isn't allowed during an undo). A test checks the undone value is heard.
  - **One step per drag:** `track_effects::ControlDrag` starts an undo step and holds a `tracktion::Edit::UndoTransactionInhibitor`, so a pause mid-drag doesn't split it (Tracktion closes a step 350 ms after the last change). A click uses `setControl`. Don't add custom UndoableActions that set parameters: setting one writes state through the UndoManager, which asserts during undo.
  - **UI:** the FX button on the header's name row (lit when the track has effects or a reverb send, disabled for external sound sources) opens `EffectsPanel` (`src/ui/`) in a CallOutBox. It holds the track by id and polls for changes (undo).
    - **Opening:** use `EffectsPanel::show`, which puts the call-out inside the button's window (so it can't run off the window's edge) and remembers the button, so Reverb Settings... points the shared reverb's panel at it too. The screenshot tour finds such call-outs among the window's children.
- **Automation** (`src/model/Automation.h`, namespace `automation`): Tracktion's curves on a track's volume, pan, reverb send and effect controls (`getTargets`; none for external sound sources).
  - **The curves** are Tracktion's: saved with the song, followed in playback and export, points in seconds. `TempoSetting::setBpm` remaps them with the clips, so they stay on their beats (tested).
  - **Showing:** each track shows at most one parameter's curve over its lane (`getShown`/`setShown`, the `purrludeAutomation` track property: a view setting, not undoable). The track menu's Automation submenu and the lane's label choose it.
  - **Editing:** `ArrangementView` draws the curve over the dimmed clips. A click adds a point, a drag moves it (`movePoint` keeps it between its neighbours; Shift snaps to beats), a double-click or the lane's menu removes it. `addPoint`, `removePoint` and `clear` are one undo step each. A drag holds an `automation::PointDrag` (an UndoTransactionInhibitor) so it's one step too.
  - **Automated controls** follow their curve and are disabled: the header's volume and pan, and the effects panel's knobs and send.
  - **Plugin controls:** `getPluginControls` lists every control of each loaded third-party plugin (not Tracktion's wet/dry), as `Target`s with a `group` (the plugin's name) and `shortName`. `getTargets` adds only the automated ones. `getShown` looks a shown plugin control up directly, since it runs on every repaint and a plugin may have hundreds.
    - **Menus:** `TrackHeader::addAutomationItems` builds both automation menus: Purrlude's controls, then a submenu per plugin with its automated, shown and recently moved controls, and Choose a Control... (`ControlChooser`, a searchable list in a call-out).
    - **Recently moved:** `automation::TouchedControls` (owned by `LiveInstrument`) listens to the Edit's `ParameterChangeHandler`. It counts a change only off automation and while a mouse button is down, since plugins also send changes of their own (e.g. when their window opens). Automation playback calls it on the audio thread, so it returns at once then.
    - **Tests:** `[plugins][automation]` automates DLSMusicDevice's volume and checks the fade is heard.
  - **Recording** (the transport's Automate button, A; `automation::isWriting`/`setWriting`): Tracktion's newer recorder (`EngineBehaviour::enableExperimentalAutomation`, set in `createEngine`) in touch mode. `automation::Recorder` (owned by `LiveInstrument`) sets every track to touch mode when a pass starts, and makes each pass one undo step: it begins the step at play and holds an `UndoTransactionInhibitor` until stop, then punches out.
    - **Off on open:** Tracktion keeps the switch in the transport state (on by default with that recorder), so `Recorder` turns it off for every Edit.
    - **Holding:** a control is recorded while it's held (`automation::Gesture`, i.e. `parameterChangeGestureBegin/End`): the header's fader and pan (`TrackHeader::mixDragStarted`), effect knobs and the send (`track_effects::ControlDrag` holds its parameter). A control moved without a gesture (a MIDI knob) is let go 2 s after it last moved.
    - **Undo:** controls moved during a pass don't start their own undo steps (`ControlDrag` skips its step; the header uses `TrackMix::changeInCurrentStep`), so undoing the pass also restores them.
    - **UI:** while Automate is on, automated controls are enabled (to record over them). The arrangement shows the control being recorded on its lane (`automation::findRecording`).
    - **Tests:** `[recording]` play the transport, so they skip without an audio device.
  - **Not done:** moving automation with clips.
- **Master** (the whole mix): Tracktion's master chain, which playback and export (`useMasterPlugins`) both go through: the master plugin list, then the master volume plugin, then the master meter.
  - **Volume and balance:** `TrackMix` with `TrackMix::masterID()` (an invalid id) reads and writes the master volume plugin's state and syncs its parameters, like a track's; changes are undo steps the same way. A new song's master starts at -3 dB (Tracktion's default).
  - **Effects:** `track_effects::getMasterEffects`, `addToMaster` (built-in or plugin, at the end of the list). `findPlugin` and `ParameterSync` include them.
  - **MIDI learn:** `automation::getMasterTargets` (volume, balance, master effects' controls), `findMasterTarget`, `findMasterParameter`. Mappings name the master with `TrackMix::masterID()` as their track.
  - **UI:** the strip in the ruler's corner (`ArrangementView::RulerOverlay`: the Master button, a volume slider, the meter). The Master button opens `EffectsPanel` in master mode (volume and balance knobs, Reverb Settings..., the master's effects).
  - **Not yet:** master automation (it has no lane, and Tracktion records only parameters on tracks).
  - **Tests:** `[master]`.
- **Third-party plugins** (VST3, AU, LV2; `src/model/PluginLibrary.h`, `src/ui/PluginWindows.h`):
  - **Scanning:** the toolbar's Plugins button opens JUCE's `PluginListComponent` on Tracktion's `knownPluginList`, which Tracktion saves in its own settings (`~/Library/Purrlude/Settings.xml` on macOS). Plugins load only once they're in that list.
  - **Out of process:** the app's engine (`createEngine (name, plugin_windows::Behaviour, true)`) scans in a child process: the app itself, started with a `PluginScan` command line. `PurrludeApplication::initialise` hands that to `PluginManager::startChildProcessPluginScan` before anything else, and such instances are allowed alongside the running app. Tests scan in-process.
  - **Instruments:** `TrackInstrument::setPluginInstrument` replaces the built-in synth (one undo step) and marks the track (`purrludePluginInstrument`), so `setUp` doesn't add the GM instrument when the plugin is missing on this computer. `useBuiltInInstrument` goes back. The instrument menu lists them under Plugins.
  - **Effects:** `track_effects::getEffects` includes `ExternalPlugin`s other than the instrument; `add (edit, track, PluginDescription)` puts one before the volume plugin. They have no knobs (`getControls` is empty): their rows have Open Window.
  - **Loading:** both load the plugin before touching the track, so a failure returns its error and changes nothing.
  - **Windows:** `plugin_windows::Behaviour` (a `tracktion::UIBehaviour`) makes them from Tracktion's editor; Tracktion owns and closes them. Tracktion opens no plugin window while anything is modal (`isDialogOpen`), and the FX panel's call-out is, so `plugin_windows::show` waits (up to 2 s) for modal components to close, and the panel dismisses its call-out first. `PluginWindow::setEditor` sets the window's resizability before the editor goes in: changing it afterwards re-lays out the window at once, and Apple's EQ and filter views (AULowpass, AUNBandEQ, ...) then read parameters before they have their plugin and their failed assertion ends the app. The hidden `[.plugin-ui]` test opens every Apple effect's window.
  - **Not yet:** sidechains.
  - **Tests:** `[plugins]` uses Apple's DLSMusicDevice and AUDelay on macOS (skipped elsewhere). `PURRLUDE_PLUGIN_FILE=/path/Synth.vst3 PurrludeTests "[.plugin-file]" -s` loads a real plugin as an instrument or effect, opens its window and renders a note through it.
- **Shared reverb** (`src/model/SharedReverb.h`, namespace `shared_reverb`): one reverb for the whole song, fed by per-track sends.
  - **The bus** is a hidden audio track (`purrludeTrackKind="reverbBus"`) with an aux return, a fully wet reverb, volume and a meter, made with the first send. It's solo-isolated, so a soloed track keeps its reverb. `TrackInstrument::setUp` leaves it alone.
  - **Song tracks:** the bus is not a song track. Code that means "the user's tracks" (arrangement, score, live input, import, loops, export) uses `purrlude::getSongTracks`, never `tracktion::getAudioTracks`.
  - **Sends** are aux sends (bus 0) after the track's volume, so the fader turns the reverb down too. They start at -inf; the first drag of the send knob makes the send (and bus) in the same undo step.
  - **Stems** render each track with the bus, so each stem has its own share of the reverb and the stems still add up to the mix.
  - **UI:** the top of each track's FX panel has the send knob and Reverb Settings..., which opens the same panel for the bus (its reverb's controls; Amount is the reverb's level). Effects added on the bus are fully wet.
  - **Level:** at a 0 dB send the reverb is nearly as loud as the direct sound and rings for about 1.5 s (room size 0.6).
- **UI:** the arrangement has one `TrackHeader` per track and scrolls vertically in a Viewport.
  - **Track menu:** rename, delete, automation and MIDI channel (`TrackHeader::showMenu`). A right-click opens it at the mouse, and the header's ⋯ button (`MenuButton`, its dots drawn: not every UI font has "⋯") opens it below itself, for those who wouldn't think to right-click. Tooltips and the guide call it the track menu.
  - **Zoom and horizontal scroll:** `TimelineView` (`src/ui/`, pure maths, tested) holds the beat at the left edge and the pixels per beat.
    - **Fit mode** (the default) shows the whole song as it grows. Zooming leaves it, and zooming out past the song or Cmd-0 returns to it.
    - **Controls:** pinch, Cmd+scroll and Cmd +/- zoom around the mouse or playhead. Sideways swipes and Shift+scroll scroll.
    - **Zoom bar:** the arrangement's `ZoomBar` (− + Fit and a scrollbar) is laid out by `MainComponent` under the viewport, so it doesn't scroll away.
    - **Playback:** the view pages forward when the playhead leaves it.
  - **Sticky ruler:** the ruler is an overlay kept at the top of the visible area. The viewport scrolls by moving the arrangement, so `ArrangementView::moved` repositions it.
    - It uses `-getY()`: during that move, `Viewport::getViewPositionY()` still has the old value.
    - `positionOverlays` also keeps the ruler and playhead above track headers added later.
  - **Meters:** `TrackInstrument::setUp` gives every track a `LevelMeterPlugin` after its volume. `LevelMeter` (`src/ui/`) shows it in the header, and the master level (`EditPlaybackContext::masterLevels`) on the ruler.
  - **Meter references:** `LevelMeter` looks the measurer up on every update and holds only a weak reference, since plugins and playback contexts come and go.
  - **External sources:** tracks played on an external device show no level.
- **Score:** the Score view and PDF/MusicXML export use `notation::buildEditScore` (a part per track with notes, one shared key, equal length). File > Export Parts as PDF writes one PDF per track.
- **Scene list:** `LiveInstrument` creates the Edit's scene list at setup. Tracktion otherwise creates it undoably inside the first track insertion, which breaks undoing that insertion.

**MIDI learn** (`src/model/MidiControls.h`, owned by `LiveInstrument`; `src/ui/MidiLearnPanel`, the toolbar's MIDI Learn button):
- **Mappings:** a Control Change number and channel to an automation target (`automation::findTarget`: anything `getTargets` lists, or any plugin control). They're stored in the Edit (`PURRLUDE_MIDI_CONTROLS`), so they're saved with the song. Learning and removing one are undo steps. A controller controls one thing, and a control has one controller.
- **Input:** every `PhysicalMidiInputDevice` gets a listener (re-attached when devices change). It runs on the MIDI thread and keeps the latest value per controller; an `AsyncUpdater` handles them on the message thread. Bank Select (0/32, used for tones), data entry/RPN/NRPN and channel mode messages are ignored (`isReserved`). Tracktion's own `ParameterControlMappings` isn't used: it's only active for the UI's "focused" Edit and keeps its mappings outside the Edit's state.
- **Learning:** while the panel is open (`setLearning`), the last control the user moved waits for a controller. Moves come through the Edit's `ParameterChangeHandler` while a mouse button is down; the header's volume and pan write their state, so they call `automation::userMoved`. Mapped controllers don't move anything while learning.
- **Applying:** volume and pan go through `TrackMix::changeInCurrentStep` (they live in the track's state; MixChange actions coalesce), other parameters through `setNormalisedParameter`. Both are undoable, a burst at a time by Tracktion's undo timer, and recorded when Automate is on.
- **Tests:** `[midi-learn]`.

**Transport:** `TransportBar` stops playback 1 s after the last clip ends (`AudioExport::getSongRange`); recording never stops by itself. Play from at or past the end starts from the beginning. While recording, the take being recorded shows the notes played so far (`ArrangementView::readLiveNotes`, from Tracktion's `InputDeviceInstance::getRecordingNotes` queues: single-reader, so only it may read them).

**Tempo changes** (`src/model/TempoChanges.h`, namespace `tempo`):
- **Storage:** Tracktion's tempo settings; the first is the starting tempo at beat 0. A setting's curve is about the stretch after it: ±1 holds its tempo (the next one jumps), 0 ramps in a straight line to the next. So a change's "gradual" flag is the curve of the setting before it. `add` makes a change sudden and gives it the previous setting's old curve; `remove` passes the removed one's curve back.
- **Keeping the song on its beats:** Tracktion's `insertTempo`/`removeTempo` don't move clips (they're placed in seconds), so every edit runs inside an `EditTimecodeRemapperSnapshot` (save before, remap after), which keeps clips, automation and the loop on their beats. Audio clips without auto-tempo keep their length in time. Each edit is one undo step, and calls `sortAllClipsLikeTracktion`.
- **Time signature:** `changeTimeSignature` rescales tempo changes' beats with the notes, so they stay where they are in time.
- **UI:** the ruler draws each change (a green line and "120 BPM", "→ 120 BPM" when gradual) and its right-click menu adds, changes, makes gradual and removes them (`RulerOverlay::showTempoMenu`). The transport bar shows and edits the tempo at the playhead.
- **Score:** `notation::Score::tempoChanges` (in ticks), from `getTempoChanges`; MusicXML gets a metronome mark (or "120 BPM" words on screen, where the mark's note can't be drawn) at each change, and "accel."/"rit." where a gradual one starts. Changes after the music's last bar aren't written.
- **Not adapted:** a delay's note-value time uses the starting tempo.
- **Tests:** `[tempo]`.

**Loop playback** (`src/model/LoopRange.h`, namespace `loop_range`; the Loop button or L):
- **Storage:** the range is kept in quarter notes on the Edit's transport state (`purrludeLoopStart/End`, saved with the song), because Tracktion's loop points are in seconds and would drift off the bars when the tempo changes. Quarter notes also stay put when the time signature changes, like the music.
- **Syncing:** `sync` writes it to Tracktion's loop points; `TransportBar` calls it 20 times a second. The loop isn't part of undo, like the playhead.
- **Behaviour:** while looping, playback doesn't stop at the end of the song, and Play from outside the loop starts at its start (`getPlayStart`). Turning it on with no range loops four bars from the playhead's bar.
- **Recording while looping** starts at the loop's start (audio passes are counted from there), and each pass becomes a take. The loop can't be changed while recording.

**Takes** (`src/model/Takes.h`, namespace `takes`):
- **MIDI** uses Tracktion's takes (one MIDI list per pass, the last one current). `prepareMidiInputs` turns the MIDI inputs' `mergeRecordings` off while looping, since merging folds every pass into one clip; it's on otherwise (recording over a clip adds to it).
- **Audio** keeps its own list (`PURRLUDE_TAKES` on the clip, a `TAKE` with a `source` per pass, and `current`). Tracktion's audio takes need its project database: choosing a take that's a plain file deletes it. Its take list also writes paths relative to the project *file*, which asserts for an unsaved project, so `adoptRecordedTakes` finds the passes by Tracktion's file names instead ("<recording>", "<recording>_take_2", ...).
- **Which take is heard:** the last pass, unless an audio pass is under half the loop (recording stopped early), when it's the one before.
- **Finishing:** `takes::RecordingFinisher` (owned by `LiveInstrument`) handles every recorded clip, in the recording's undo step: it adopts audio takes and calls `clips::makeRoomFor`, so a recording replaces what it covers like any other clip.
- **Saving:** `Project::gatherAudio` gathers take files with the clip's own file.
- **UI:** clips with takes show "Take n/m"; the clip menu's Takes submenu chooses one (one undo step) or deletes the others (the files stay).
- **Tests:** `[loop-takes]` (the hidden `[takes]` tag is the MusicXML check of a real project).
- **Punch in/out** (the Punch button or P; `loop_range::isPunchOn/setPunchOn`, `purrludePunch` on the transport state): the loop range is also the punch range, as in Tracktion. `isPunching` (punch on, looping off) is Tracktion's `recordingPunchInOut`, which `sync` keeps in step: Tracktion refuses to record with both on, and loop recording already records only inside the range, a take per pass.
  - **Recording:** Record starts at `getRecordStart` (a bar before the punch-in, or the playhead if it's up to four bars before), and `TransportBar` stops it a bar after the punch-out (`getPunchStopTime`). Tracktion crops the recordings to the range. `takes::prepareMidiInputs` turns MIDI merging off while punching, so the punch replaces what was there.
  - **Tests:** `[punch]` (the real recording uses the simulated device, like `[loop-takes]`).
- **UI:** the ruler's top strip (`ArrangementView::RulerOverlay`) shows the range as a band: drag to set it (bars, Shift for beats), drag the band or its ends, click it to toggle. The band is orange while looping and vermillion while punching, and the lanes under it are shaded then. A clip's menu has Loop This Clip.

**Drum loops:**
- **Content:** `content/loops/groove.json` holds 37 4-bar loops in 14 styles from the Groove MIDI Dataset (CC BY 4.0). They're MIDI on the GM drum map, timed in quarter notes.
  - `tools/content/build_groove_loops.py <download-dir>` regenerates the file: it downloads the dataset (SHA-256 checked), picks the steadiest 4 bars of up to 3 beats per style, and maps the TD-11 kit to GM.
  - Hits just before a downbeat count as on it. Loops with a triplet feel are labelled "shuffle" and listed after the straight ones.
- **Model:** `LoopLibrary` (`src/model/`) loads the compiled-in loops. `insertLoop` puts one on the Drums track (a built-in drum-kit track, created if needed, without moving the keyboard's track), repeated to fill the bars, as one clip and one undo step. An empty song takes the loop's tempo.
- **UI:** `LoopsPanel` (`src/ui/`), the Loops toolbar button, shares the side with MIDI Devices. It has Drums / Bass / Chords modes, adds at the playhead's bar, and shows the drum loops' required credit.
- **Looping clips:** a loop is stored once in a looping clip, so lengthening the clip repeats it. `notation::getClipNotes` unrolls looping clips for the score and the arrangement.

**Accompaniment** (`src/model/Accompaniment.h`, content in `content/accompaniment/patterns.json`, original):
- **Progressions** are Roman numerals, one entry per bar; two numerals split the bar. Flats are explicit (`bVII`), and suffixes are `7`, `maj7`, `ø7`, `°`, `°7`. Each progression is written for major or minor keys.
- **Patterns** are one-bar bass or chord parts. Each note names chord tones, not pitches: R, 3, 5, 7, 8, 6, b7, 5- (fifth below), A (a half step below the next chord's root), C (the voiced chord) and v0–v3 (a note of the voicing), joined with `+`.
- **Generating notes:** bass roots sit in A1–G#2. Chords are voice-led: each takes the close-position inversion nearest the previous chord, over two passes so the loop joins.
- **Chord names** are spelled from the numeral's degree, so ♭VII in C is Bb.
- **Inserting:** `insert` puts one pass of the progression in a looping clip. It goes on a built-in track with the pattern's role (a track property) and GM program, created and named after the instrument if needed.
- **Key:** `detectKey` estimates the key from all non-drum tracks. The panel follows it until the user picks a key.

**Audio export** (`src/model/AudioExport.h`, File > Export Audio, Cmd-Shift-E):
- **Formats:** WAV or FLAC at 16 or 24 bits, or Ogg Vorbis at about 192 kbps, at 44.1 or 48 kHz, optionally peak-normalised to -1 dB. MP3 isn't offered: JUCE has no MP3 encoder.
- **What's rendered:** the mix, or stems (one file per track that has clips and isn't muted). The range runs from the start to the end of the last clip, plus up to 2 s of tail. The metronome isn't included.
- **Rendering:** it uses `tracktion::EditRenderer::render` on a background thread.
  - Its finished callback runs on the render thread, so results are posted to the message thread.
  - **Never join a render thread on the message thread.** Tracktion sets renders up and tears them down there with `callBlocking`, which waits without a timeout, so a join can deadlock. Finished or cancelled handles go to helper threads instead.
  - **Cancelling** (deleting the `AudioExport`) runs the message loop until those helpers finish, then joins them, so nothing outlives the export.
  - **Don't detach:** a detached thread crashed the Windows test runner at exit.
- **UI:** `ExportAudioDialog` (`src/ui/`) remembers the last choices in the settings file. `MainComponent` shows a progress window with Cancel and reveals the file when done.

**MIDI import** (`src/model/MidiImport.h`, File > Import MIDI File, Cmd-I, or drop files on the window):
- **Parts:** each track/channel pair with notes becomes an instrument track with one clip. The first Program Change picks the GM instrument, channel 10 is the drum kit, and so is a channel whose Program Change before its first note follows Bank Select MSB 127 (Yamaha XG) or 120 (GM2) (`isDrumBank`; the bank takes effect at a Program Change, as in a sound module; XG's SFX kits, MSB 126, aren't drums; GS sets drum channels by SysEx, which isn't read). A drum part gets the kit of the last Program Change before its first note (`gm::findDrumKit`: an unknown kit is the standard one). Other banks (GM2/XG/GS variations) are ignored, so those sounds play as their GM sound. Tracks take the file's track names or the instrument.
- **Content:** notes, CCs (not bank select), pitch bend, channel pressure and aftertouch come in. Ticks convert straight to beats, so placement doesn't depend on tempo.
- **Placement:** parts start at the playhead's bar.
- **Empty song:** it reuses the empty track and takes the file's tempo and time signature with their changes (inserted before the clips; tempo changes hold until the next, and signature changes that would change the beat unit are dropped), and its first key signature. SMPTE-timed files are refused.
- **Song key:** the key is stored on the Edit (`notation::get/setKeySignature`). Scores and the accompaniment's key use it instead of estimating, since jazz ii–V material fools the estimate.
- **Checking a real file:** `PURRLUDE_MIDI_FILE=/path/song.mid PurrludeTests "[.midi-file]" -s` imports it, prints its tracks and writes the score PDF next to it.
- **Undo:** the import is one undo step, left open like other arrangement edits.
- **Speed:** each part's events are written straight into a detached `SEQUENCE` ValueTree, and the clip is inserted with it (`insertClipWithState`), one undoable change per part. Adding them through a clip's `MidiList` makes each event an undoable change the Edit reacts to, and `MidiList::addNote` then finds the new note by a linear search. That took tens of seconds for a 12-part, 6-minute file.

**Time signature changes** (`src/model/TimeSignature.h`, namespaces `bars` and `meter`; the ruler's right-click menu):
- **One beat unit:** every signature in a song shares its lower number, because Tracktion counts beats in that unit; with it fixed, beats keep one length, so adding, changing or removing a change moves no music, only the barring after it. `changeTimeSignature` refuses a new lower number while there are changes, and the transport's menu and dialog don't offer one. Changing the beat unit partway through isn't supported.
- **Bars:** `bars::Map` (build once, ask many times: drawing) maps bars to beats and back, matching Tracktion's own bar arithmetic (tested): a change part way through a bar takes effect at the next bar. Everything that counts bars uses it (arrangement grid, ruler numbers, snapping, loop and punch ranges, split names, song length, the piano roll's grid, imports, loops and accompaniment), never the first signature's upper number.
- **Changes:** `meter::add` (at a bar's start, one undo step; a copy of the previous setting, so the same lower number), `setNumerator`, `remove`. Editing or removing one also removes changes that then repeat the signature before them, so taking out a bar of 3/4 takes out the "back to 4/4" too.
- **UI:** the ruler draws each change (a purple line and "3/4"), and where several changes share a bar line its line is split between their colours. The transport's button shows and changes the signature at the playhead.
- **Score:** `notation::MeasureMap` (built from the first signature and `MeterChange`s; a plain `TimeSignature` converts to it) gives each measure's start, length and signature. The score builder, chord detection and MusicXML (a `<time>` where it changes) use it. The song's bars are the score's measures.
- **MIDI import:** an empty song takes a file's signature changes that keep its beat unit.
- **Tests:** `[meter]`.

**Time signature** (`src/model/TimeSignature.h`, the "4/4" button in the transport bar):
- **What changes:** `changeTimeSignature` changes the song's one time signature as one undo step, keeping how the song sounds. Clips stay at their times and notes in place; only the barring changes.
- **Rescaling:** Tracktion counts beats in the signature's unit (quarters in x/4, eighths in x/8; BPM stays in quarters). So when the lower number changes, notes, controllers and loop ranges are rescaled, and clip positions are restored afterwards.
- **Not yet adapted:** drum loops and accompaniment patterns are 4/4 material, so they don't fit other meters.

**Clip editing** (`src/model/ClipEditing.h`, `ArrangementView`):
- **Operations:** `clips::move`/`moveAll` (also copies), `setEnd`, `setStart` (trims, keeping the content in place; a looping clip's offset wraps within its loop), `setFades` (audio), `remove`/`removeAll`, `split`/`splitClips`/`splitAll` (Tracktion's `split`, which fixes up offsets and looping clips), `duplicate`/`duplicateAll`, and `copy`/`paste`. A clip put over others replaces what it covers, splitting them if needed.
- **Groups:** the `...All` functions take the selection and are one undo step. `moveAll` places every clip before clearing under them, so a clip moving to where another selected clip was doesn't remove it.
- **Clipboard:** `clips::Copied` holds snapshots of the clips' states (IDs remapped on each paste), their starts from the first one, lengths in beats and tracks. `ArrangementView` keeps it, so it's per song: an audio clip's file may be stored relative to its project. Paste goes at the playhead; clips from one track go on the lane last clicked if it can hold them, otherwise each on its own track.
- **UI:** `ArrangementView` keeps a selection (click, Cmd-click, a box dragged across empty space, Cmd+A). A plain click on a clip also moves the playhead when stopped. A clip's parts (`ClipPart`): the body moves the selection, the edges trim, an audio clip's top-corner handles set the fades (shown on selected and hovered clips). Cmd+T (split the selected clips under the playhead, or every clip it crosses), Cmd+X/C/V/D, the clip menu (also Split Here, at the nearest beat), a right-click on an empty lane (Paste), and the Edit menu.
- **Undo:** each operation starts its own undo step. Tracktion re-sorts a track's clips asynchronously and records that as undo actions. It also closes the undo step 350 ms after a change (`UndoTransactionTimer`), so on a slow machine the re-sort could become a step of its own, and undo would revert only that.
  - So every edit that adds, moves or splits clips calls `sortAllClipsLikeTracktion` (`src/model/ClipOrder.h`, a copy of Tracktion's private sort) inside its step, leaving Tracktion nothing to move.
  - A test checks that no undo actions arrive after an edit.

**Piano roll:**
- **Editor:** `PianoRoll` (`src/ui/`) edits one MIDI clip of the instrument track. It's the Piano Roll tab of the lower panel; double-click a clip in the arrangement to open it there.
- **Edits:** they go through `notes::` operations in `src/model/NoteEditing.h` (add, move, resize, remove, quantize; all undoable). These are what's tested.
- **No stale pointers:** the editor holds the clip by `EditItemID` and notes by their `ValueTree` state, resolving them on each use.
- **Following playback:** while playing, notes under the playhead are drawn white with a yellow outline and their keys light up (`PianoRoll::getPlayingBeat` maps the playhead into the clip's content, through the loop for a looping clip). The view pages forward (and back) when the playhead leaves it, like the arrangement. The screenshot tour plays the demo with the master volume at -100 dB to capture it.
- **Previews:** `LiveInstrument::previewNote` injects into "All MIDI Ins" for the built-in synth, stamped with `getMillisecondCounterHiRes() * 0.001`; Tracktion drops 0-stamped input. For an external sound source it sends straight to the device, since live echo may be off.

Live playing: `purrlude::LiveInstrument` (`src/model/LiveInstrument.h`) puts the built-in 4OSC synth on the first track and routes Tracktion's "All MIDI Ins" virtual device to it, so any connected keyboard, Bluetooth included, plays it with no per-device routing.

- macOS (`src/devices/BleMidiService_mac.mm`, implemented, macOS 13+): CoreBluetooth scans and connects, subscribes to the MIDI I/O characteristic, then calls `MIDIBluetoothDriverActivateAllConnections()` (on the message thread; it fails with -304 from a background queue) so Apple's Bluetooth MIDI driver takes over the link and publishes a CoreMIDI device. CoreMIDI devices are matched to peripherals by advertised name. No public API removes a pairing, so `forget()` only disconnects. The test runner embeds `tests/PurrludeTests-Info.plist` for Bluetooth permission. Tested with a Roland GO:KEYS 5: it sends on channel 1 but only plays incoming notes on channel 4 (run the hardware test with `PURRLUDE_BLE_CHANNEL=4`), and it can wedge (CoreMIDI shows it online with no ports, no advertising) after a quick disconnect/reconnect; power-cycling the keyboard recovers it. Its "pairing" mode is for Bluetooth audio; BLE MIDI connects without it, and leaving it in pairing mode may make the wedge more likely. It keeps one pairing: after pairing with the Windows PC (which it needs there), connecting from the Mac fails with `CBErrorPeerRemovedPairingInformation` ("Peer removed pairing information") until the keyboard is forgotten in System Settings > Bluetooth; `describeError` says so.
- iOS: JUCE's `BluetoothMidiDevicePairingDialogue` / the same CoreBluetooth + CoreMIDI flow
- Android: JUCE's pairing dialogue / Android MidiManager
- Windows (`src/devices/BleMidiService_windows.cpp`, implemented): C++/WinRT `BluetoothLEAdvertisementWatcher` for discovery, custom pairing (just-works/confirm-only; PIN devices must be paired in Windows Settings), and a `GattSession` with `MaintainConnection` to hold the link. JUCE (`JUCE_USE_WINRT_MIDI=1`) lists connected BLE devices as normal MIDI ports, but a remembered device's MIDI doesn't go through them (below). Windows MIDI Services can replace this later.
  - **MIDI is read and written directly** (`BleMidiService::readsMidiDirectly`, `sendMidi`): Windows' own Bluetooth MIDI driver stops passing a device's messages after seconds to minutes, both ways (a GO:KEYS on this PC, input even in a bare JUCE test program), until it's reconnected; read over GATT, the same device never stopped. Once connected, the backend subscribes to the MIDI I/O characteristic, `BleMidiPacketDecoder` (`src/devices/BleMidiPacket.h`, tested) turns its packets into messages (timed on arrival), and `onMidiMessage` passes them on, on a Bluetooth thread; it resubscribes when `MaintainConnection` brings a dropped link back. `BluetoothMidiInput` (`src/model/`) hands each message to the device's (disabled) Tracktion `PhysicalMidiInputDevice`, which passes it to MIDI learn and All MIDI Ins as its port would, and to `ToneListener::handleMessage` under the input port's name. The devices panel lists such devices as "<name> (Bluetooth)". Tones are learned under the device's *output* port name: on Windows its ports are named "... (Bluetooth MIDI IN/OUT)", and the track's sound source is the output (with the input's name, a tone picked on the keyboard never matched it).
  - **Output:** `cmake/TracktionMidiOutputRouter.cmake` adds `tracktion::MidiOutputDevice::Router` to Tracktion (checked string edits at configure time, like the crill change). `BluetoothMidiOutput` (`src/model/`) is the router: a remembered device's Tracktion output opens no port, and everything sent to it (playback, tones, volume, previews) goes to `BluetoothMidiManager::sendMidi`. The backend queues messages per device and one task writes them in order, packed by `encodeBleMidi` to the ATT MTU, without response.
  - **What keeps a device connected:** Windows keeps the link up while any app holds a `BluetoothLEDevice` for it, or has one of its MIDI ports open (its MIDI driver connects for the port, and JUCE lists every paired device's ports). So the backend holds a device only while connecting or connected, and drops the reference on `disconnect()`. It doesn't call `Close()`: a process gets one shared object per device, so that would also close it under a lookup in flight. `BluetoothMidiPorts` (`src/model/`) disables the Tracktion MIDI ports of devices the user disconnected (`BluetoothMidiManager::getClosedPortNames`), and `ToneListener` skips them. Tracktion saves whether a device is enabled, so the ports it closed are kept in the settings and opened again at the next launch.
  - **Ports and timing:** on Windows a remembered device's input port is always closed (read directly, above), and its output stays closed until Purrlude has connected it, then opens fresh: a port opened before Purrlude's connection brings the link up through Windows' driver alone. At quit the outputs are saved closed, so the next launch's engine doesn't open them before the device is connected.
  - **Connecting:** after opening the `GattSession`, the backend asks for all services, uncached, so Windows connects at once. A GO:KEYS always refuses a search for one service by UUID (`0x80070016`, "The device does not recognize the command").
  - **Encryption:** the GO:KEYS's MIDI characteristic needs an encrypted link, so it must pair. On this Windows PC's Realtek RTL8852BE adapter (`USB\VID_0BDA&PID_B85B`), pairing timed out (`AuthenticationTimeout`) with the March 2024 driver and worked with 18.4045.0.3000 (June 2026, Microsoft Update Catalog). If pairing times out, update the Bluetooth driver first.
  - **Hardware checks:** `PURRLUDE_BLE_DEVICE=GO:KEYS PURRLUDE_BLE_CHANNEL=4 PurrludeTests "Connect and exchange MIDI" -s`; `PURRLUDE_BLE_ADDRESS=<id> PurrludeTests "[gattprobe]" -s` talks GATT directly, without the backend (`PURRLUDE_BLE_LISTEN_S=60` counts packets per second for a minute).
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
src/analysis/           Analysis layer: melody, chord (basic-pitch) and drum transcription, instrument separation (Demucs) (no UI, no Tracktion)
src/ui/                 UI components (TransportBar, ArrangementView, ScoreView, PianoRoll, MidiDevicesPanel, LoopsPanel, MidiLearnPanel, EffectsPanel, PluginWindows)
tools/content/          Scripts that generate bundled content (run by hand; output is committed)
tools/models/           Scripts that make the ML models that are too big to commit (run by hand)
branding/               The app icon (made from the logo by tools/branding/make_icon.py; ICON_BIG in CMakeLists.txt)
tests/                  Catch2 test runner (PurrludeTests); tests/projects holds songs saved in each project format version
docs/user-guide/        End-user guide: README.md index, one page per topic, images/ taken by the app (and the logo, Purrlude-Cat-Logo.png)
.github/workflows/      CI for Windows, macOS, Linux
```

### Audio → MIDI (melody)

`src/analysis/` has no Tracktion dependency; it works on plain audio buffers.

- **`trackPitch`** is pYIN. Per frame (1024 samples, hop 128, at 22.05 kHz), it computes YIN's difference function by FFT and scores its cumulative mean normalised difference against 100 thresholds with a Beta(2,18) prior, which gives pitch candidates.
  - A Viterbi pass over 321 pitch bins (fifths of a semitone, E1 to G#6) × voiced/unvoiced picks a smooth path: jumps of up to 5 semitones, voicing switching with probability 0.01.
  - Frames quieter than about -50 dBFS are unvoiced. Loudness is measured over 256 samples at the frame's centre, to follow attacks.
- **`segmentNotes`:** it bridges unvoiced gaps under 30 ms. A note starts where pitched sound starts, where the pitch settles over 0.7 semitones away for 50 ms (so vibrato and slides stay one note), or at a re-attack (loudness falls below half the note's peak, then rises 1.8×).
  - **Pitch and velocity:** a note's pitch is the median of its middle frames; its velocity comes from its peak loudness.
  - **Minimum length:** notes under 60 ms are dropped.
- **`transcribeMelody`:** mixes to mono, low-passes at 5 kHz, resamples to 22.05 kHz, then tracks and segments. It's about 2.8 s per minute of audio in a debug build.
- **Drums** (`transcribeDrums`): kick, snare and closed hi-hat, mapped to GM 36/38/42.
  - **Spectra:** 512-sample windows (23 ms) every 64 samples at 22.05 kHz.
  - **Bands:** low 30–90 Hz (kick thump), mid 1–4 kHz (snare wires), high 6–11 kHz (hi-hat). A body band at 150–400 Hz is used only for comparisons.
  - **Finding hits:** a hit is a rise of at least 9 dB on a smoothed level (the maximum over 23 ms, since a low kick's raw level flickers) that dies away by 6 dB within 150 ms.
  - **Telling drums apart:** bands bleed into each other, so a rise struck together with another drum's only counts if its band holds up against that drum's. The thresholds are in `DrumTranscriberOptions`.
  - **Levels:** detection uses levels clamped at -50 dB; comparisons use true levels down to -100 dB.
  - **Tuning:** the defaults were tuned on built-in loops rendered through the GM kit (`PurrludeTests "[.drum-accuracy]" -s` reports recall on hits at velocity 40+ and false hits).
  - **Results:** kicks about 90–100%, snares about 85% (not ghost notes), hi-hats about 75%. Toms and cymbals come out as the nearest of the three.
- **Notes and chords** (`transcribePolyphonic`, `src/analysis/PolyphonicTranscriber.h`): Spotify's basic-pitch model run by ONNX Runtime, for piano, guitar, or several instruments at once.
  - **The model** (`nmp.onnx`, 230 KB) is compiled in (`PurrludeModels`). It hears 2 s windows at 22.05 kHz overlapping by 30 frames, and gives, per 256-sample frame and piano key, how likely the key is sounding and starting. Half the overlap is dropped from each side of a window, so a frame's time is `frameTime`: window × 36164 samples + frame-in-window × 256.
  - **Notes** (`notesFromActivations`) are a port of basic-pitch's `output_to_notes_polyphonic` (onset peaks, notes lasting until a key is quiet for 11 frames, then the "melodia trick"), without pitch bends. On the same audio it gives exactly the notes basic-pitch's Python does. basic-pitch's own `model_frames_to_time` drifts on long files (about 0.14 s after 2½ minutes), so our frame times are worked out from the windows instead.
  - **Speed:** about 0.3 s per minute of audio in a debug build.
  - **ONNX Runtime** is a prebuilt shared library per platform, copied into the app (`Contents/Frameworks` on macOS, next to the exe elsewhere) and the test runner. Platforms without a prebuilt release (Intel Macs) build without it: `PURRLUDE_HAS_ONNXRUNTIME=0`, and the menu item is disabled (`audio_to_midi::isAvailable`).
  - **Windows x64** uses ONNX Runtime's DirectML build instead (the `Microsoft.ML.OnnxRuntime.DirectML` NuGet package, 1.24.4: it lags the plain releases; `PURRLUDE_DIRECTML`, on by default; `PURRLUDE_HAS_DIRECTML`), so Demucs can run on the GPU. DirectML itself is proprietary and isn't bundled: the app uses Windows' own `System32\DirectML.dll`.
  - **Telemetry:** official ONNX Runtime builds send usage telemetry to Microsoft. `getOnnxRuntime` sets `ORT_DISABLE_TELEMETRY=1` before creating its environment and calls `DisableTelemetryEvents`. Keep it that way: Purrlude sends nothing without consent.
  - **Licenses:** basic-pitch's LICENSE and NOTICE and ONNX Runtime's LICENSE and third-party notices are copied into the app's `licenses` folder.
  - **Settings:** both conversions use `audio_to_midi::getPolyphonicOptions` (onset 0.6, frame 0.3, `endBelowPeak` 0.6, notes from 0.09 s), tuned with `[.separate-score]` (`PURRLUDE_PLAIN_SWEEP=1` scores plain conversion on each part and on the mix): on single instruments F 0.597 against basic-pitch's 0.554 (0.262 against 0.222 with note ends), on whole mixes 0.531 against 0.495. Separation lowers the bass's frame threshold to 0.2.
  - **Known behaviour:** it can add a note an octave above a note with a strong second harmonic.
  - **Checking a real file:** `PURRLUDE_AUDIO_FILE=/path/file.wav PurrludeTests "[.poly-file]"` prints the notes found.
- **Separating instruments** (`separateStems`, `src/analysis/SourceSeparator.h`; on the GPU on Windows and macOS): Meta's Demucs v4 six-instrument model splits a song into drums, bass, other, vocals, guitar and piano.
  - **The model** (`htdemucs_6s-purrlude-1.onnx`, 115 MB, the `models-2` release) is Meta's weights fine-tuned by `tools/training` (10,250 steps on Slakh2100 and General MIDI renders; see Fine-tuning below), exported by `tools/models/export_demucs.py --weights` (PyTorch 2.5.1, sevagh/demucs.onnx at a pinned commit; SHA-256 `96dda58d…`). Meta's original (`htdemucs_6s.onnx`, `7924dfb5…`, the `models-1` release) is what the export makes without `--weights`; `separation::removeOldModels` deletes it from the user's folder once the new one is downloaded. ONNX can't express the spectrogram steps, so the export leaves them out and `SourceSeparator.cpp` does them: torch.stft/istft with Demucs's padding (`demucs::spectrogram`, `inverseSpectrogram`).
  - **Processing** follows Demucs's `apply_model` with no shifts: the song is normalised by its mono mix's mean and standard deviation, cut into 7.8 s segments overlapping by a quarter, each padded with the audio around it, and cross-faded back with triangular weights. Finished audio is passed on as soon as no later segment can change it, so a long song doesn't need gigabytes.
  - **Matches PyTorch:** on a real song, every stem matches PyTorch Demucs to 64–92 dB SNR (float rounding). About 0.3 s per second of audio in a debug build.
  - **Spectrograms in parallel:** each segment's 2 forward and 12 inverse spectrograms run on threads of their own (`inParallel`). With JUCE's own FFT (Windows, Linux) they took as long as the network on a GPU.
  - **GPU (Windows):** `loadDemucs` asks for the GPU (`makeSessionOptions (true)`: DirectML, high-performance adapter) and falls back to the CPU if the session won't start or a run fails mid-song (Windows can reset a busy GPU). `PURRLUDE_ONNX_DEVICE=cpu` forces the CPU.
    - **Graph fusion is off** (`ep.dml.disable_graph_fusion`): on an AMD Radeon (Ryzen 7 5700U) DirectML's fused graph for the first time-encoder layer (`tencoder.0`) gave stems hundreds of times too loud, the same every run. Unfused it matches the CPU (57–88 dB SNR) at the same speed. Found by exposing intermediate results as outputs and comparing CPU with GPU; exposing a node splits the fusion there, which is why the error came and went. Metacommands made no difference.
    - **Speed:** a 2-minute song took 43 s on that GPU (31 s network, 6 s spectrograms, 5.5 s loading the model) against about 2 minutes on its CPU (4 threads).
    - **Check:** `PURRLUDE_DEMUCS_MODEL=... PURRLUDE_AUDIO_FILE=... PurrludeTests "[.separate-gpu]" -s` separates 30 s both ways and checks every stem matches (40 dB+).
  - **GPU (macOS):** Core ML (`MLProgram`, `CPUAndGPU`), from the regular ONNX Runtime package. Compiling Demucs for it takes about 15 s per session, as long as separating 2 minutes on the CPU, so it's used only with a cache (`ModelCacheDirectory`, about 430 MB): `analysis::setGpuCacheFolder`, which `MainComponent` points at `~/Library/Caches/Purrlude/GPU Cache`. Without one, macOS uses the CPU.
    - **Cache:** a folder per model file, named by `getModelFingerprint` (name, size, date), so a replaced model is never run from an old cache; other folders are deleted.
    - **Speed (M-series, debug build):** a 103 s song took 12.5 s from the cache, 23 s the first time, and 19–27 s on the CPU. Letting Core ML choose its units (Neural Engine included) was a little slower, the Neural Engine alone much slower, and `RequireStaticInputShapes` didn't help.
    - **Check:** the same `[.separate-gpu]` test (its cache is in `~/Library/Caches/PurrludeTests`); every stem matches the CPU's to 57–100 dB from the cache.
  - **Fine-tuning** (`tools/training/`, run by hand; nothing in the build): `train.py` fine-tunes `htdemucs_6s` with upstream Demucs 4.0.1 on Slakh-style songs (`slakh.py`: each instrument from `metadata.yaml` to one of the six parts by class), Slakh2100 (CC BY 4.0) and General MIDI renders of its MIDI (`render_gm.py`, FluidSynth with GeneralUser GS and FluidR3_GM, both permissively licensed), mixing random 7.8 s segments with random gains and Demucs's remix. Slakh has no vocals, so vocals are left out of the loss (training them toward silence would make the model forget singing): check a new model on songs with singing too. It checkpoints and resumes, trains in bfloat16 on CUDA, and reports each part's SDR on held-out songs. `tools/models/export_demucs.py --weights weights.th` exports the result, and `[.separate-score]` scores it with `PURRLUDE_SEPARATION_MODEL=model.onnx`. `setup_pod.sh` prepares a rented GPU machine (RunPod's PyTorch image: packages without replacing its PyTorch, Slakh2100 checked against Zenodo's MD5, the renders). On a Mac it works for trying things out (MPS), but one segment at a time already peaked at 11.5 GB. **Result (models-2):** on 16 of Slakh's test songs, right-track F 0.451 against the original's 0.431 (0.170 against 0.152 with ends; better on 11 and 12 of 16); vocals on real songs stayed within 10–15 dB of the original's at the same level; by ear (real songs), piano and strings land in their own parts and the bass is found where the original missed it. Training plateaued after about 10,000 steps (20,000 scored no better). The run cost under $2 on RunPod (an RTX 4090 did 4 steps/s with 12 workers). **Judging a new model:** `evaluate.py <PurrludeTests> <songs> <logs> original=a.onnx tuned=b.onnx` scores models with `[.separate-score]` on songs it wasn't trained on (Slakh's test songs, with `make_reference.py`'s reference.mid) and compares them; `[.separate-compare]` (`PURRLUDE_SEPARATION_MODEL`, optional `PURRLUDE_SEPARATION_BASELINE`, `PURRLUDE_SECONDS`) separates a real song with both models and prints, per part, how close the new one is to the old and its level beside it: on songs with singing, the vocals should stay close and not get quieter.
  - **Where the model lives** (`separation::findModel`): the user's models folder (`Models/` beside the settings file), then beside the app (`Contents/Resources/models` on macOS, `models/` elsewhere), then, in development builds, the CPM cache (`PURRLUDE_DEMUCS_MODEL`, also copied into the app). Without it, Separate Instruments offers to download it (`MainComponent::downloadModelThenSeparate`, then separates): `separation::getModelDownload` gives the URL (the `models-2` release of the GitHub repository), size and SHA-256, and `FileDownload` (`src/model/`) downloads into `<file>.part` on a background thread, resumes with an HTTP Range request, and checks the size and SHA-256 before moving it into place. Only ever on the user's go-ahead. Linux needs curl for it (`JUCE_USE_CURL` on Linux, `NEEDS_CURL` on the targets).
  - **What it can't do:** separation stops at those six; strings, synths and horns all go to "other". It's trained on real recordings: on a General MIDI render it put the sax in "piano" and much of the bass in "guitar", though the drums came out clean.
- **Model** (`src/model/InstrumentSeparation.h`, namespace `separation`): `Job` separates a clip on a background thread, writing each part to a WAV as it goes (the project's audio folder for audio tracks, a temporary folder otherwise). Parts more than 30 dB quieter than the mix are dropped. For MIDI, each part is converted the way that suits it (`convertPart`): drums with the drum converter, vocals with the melody one, the rest with notes and chords at onset 0.6 and frame 0.3 (bass: frame 0.2, up to A3), ending each note at 0.6 of its peak (`PolyphonicOptions::endBelowPeak`: an instrument's release stays over the threshold) and keeping notes from 0.09 s. Tuned on separated parts: right-track F 0.369 against 0.341 with basic-pitch's defaults, and 0.139 against 0.113 counting note ends. Then `cleanUp` removes bleed: a pitch starting within 50 ms in several parts stays only where it's loudest (`analysis::noteLevelDb`, three harmonics) and in parts within 10 dB of that (doublings). A small gain: right-track F 0.334 → 0.341 on BabySlakh's 20 songs. `insertParts` adds the tracks under the clip's track (vocals, guitar, piano, other, bass, drums), with GM sounds, or as audio tracks over the clip's span; one undo step.
- **Model** (`src/model/AudioToMidi.h`, mode melody, polyphonic or drums):
  - `getSource` reads, on the message thread, the part of the file a clip plays.
  - `Conversion` reads and transcribes it on its own thread; deleting it cancels.
  - `insertNotes` puts the notes on a new track under the audio track, over the clip's span, as one undo step: a piano track for a melody or chords, a drum-kit track for drums.
- **UI:** right-click an audio clip → Convert Melody / Notes and Chords / Drums to MIDI, or Separate Instruments to MIDI / Audio Tracks, with progress and Cancel from `MainComponent`.
- **Lead and backing vocals** (`src/model/VocalSplit.h`, `analysis::splitCentre` in `src/analysis/CentreSplit.h`; the clip menu's Split Lead and Backing Vocals (Rough), stereo clips only): no model, just where the voices sit. Per STFT frame and bin (2048, hop 512, Hann both ways), the share 2 Re(L R*) / (|L|² + |R|²) raised to `sharpness` 8 decides how much of the channels' average is centre. The centre (mono) is the lead, left and right minus it the backing vocals, so the two tracks add up to the clip exactly (tested by rendering both ways). `vocal_split::Job` writes the WAVs to the project's audio folder; `insertParts` adds two audio tracks under the clip's track, one undo step. Centred backing vocals stay with the lead. A model trained for lead/backing separation would do better, but most community karaoke models have no licence; check one's licence before shipping it.
- **Scoring separation to MIDI:** `PURRLUDE_AUDIO_FILE=song.wav PURRLUDE_MIDI_FILE=song.mid PurrludeTests "[.separate-score]" -s` separates once and scores the notes against the file's parts (same pitch, start within 50 ms; each part on its best track), before and after `cleanUp` and with its settings varied. The MIDI must be exactly what the audio was rendered from (a song's project can differ from the file it came from). BabySlakh (Slakh2100's first 20 songs, CC BY 4.0, Zenodo record 4603870; 16 kHz audio) works: combine each song's `MIDI/Sxx.mid` that has a `stems/Sxx.wav` (its metadata wrongly says nothing was rendered) into one file, drums on channel 10. Never commit it. What it showed: the bass is found worst (43% of its notes anywhere, 29% on its best track, often on "guitar"); piano and guitar are found more often than they land on their own track. Converting the bass with the melody converter was far worse, and moving it earlier didn't help. The test also sweeps the notes-and-chords thresholds per part (the model runs once per part; `PURRLUDE_SWEEP=0` skips it) and scores note ends (within 50 ms or a fifth of the note's length). Run the songs one at a time: a song peaks at about 3.7 GB (four at once ran a 16 GB Mac out of memory). Note ends are still the weakest part (about 14% of notes end where the real ones do); merging a track's broken-up-looking notes was worse, since they're mostly real repeats. Transcribing the whole mix and giving each note to its loudest part scored lower than separating (0.349 against 0.369). `PURRLUDE_STEMS_FOLDER=<folder>` reads the parts from `<folder>/<Stem>.wav` instead of separating: with BabySlakh's own instrument audio summed into the six parts (a perfect separation) the pipeline scores 0.601 (0.270 with ends), so a better separation is the biggest gain left; note detection is second.
- **Tests:** synthesized tones (`tests/MelodyTranscriptionTests.cpp`) cover pitches, timing, vibrato, legato, repeated notes, silence and velocity. `[.bench]` times a minute of audio.

### Projects

`purrlude::Project` (`src/model/Project.h`) owns one open song: its `tracktion::Edit`, the `LiveInstrument`, the file and the autosave.

- **File format:** projects are Tracktion Edit XML with a `.purrlude` extension, saved by writing a temp file and swapping it in. The default folder is `~/Documents/Purrlude`. Every file dialog opens in the folder last used in one (`recent_folder`, `src/ui/RecentFolder.h`, the `lastFileFolder` setting; `MainComponent::getStartFolder` falls back to the song's folder, then the default), keeping its suggested file name.
- **Format version** (`src/model/ProjectFormat.h`, `docs/project-format.md`): each save writes `purrludeFormat` on `EDIT`; opening runs `project_format::upgrade` (a step per version), and a song from a newer Purrlude opens with a warning (`Project::getOpenWarning`, shown by `MainComponent`). The format is frozen from 1.0: when Purrlude's additions to the file change, bump `currentVersion`, add an upgrade step, and keep every old song working.
  - **Saved songs:** `tests/projects/format-N` is a song saved in each version, using every feature (`ProjectFormatTests.cpp`, `[project-format]`, opens a copy of each and checks it all; `writeSong` and `checkSong` take the version, and format 2's drums use the room kit). `PurrludeTests "[.write-format-song]"` writes the current version's. Add new features to the song and its checks when they add to the file.
  - **Document** every new `purrlude...` property or `PURRLUDE_...` element in `docs/project-format.md`.
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
5. Separate voices (`separateVoices`): notes that start and end together are a chord; chords that overlap as separate lines (by more than half the shorter one, so legato overlaps don't count) go into an upper and a lower voice, the higher line upper wherever the two sound together. Most music stays one voice. Each voice is laid out with each chord lasting until its next onset, gaps as rests; a third overlapping line is cut, as everything was before. The lower voice (`Measure::lowerVoices`) is written only in measures where it plays: stems up for the upper, down for the lower, MusicXML voices 1/2 (5/6 on a grand staff's lower staff) with a `<backup>` between.
6. Split notes at barlines into readable values with ties. Dotted values start only on strong subdivisions.
7. Spell pitches for the key.

MusicXML uses 48 divisions per quarter and writes accidentals explicitly, once per measure and staff.

**Drums:** a built-in drum-kit track (`TrackInstrument::playsDrumKit`) becomes a percussion part.
- It has one staff with a percussion clef and no key signature, and is left out of key estimation.
- `drumPitch` places each GM drum (kick F4, snare C5, hi-hat G5 x, ride F5 x, crash A5 x), and drums sharing a position are written once.
- Two voices: hands (stems up) and feet (kick 35/36, pedal hi-hat 44; stems down, `isDrumPlayedByFoot`), unless the kit has only one. Each hit lasts until the next one in its voice and measure, with no ties.
- MusicXML uses `<unpitched>` and `<notehead>`; one voice has stems up. An open hi-hat is an x with `<technical><open-string/>`, because Verovio draws `circle-x` as a slash.

**Beams:** notes shorter than a quarter are beamed within each beat (each dotted quarter in compound time), with hooks for lone 16ths. Rests break beams.

A note quantized past the end of the performance moves back onto the last grid step, so it can't create a bar of its own.

**Triplets:** `chooseTripletBeats` decides per beat (quarter notes from the song start).
- **Detection:** onsets near ⅓ or ⅔ of a beat (and nearer than the grid) vote triplet, and onsets on the grid's off-beats vote straight. A beat is triplet if the triplet votes win.
- **Quantizing:** in triplet beats, times snap to thirds of a beat (16 ticks).
- **Note values:** the part of a span inside a triplet beat becomes triplet eighths and quarters, unless the span fills the beat.
- **MusicXML:** triplet notes get `<time-modification>` 3:2, and each beat's group a `<tuplet>` start and stop. Swing is written literally, as triplet quarter + eighth.
- `BuildOptions::triplets = false` turns this off.

**Transposing instruments:** `transpositionForProgram` maps GM programs to how they're written.
- **The table:**
  - Bass and guitar are written an octave up.
  - Tenor sax is a ninth up and baritone sax an octave and a sixth up.
  - Trumpet, clarinet and soprano sax are in B♭, and alto sax in E♭.
  - Horn and English horn are in F.
  - Piccolo, celesta, xylophone and glockenspiel are written lower.
- **Building:** `buildMultiPartScore` builds a transposing part from its written pitches, in its written key (`transposeKey`, preferring the simpler enharmonic). Staves and spelling then follow the written notes.
- **MusicXML:** the part gets `<transpose>` (diatonic, chromatic, octave-change), so other programs play it right.
- **Concert pitch:** `BuildOptions::concertPitch` writes everything as it sounds, and the Score tab has a toggle for it. Exports are transposed.
- **What's known:** only the built-in GM instrument (`notation::getTransposition`). Tracks played on an external device are written as they sound.

**Chord symbols** (`src/notation/ChordDetection.h`): `detectChords` names the chord in each half bar (whole bar in 2/4, 3/4, odd meters) from every pitched part's notes, weighted by how long they sound (low notes more), against 13 templates; the lowest note sounding for a quarter of the window gives a slash bass. Written only on change; fewer than three pitch classes gives none.
- **Spelling:** `spellChordRoot` spells by scale degree (bVII in C is Bb), unlike `spell`.
- **Placement:** `buildMultiPartScore` puts them on the first pitched part (`Part::chords`), transposed for it (`transposeChords`) unless concert pitch. `BuildOptions::chordSymbols` (default on; the Score tab's toggle).
- **MusicXML:** `<harmony>` with `<kind text="m7">`, `<bass>`, and `<offset>` from the measure start (Verovio honours it).
- **Accidentals:** Verovio writes chord-symbol accidentals as SMuFL characters (U+EA63–EA67) in its Leipzig font, which JUCE can't draw. `prepareVerovioSvg` turns them into "b"/"#" in the text's font (its size is on the sibling tspan).

**Not handled yet:** more than two voices per staff, time-signature changes of the beat unit, and other tuplets (quintuplets, triplets spanning two beats).

**On-screen score:** `ScoreView` (`src/ui/`) reads the song's notes twice a second on the message thread (`notation::getEditScoreInput`), skipping while recording. When they or the width change, its render thread builds the score and MusicXML (`buildEditScore (EditScoreInput)`), and, if the MusicXML changed, engraves it with `notation::ScoreRenderer` (Verovio) and builds a `notation::ScoreDrawing`. Verovio has global state, so it is only ever used from that one thread.
- **ScoreDrawing, not Drawable:** a `juce::Drawable` is a tree of Components, one per shape, and Components may only be used on the message thread (building one elsewhere fires a JUCE assertion per shape: tens of thousands for a song). `ScoreDrawing` (`src/notation/`) reads the prepared SVG into plain paths, fills and strokes in the drawing's units, so it's built on the render thread. It draws only the shapes in the area being painted, and finds and highlights shapes by element id.
  - **Same look as the Drawable:** a test draws a score both ways and compares the pixels. Two details matter. `Graphics::strokePath` with a transform takes the thickness in pixels, so stroke widths are scaled by the view's scale. Fonts get the whole family list like JUCE's parser ("Times, serif" matches nothing, so text is the default sans-serif, as in the PDF).
  - **The PDF export** still uses Drawables, on the message thread.
- **Full height:** `MainComponent`'s Full Height button (on the editors' tab bar, Cmd+Shift+F) gives the editors the whole window and hides the arrangement and its zoom bar. Hiding the editors turns it off.
- **Keep the message thread light:** for a long song (12 parts, 6 minutes, 35k notes, Release build) building the score takes about 1 s, engraving 30 s, and making the 110k-shape drawing a few seconds. Only reading the notes (a few ms) happens on the message thread.
- **Closing mid-engrave:** Verovio can't be interrupted, so a closed `ScoreView` doesn't join its render thread. It hands it to a registry that lets it finish and deletes it later. `ScoreView::waitForClosedRenderThreads()` joins what's left; the app calls it in `shutdown()` after the window is gone and before the engine.

JUCE's SVG parser can't draw Verovio's output directly. `prepareVerovioSvg` fixes it:
- Glyph `<use>` references become inline copies of their definitions.
- `currentColor` becomes the ink colour.
- `<style>` is replaced by a stroke attribute on the root.

The drawing is scaled to the SVG's declared pixel size (`getSvgDisplaySize`), because its coordinates are in Verovio's internal units.

**Following playback:** while the song plays, `ScoreView` colours the notes being played and scrolls to keep them in view.
- **Timing:** `ScoreRenderer::renderMusicXml` also returns Verovio's timemap, which gives each engraved note's id with its start and end in quarter notes. `parseTimemap` reads it.
- **Colouring:** `ScoreDrawing::setHighlighted` marks a note's shapes (everything inside the element with its id), and they're drawn in the highlight colour. There's no re-engraving.
- **Ties:** a note tied across a barline sounds as its first part, so the continuation isn't coloured.

The on-screen score and the PDF write tempos as words ("100 BPM", `MusicXmlOptions::tempoMark = false`): the metronome mark's note needs a SMuFL text font. Exported MusicXML has the real mark. Tempo marks go over the top part only.

**Title and composer** (`notation::getScoreTitle/getComposer`, `purrludeTitle`/`purrludeComposer` on `EDIT`; the Score tab's Title... button): MusicXML `work-title` and `creator type="composer"`, shown by Verovio's page header (now also on screen). Without a set title, the title is the song's name (`ScoreView::setSongName`; none for an unsaved song). `ScoreDrawing` places text by positioned `tspan`s too (`x`/`y`/`text-anchor`), as Verovio writes the header.

Verovio's `data/` folder (fonts and glyph files) is copied into the app (`Contents/Resources/verovio` on macOS, `verovio/` next to the exe elsewhere). Dev builds and tests fall back to the source tree via `PURRLUDE_VEROVIO_DATA`.

Verovio 6.3.0's `NO_EDIT_SUPPORT` doesn't compile, so leave it off.

Verovio's page header found no composer imported from MusicXML (an XPath union whose later steps lacked the full path): `cmake/patches/verovio-header-composer.patch` fixes it.

Verovio is patched (`cmake/patches/verovio-lazy-thread-locals.patch`, applied by CPM) so its `thread_local` statics with destructors are function-local. MSVC creates namespace-scope ones on every thread, the OS's worker threads included. In Debug builds, a thread that exits during process shutdown then destroys them after the STL's locks are gone, crashing the process. That made Windows test runs fail now and then after all their checks had passed. When upgrading Verovio, re-check `thread_local` in its sources and refresh the patch.

Only shapes with a `stroke-width` (lines, stems, ties) get the ink stroke. Stroking glyph outlines thickens them, and JUCE flattens stroked outlines into thousands of segments, which made PDFs about 6× larger.

**PDF:** `exportScorePdf` (File → Export Score as PDF, Cmd-E) engraves A4 pages with Verovio, with the title as a header. It draws each page's Drawable into a `PdfDocument` page. Everything is vector and text is written as glyph outlines, so no fonts are embedded. There are no images or true transparency. Individual-part PDFs will come with multiple instrument tracks.

**MusicXML:** File → Export MusicXML writes the same score.

`ScoreRenderer` serializes all Verovio calls on a global lock, because Verovio's state is global. Renderers on different threads (the score view and an export) are therefore safe.

**Checking real recordings:** `PURRLUDE_PROJECT=/path/song.purrlude PurrludeTests "[takes]" -s` writes `song.musicxml` and prints each measure.

`purrlude_core` is a CMake **INTERFACE** library: JUCE modules are compiled into each final target, so core sources are compiled into both the app and the test runner. Add new non-UI-shell sources to `purrlude_core`, not to the `Purrlude` target.

## Splash screen and About

- **Splash** (`src/app/Splash.h`): shown in `PurrludeApplication::initialise` before the engine is made (drawn at once with `performAnyPendingRepaintsNow`, since the message loop isn't running yet), and dismissed when the window is up: it fades after 3 s on screen, or when clicked. Not shown with `--doc-screenshots`.
- **About** (`src/app/AboutWindow.h`): the Purrlude application menu on macOS (`setMacMainMenu`'s extra Apple menu items), Help elsewhere (`MainComponent::aboutMenuId`). It shows the logo, version, "Made by Clint Christopher Canada", the AGPL, and `content/ACKNOWLEDGEMENTS.txt`.
- **Acknowledgements:** keep `content/ACKNOWLEDGEMENTS.txt` in step with `content/LICENSES.md` when adding a dependency or asset. It's compiled in with the logo (`branding/purrlude-logo.jpg`, made by `tools/branding/make_icon.py`) as `PurrludeBranding`, and copied with `LICENSE` (as `LICENSE-Purrlude.txt`) and the GeneralUser GS license into the app's `licenses` folder, which License Files... reveals.

## User guide

`docs/user-guide/` is the end-user guide: `README.md` is the index, with one numbered page per topic. Keep it in step with the UI: when a feature, label or shortcut changes, update its page.

**Screenshots** are taken by the app itself: `Purrlude --doc-screenshots <folder>` (`src/app/DocScreenshots.cpp`) builds a demo song from the built-in loops and accompaniment, opens each panel, menu and dialog, and saves PNGs. Pop-ups are separate windows, so they're drawn onto the main window's image where they are on screen.
- **How to run:** run the binary directly, e.g. `build/Purrlude_artefacts/Debug/Purrlude.app/Contents/MacOS/Purrlude --doc-screenshots docs/user-guide/images`. It quits when done (about a minute).
- **It doesn't touch the user's setup:** it uses its own settings folder ("Purrlude Doc Screenshots"), so the user's settings, last project and autosaves aren't touched, and it can run while Purrlude is open.
- **It needs the foreground:** it brings itself to the front before each step, because JUCE closes pop-up menus when the app isn't in front. Don't click elsewhere while it runs.
- **Adding a screenshot:** add a step in `DocScreenshots::addSteps`, then reference `images/<name>.png` from the page.

**PDF:** `python3 tools/docs/build_user_guide_pdf.py` rebuilds `docs/user-guide/Purrlude User Guide.pdf` from the pages, in the order `README.md` lists them. It adds a cover, a table of contents with page numbers and PDF bookmarks, and turns links between pages into links within the PDF. It needs Python's `markdown` package and WeasyPrint (`brew install weasyprint`).
- **List syntax:** it converts GitHub-style lists (no blank line before a list, two-space nesting) for Python's Markdown. Keep the pages in plain GitHub Markdown.
- **Image sizes:** give images an HTML `width`, which becomes their size in the PDF.
- **When to rebuild:** after editing the guide.

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

# Release (the app only): ninja-release or vs2022-release, into build/Purrlude_artefacts/Release
cmake --build --preset ninja-release --target Purrlude
```

**Linux AppImage:** `gh workflow run appimage` builds a Release on Ubuntu 22.04 (the oldest glibc it runs on) and runs `tools/packaging/make_appimage.sh`, which packs the build folder (the app and what it finds next to itself) with linuxdeploy and appimagetool (pinned releases, SHA-256 checked; the static type-2 runtime, so users don't need FUSE 2), and fails if a library would be missing. The AppImage is kept as the run's artifact for 14 days; attaching it to a release is a separate step. The separation model isn't included (downloaded on first use). The icon is `branding/purrlude-icon-256.png` (made by `tools/branding/make_icon.py`).

**Windows installer:** `python tools/packaging/make_windows_installer.py build/Purrlude_artefacts/Release <out>` (needs Pillow and Inno Setup 6) compiles `tools/packaging/purrlude.iss`: the build folder (not `models/`), `LICENSE` and `NOTICE` (shown as pages too), wizard images and icon drawn from `branding/`, and Microsoft's latest Visual C++ runtime installer (ONNX Runtime needs it), run only when the PC's runtime is older. Windows 10 1903+ (DirectML). Keep the `AppId` unchanged: upgrades and the uninstaller find Purrlude by it. It isn't code-signed yet, so SmartScreen warns.

Dependencies download into `.cpm-cache/` on first configure (slow the first time). MSVC builds compile with `/MP` (parallel files within a target). Linux needs the JUCE system packages listed in `.github/workflows/build.yml`.

## Conventions

- Namespace: `purrlude`. Code style follows JUCE/Tracktion (4-space indent, space before call parentheses, `camelCase` methods, `PascalCase` types).
- Use `tracktion::` (not the `te::` alias) in headers.
- Platform backends live in `*_windows.cpp` / `*_mac.mm` / `*_linux.cpp` files, added to `purrlude_core` with `$<PLATFORM_ID:...>` generator expressions.
- OS callbacks (WinRT, CoreBluetooth, D-Bus) run on background threads: copy plain data and post to the message thread via `juce::MessageManager::callAsync` + `juce::WeakReference`; never touch shared state from those threads.
- The `tracktion::Edit` is the project document; UI and analysis must never hold raw pointers into it across message-loop iterations without listening for changes.
- Add tests for model, notation and analysis logic; audio-engine code should have offline (non-real-time) render tests.
