# 11. Audio to MIDI

[← Effects, Reverb and Plugins](10-effects.md) · [Contents](README.md) · [Next: Importing and Exporting →](12-import-export.md)

Purrlude can listen to an audio clip and write down its notes as MIDI. You can then edit them in the Piano Roll and see them as sheet music. It can also split a recorded song into its instruments.

Everything runs on your computer. No audio is sent anywhere.

<img src="images/clip-menu-audio.png" alt="An audio clip's right-click menu: Split at Playhead, Split Here, Cut, Copy, Duplicate, Convert Melody to MIDI..., Convert Notes and Chords to MIDI..., Convert Drums to MIDI..., Separate Instruments to MIDI..., Separate Instruments to Audio Tracks..., Split Lead and Backing Vocals..., Loop This Clip, Delete" width="766">

Right-click an audio clip and choose a command. A progress window shows while it works, and **Cancel** (or Esc) stops it. Only one conversion runs at a time.

## Converting to MIDI

| Command | For | Result |
|---|---|---|
| **Convert Melody to MIDI...** | One note at a time: singing, humming, whistling, a solo instrument | A piano track |
| **Convert Notes and Chords to MIDI...** | Piano, guitar, or several instruments playing together | A piano track |
| **Convert Drums to MIDI...** | A drum recording or loop | A drum-kit track with kick, snare and hi-hat |

<img src="images/converting.png" alt="The progress window: Converting Hummed Idea to MIDI, Listening for the melody..., with Cancel" width="350" align="right">

**The result:**
- The notes go on a new track under the audio track, named after the clip with *(notes)*, or *(drums)* for drums.
- They line up with the clip's place in the song.
- The whole conversion is one undo step.

**Change the sound:** use the new track's instrument menu, e.g. to Flute for a hummed melody or Acoustic Bass for a bass line.

<br clear="right">

<img src="images/converted.png" alt="The Hummed Idea audio clip, and below it the Hummed Idea (notes) track with the melody converted to MIDI" width="100%">

### Tips for good results

- **Melody** follows slides and vibrato as one note, and starts a new note when the pitch settles somewhere new or you re-attack. Very short blips (under 60 ms) are left out. A clean, dry recording works best.
- **Notes and Chords** uses Spotify's basic-pitch model. Occasionally it adds a note an octave above a note with a strong overtone.
- **Drums** finds kicks, snares and closed hi-hats. Toms and cymbals come out as whichever of the three they sound most like. Soft ghost notes may be missed.
- **Expect to tidy up.** No converter is perfect. Use the [Piano Roll](07-piano-roll.md) to fix wrong or missing notes.

**Availability:** Notes and Chords and instrument separation need ONNX Runtime, which is included for Windows, Linux and Apple Silicon Macs. On Intel Macs those commands are greyed out.

## Separating instruments

**Separate Instruments to MIDI...** and **Separate Instruments to Audio Tracks...** split a mixed recording into up to six parts:
- drums
- bass
- vocals
- guitar
- piano
- everything else (strings, synths, horns, ...)

It uses Meta's Demucs model, fine-tuned for Purrlude to tell piano, guitar, bass and the rest apart better.

- **To Audio Tracks** puts each part on its own audio track under the clip, playing in the same place. Use them to mute, solo or remix parts, or to practise along with one taken out.
- **To MIDI** also converts each part to notes the way that suits it:
  - drums with the drum converter
  - vocals with the melody converter
  - the rest with notes and chords, with the bass kept to its range
  
  Each part goes on its own instrument track with a matching sound.

**What you get:**
- Parts that are almost silent (more than 30 dB quieter than the song) are left out.
- New tracks are named after the clip and the part, e.g. *Song Drums*.
- The whole separation is one undo step.

**Speed:** on a Mac, separating runs on the graphics chip and takes about an eighth of the song's length, e.g. a 3-minute song takes about 25 seconds. The first time is slower (about a quarter of the song's length), while the Mac prepares the model for its graphics chip; it keeps the result (about 430 MB) in `~/Library/Caches/Purrlude` so later separations start at once. If that folder is cleared, the next separation prepares it again. On Windows it runs on the graphics card (any DirectX 12 one, built-in graphics included) when it can: a 2-minute song took about 45 seconds on a laptop with AMD Radeon graphics, and about as long as the song itself without it.

**Limits:**
- Drums usually separate cleanly.
- Pitched instruments can still end up in the wrong part now and then, for example strings in "piano" or a bass in "guitar".
- Separation only knows these six parts.

**The model** is a 115 MB file (`htdemucs_6s-purrlude-1.onnx`) that isn't included with Purrlude. The first time you separate instruments, Purrlude asks whether to download it from its GitHub page. Nothing else is sent or received, and your audio stays on your computer. If the download is cancelled or interrupted, the next one carries on from where it stopped, and the file is checked before it's used. If you had the earlier model (`htdemucs_6s.onnx`), Purrlude offers the new one the next time you separate, and deletes the old file once it's downloaded.

**Its licence is different from Purrlude's.** The model is built from Meta's trained Demucs model, which Meta provides only for scientific purposes; it isn't open-source like Purrlude. Download and use it only if that fits what you're doing. Everything else in Purrlude works without it.

It's kept in your models folder, where you can also put a copy yourself:
- macOS: `~/Library/Application Support/Purrlude/Models/`
- Windows: `%APPDATA%\Purrlude\Models\`
- Linux: `~/.config/Purrlude/Models/`

Developers can make it with `tools/models/export_demucs.py`.

## Splitting lead and backing vocals

**Split Lead and Backing Vocals...** in an audio clip's menu splits the vocals in two: the lead vocal and the backing vocals. Use it on the **whole song**: the lead vocal model was trained on songs, and finds far more of the lead there than in a vocals part on its own. It works on a vocals part too (from **Separate Instruments to Audio Tracks**), but leaves more of the lead in the backing vocals.

- **The lead vocal model:** the first time, Purrlude offers to download a model that listens for the lead vocal: a karaoke model by anvuew, about 213 MB, licensed GPL-3.0, into the same models folder as the separation model (above). Developers can make it with `tools/models/export_lead_vocals.py`. Your audio stays on your computer.
  - **How it tells them apart:** the model finds the lead singer in the song. The backing vocals are the rest of the song's vocals, which the separation model finds; so on a whole song, install that model too (**Separate Instruments** offers it). Without it, the backing vocals track is everything but the lead, instruments included.
  - **Speed:** on a fast GPU, about the clip's length or less; on a laptop's built-in graphics or the CPU, several times the clip's length (a 4-minute song took over half an hour on one).
  - **Built-in graphics (Windows):** they also draw the screen, so Purrlude asks first whether to use them or the CPU. With the graphics, the screen may stutter and the graphics driver may report a timeout, but the split carries on. The CPU keeps the screen smooth and takes about twice as long.
  - **Limits:** some of the lead (often its reverb) is left in the backing vocals, sounding thin, and backing vocals sung with the lead can be heard faintly in it.
- **Without the model** (choose **Split Roughly**, stereo clips only): Purrlude splits by where the voices sit and by the lead's pitch. The lead is usually in the middle, and backing vocals are often spread to the sides. Of what's in the middle, the strongest voice goes to *Lead Vocals*, and harmonies sung in the middle with it go to *Backing Vocals*. A doubled lead and harmonies where the lead rests stay with the lead, and the lead's reverb, if it's wide, goes with the backing vocals. It takes a few seconds.
- **What you get:** two audio tracks under the clip's track, playing in the same place, one undo step. Split from a vocals part (or without the separation model), together they sound exactly like the clip, so mute the clip's own track to use them.
