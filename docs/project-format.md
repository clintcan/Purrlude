# Purrlude project format

A Purrlude song (`.purrlude`) is a [Tracktion Engine](https://github.com/Tracktion/tracktion_engine) Edit, saved as XML, with Purrlude's own properties and elements added. This page describes those additions and how the format is versioned.

## Versions

The `EDIT` element's `purrludeFormat` attribute is the format version. Each save writes it. Songs saved before it was written are version 1.

| Version | Purrlude | Changes |
|---|---|---|
| 1 | 1.0 | The format described here |

**From 1.0 on, the format is frozen:** a song saved by one version of Purrlude opens in every later one.
- When the format changes, `project_format::currentVersion` (`src/model/ProjectFormat.h`) goes up, and `project_format::upgrade` gains a step that converts the older version as a song opens.
- A song saved by a newer Purrlude opens with a warning that parts of it may be missing.
- `tests/projects/format-N` holds a song saved in each version, using every feature. `ProjectFormatTests.cpp` opens each one and checks that nothing was lost. When the version goes up, keep the old songs and their checks, and write the new one with `PurrludeTests "[.write-format-song]"`.

Parts of the format are Tracktion's (tempo and time signatures, clips, plugins, automation curves), so a Tracktion upgrade can change them too. The saved songs' tests catch that.

## Files

- **The song:** `<name>.purrlude`.
- **Its audio:** `<name> Audio/`, next to it. Saving copies every audio clip's file (and every take) there, and clips refer to it by a path relative to the song's folder: `source="<name> Audio/take.wav"`. A song and its audio folder can be moved together.
- **Autosaves** are the same format, kept in the app's settings folder. They add `purrludeFile` on `EDIT`: the song they belong to.

## Purrlude's additions

### The song (`EDIT`)

| Attribute | Value |
|---|---|
| `purrludeFormat` | The format version |
| `purrludeKeySignature` | The song's key, as `"<fifths> major"` or `"<fifths> minor"` (fifths: -7 to 7; -1 is F major). Absent: estimated from the notes |
| `purrludeTitle`, `purrludeComposer` | The score's title and composer. Absent: the title is the song's file name, and there's no composer |

| Child | Contents |
|---|---|
| `PURRLUDE_MIDI_CONTROLS` | MIDI learn: a `CONTROL` per mapping, with `channel` (1-16), `number` (the Control Change number), `track` (an Edit item id, or 0 for the master), `plugin` (the plugin's Edit item id) and `parameter` (the parameter id) |

### The transport (`TRANSPORT`)

| Attribute | Value |
|---|---|
| `purrludeLoopStart`, `purrludeLoopEnd` | The loop and punch range, in quarter notes from the start of the song |
| `purrludePunch` | `1` when punch in/out is on |

Tracktion's own loop points (in seconds) follow these; Purrlude rewrites them.

### Tracks (`TRACK`, Tracktion's audio tracks)

| Attribute | Value |
|---|---|
| `purrludeTrackKind` | `"audio"` for an audio track, `"reverbBus"` for the shared reverb's bus; absent for an instrument track |
| `purrludeProgram` | The General MIDI program (0-127) |
| `purrludeSoundSource`, `purrludeSoundSourceName` | When the track plays a MIDI device's own sounds: the device's id and name |
| `purrludeOutputChannel` | The MIDI channel it plays on (1-16) |
| `purrludeOwnChannel` | `1` when the track has its own channel on a multi-part device |
| `purrludeDeviceTone`, `purrludeDeviceToneName` | The device's tone: `"<bank MSB> <bank LSB> <program>"`, and its name |
| `purrludePluginInstrument` | `1` when a third-party instrument plays (so a missing plugin isn't replaced by the built-in one) |
| `purrludeAccompaniment` | `"bass"` or `"chords"` on the tracks accompaniment parts go on |
| `purrludeAudioInput` | An audio track's input, by name |
| `purrludeAutomation` | The automation shown on the lane: `"<plugin id>/<parameter id>"` |

### Plugins

| Type | |
|---|---|
| `purrludeSoundFont` | The built-in General MIDI instrument; `program` is its sound |

A track playing a MIDI device's sounds has a `midipatchbay` at the front of its chain, remapping every channel to the device's.

Effects are Tracktion's plugins (`reverb`, `delay`, `4bandEq`, `compressor`), before the track's `volume` and `level` plugins. The shared reverb is an `auxsend` (bus 0) after the volume, and the bus track has the `auxreturn`. The master's effects are in `MASTERPLUGINS`.

### Clips

| Child | Contents |
|---|---|
| `PURRLUDE_TAKES` (audio clips) | The takes of a loop recording: a `TAKE` per pass with its `source` (as a clip's own), and `current`, the one that's heard (counting from 0). MIDI clips use Tracktion's own takes |

## Conventions

- **Beats:** Tracktion counts beats in the time signature's unit. Every time signature in a song has the same unit, so beats keep one length through it.
- **Undo:** the view settings (`purrludeAutomation`, the loop range, punch) are written without undo; everything else is undoable.
