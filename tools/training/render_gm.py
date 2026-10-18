#!/usr/bin/env python3
"""Renders Slakh songs' MIDI through General MIDI sound sets, as Slakh-style songs, so the
separation model also learns how General MIDI instruments sound (Purrlude's own, and other
programs' renders).

    python tools/training/render_gm.py <slakh folder> <output folder> GeneralUser-GS.sf2 [more.sf2 ...]
        [--limit N] [--sample-rate 44100]

Each song is rendered with one of the sound sets, in turn. Each instrument (MIDI/Sxx.mid) is
rendered on its own with its General MIDI program from metadata.yaml (drums on channel 10), to
stems/Sxx.flac, and metadata.yaml is copied, so slakh.py reads the result like Slakh. Songs
already rendered are skipped, so it can be stopped and run again.

Needs FluidSynth (the program; LGPL, used only to render) and mido. Sound sets with licences that
allow this: GeneralUser GS (Purrlude's own), FluidR3_GM (MIT), MuseScore_General (MIT).
"""

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import mido
import numpy as np
import soundfile as sf
import yaml


def instrument_midi(source, program, drums):
    """The instrument's MIDI on one channel with its program (or the drum channel), without the
    file's own program changes."""
    midi = mido.MidiFile(source)
    channel = 9 if drums else 0

    for track in midi.tracks:
        for i, msg in enumerate(track):
            if hasattr(msg, "channel"):
                track[i] = msg.copy(channel=channel)
                if msg.type == "program_change":
                    track[i] = msg.copy(channel=channel, program=program if not drums else 0)

    if not drums:
        midi.tracks[0].insert(0, mido.Message("program_change", channel=0, program=program, time=0))
    return midi


def render_song(song, out, soundfont, sample_rate, work):
    meta = yaml.safe_load(open(song / "metadata.yaml"))
    (out / "stems").mkdir(parents=True, exist_ok=True)
    rendered = {}

    for name, stem in sorted(meta["stems"].items()):
        source = song / "MIDI" / (name + ".mid")
        if not source.exists():
            continue

        midi_file = work / (name + ".mid")
        wav_file = work / (name + ".wav")
        instrument_midi(source, int(stem.get("program_num", 0)) % 128, bool(stem.get("is_drum"))).save(midi_file)

        # No reverb or chorus: the part's own sound, as in Slakh's stems.
        subprocess.run(["fluidsynth", "-ni", "-q", "-R", "0", "-C", "0", "-g", "0.5", "-r", str(sample_rate),
                        "-F", str(wav_file), str(soundfont), str(midi_file)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        audio, _ = sf.read(str(wav_file), dtype="float32", always_2d=True)
        rendered[name] = audio

    if not rendered:
        return False

    # Every stem as long as the longest, so they line up from the start.
    length = max(a.shape[0] for a in rendered.values())

    for name, audio in rendered.items():
        audio = np.pad(audio, ((0, length - audio.shape[0]), (0, 0)))
        sf.write(str(out / "stems" / (name + ".flac")), audio, sample_rate, subtype="PCM_16")

    meta["soundfont"] = Path(soundfont).name
    with open(out / "metadata.yaml", "w") as f:
        yaml.safe_dump(meta, f)

    shutil.copytree(song / "MIDI", out / "MIDI", dirs_exist_ok=True)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("slakh", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("soundfonts", type=Path, nargs="+")
    parser.add_argument("--limit", type=int, default=0, help="render at most this many songs")
    parser.add_argument("--sample-rate", type=int, default=44100)
    args = parser.parse_args()

    if shutil.which("fluidsynth") is None:
        sys.exit("Needs FluidSynth (apt install fluidsynth, brew install fluid-synth)")

    songs = sorted(m.parent for m in args.slakh.rglob("metadata.yaml") if (m.parent / "MIDI").is_dir())
    if args.limit:
        songs = songs[:args.limit]

    with tempfile.TemporaryDirectory() as work:
        for i, song in enumerate(songs):
            soundfont = args.soundfonts[i % len(args.soundfonts)]
            out = args.output / (song.name + "-" + soundfont.stem)

            if (out / "metadata.yaml").exists():
                continue

            ok = render_song(song, out, soundfont, args.sample_rate, Path(work))
            print(f"{i + 1}/{len(songs)} {out.name}" + ("" if ok else " (no MIDI)"), flush=True)


if __name__ == "__main__":
    main()
