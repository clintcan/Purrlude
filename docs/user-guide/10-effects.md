# 10. Effects and Reverb

[← Loops and Accompaniment](09-loops-and-accompaniment.md) · [Contents](README.md) · [Next: Audio to MIDI →](11-audio-to-midi.md)

<img src="images/effects.png" alt="The Effects on Melody panel: the shared reverb send at −12 dB, a Delay set to dotted 1/8, and an EQ" width="580">

Click a track's **FX** button to open its effects. The panel shows:
- a send to the song's **shared reverb**
- the track's own effects, in the order the sound passes through them

Click anywhere outside the panel to close it. **FX** is lit green when the track has effects or a reverb send.

Effects work on Purrlude's built-in sounds and audio. When a track plays an external keyboard's sounds, its **FX** button is disabled.

## Adding effects

Click **Add Effect** and choose one of the four below. Each starts with useful settings.

| Effect | What it's for | Controls |
|---|---|---|
| **Reverb** | Space and room sound | Size, Damping, Amount |
| **Delay** | Echoes in time with the song | Time (1/4, dotted 1/8, 1/8, 1/16), Feedback, Mix |
| **EQ** | Shaping the tone: four bands at 150 Hz, 500 Hz, 2.5 kHz and 8 kHz | Low, Low mid, High mid, High (dB) |
| **Compressor** | Evening out the level | Threshold, Ratio, Makeup |

**Each effect's row:**
- **On** / **Off** switches the effect without losing its settings.
- **×** removes it.
- **Knobs:** drag up and down or round. Double-click one to reset it.

**Effect details:**
- The delay's time is a note value, so the echoes stay in time if you change the tempo.
- Effects come before the track's volume fader, so the fader and meter show the final level.

Adding, removing and switching an effect are each one undo step, and so is each knob drag.

## The shared reverb

A song has one shared reverb that every track can send to. Tracks sent to it sound like they're in the same room. It's usually better than a separate reverb on each track.

- **Send:** turn the **Send** knob in a track's FX panel up from *Off*. Double-click it to set it back to Off.
- **Volume:** the send comes after the track's volume, so turning the track down turns its reverb down too.
- **Solo:** a soloed track keeps its reverb.

<img src="images/reverb-settings.png" alt="The Shared reverb panel with Size, Damping and Amount" width="580">

**Reverb Settings...** opens the shared reverb itself. Size, Damping and Amount apply to every track's reverb. You can add effects there too; they're applied to the reverb sound only (for example an EQ to darken it).

When exporting stems, each stem includes its own share of the reverb, so the stems still add up to the mix.
