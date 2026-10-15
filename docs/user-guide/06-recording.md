# 6. Recording

[← Arranging Clips](05-arrangement.md) · [Contents](README.md) · [Next: Piano Roll →](07-piano-roll.md)

**Record** (or **R**) records every armed track at once: what you play on a MIDI keyboard, and the input of each armed audio track. Press **R**, **Space** or **Stop Rec** to stop. Recording never stops by itself.

Before recording:
- Turn on **Count-in** for a bar of clicks before recording starts.
- Turn on **Metronome** to hear the beat while you play.

The take being recorded grows as a vermillion box in the lane.

## Recording MIDI

1. Connect a MIDI keyboard ([Devices](13-devices.md)). Every connected keyboard, Bluetooth included, plays the selected track; there's no per-keyboard routing to set up.
2. Click the instrument track you want to record on. It becomes the armed track (its **Arm** lights up and it gets a blue stripe).
3. Move the playhead to where you want to start, and press **R**.

The notes land in a new MIDI clip. Recording over an existing MIDI clip on the same track adds the new notes to it. Fix timing and wrong notes in the [Piano Roll](07-piano-roll.md); the [score](08-score.md) tidies timing for display without changing what you played.

## Recording audio

1. Choose your audio interface or microphone under **Audio Settings** ([Devices](13-devices.md#audio-settings)).
2. Add an audio track (**+ Track > Audio Track**).
3. In its header, choose an **input** from the input menu, e.g. *MacBook Pro Microphone - Input 1*, or a pair of channels on an interface. Each input can feed one track.
4. Click **Arm** on the track. Arming with no input chosen picks the first free one.
5. To hear yourself while recording, turn on **In**. Use headphones: through speakers, a microphone feeds back.
6. Press **R**.

**Where recordings go:** into the project's audio folder (see [Projects](02-projects.md#where-the-audio-goes)), as a new audio clip. A recording replaces whatever it covers on its track.

**Several tracks at once:** you can arm several audio tracks, each with its own input, along with the keyboard's instrument track. They all record together.

**After a device change:** each track remembers its input by name. If you unplug and replug an interface, tracks get their inputs back.

## Loop recording and takes

To record several attempts at the same passage, record with looping on:

1. Set the [loop range](03-transport.md#loop-playback) over the passage and turn **Loop** on.
2. Press **R**. Recording starts at the loop's start (after the count-in, if it's on) and goes round and round.
3. Each pass becomes a **take**. Stop when you've got one you like.

The clip shows **Take n/m**: which take is heard, out of how many. The last complete pass is heard. If you stop partway through a pass, a short last audio take is skipped in favour of the one before.

**Choosing a take:** right-click the clip and open **Takes**:
- **Take 1**, **Take 2**, ... chooses which take is heard. It's one undo step.
- **Delete Other Takes** keeps only the current one. The other audio files stay on disk.

While recording, the loop range can't be changed and **Loop** can't be turned off.
