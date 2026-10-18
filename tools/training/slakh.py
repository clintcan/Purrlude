"""Reading Slakh-style songs for fine-tuning Purrlude's separation model.

A song is a folder with metadata.yaml (its instruments: inst_class, is_drum, program_num) and
stems/Sxx.wav or .flac (each instrument's audio). Slakh2100 (CC BY 4.0) is laid out this way, and
render_gm.py writes General MIDI renders the same way, so one loader reads both.

Each instrument goes to one of Demucs's six parts (PARTS) by its class. Slakh has no vocals, so
"vocals" is always silent here (train.py leaves it out of the loss).
"""

import random
from pathlib import Path

import numpy as np
import soundfile as sf
import yaml

PARTS = ["drums", "bass", "other", "vocals", "guitar", "piano"]   # htdemucs_6s's order

# Slakh's instrument classes that have a part of their own; everything else is "other".
CLASS_PART = {"Bass": "bass", "Guitar": "guitar", "Piano": "piano"}


def part_of(stem):
    """The Demucs part an instrument (a metadata.yaml stem entry) belongs to."""
    if stem.get("is_drum"):
        return "drums"
    return CLASS_PART.get(stem.get("inst_class"), "other")


def stem_file(song, name):
    for suffix in (".flac", ".wav"):
        path = song / "stems" / (name + suffix)
        if path.exists():
            return path
    return None


class Song:
    """One song's instruments, by part, with its length and sample rate."""

    def __init__(self, folder):
        self.folder = Path(folder)
        meta = yaml.safe_load(open(self.folder / "metadata.yaml"))
        self.stems = []   # (file, part)

        # BabySlakh's metadata says nothing was rendered, so this goes by which files exist.
        for name, stem in sorted(meta["stems"].items()):
            path = stem_file(self.folder, name)
            if path is not None:
                self.stems.append((path, part_of(stem)))

        info = [sf.info(str(path)) for path, _ in self.stems]
        self.sample_rate = info[0].samplerate if info else 44100
        self.frames = min(i.frames for i in info) if info else 0

    def read(self, start, frames):
        """The parts from a frame, as an array (6 parts, 2 channels, frames) of float32.
        Mono stems go to both channels."""
        parts = np.zeros((len(PARTS), 2, frames), dtype=np.float32)

        for path, part in self.stems:
            audio, _ = sf.read(str(path), start=start, frames=frames, dtype="float32", always_2d=True)
            audio = audio.T
            if audio.shape[0] == 1:
                audio = np.repeat(audio, 2, axis=0)
            parts[PARTS.index(part), :, :audio.shape[1]] += audio[:2]

        return parts


def find_songs(roots):
    """Every song folder (one with metadata.yaml and stems/) under the roots."""
    songs = []
    for root in roots:
        for meta in sorted(Path(root).rglob("metadata.yaml")):
            if (meta.parent / "stems").is_dir():
                songs.append(meta.parent)
    return songs


def split(songs, valid_fraction, seed=0):
    """The songs, shuffled reproducibly, split into training and validation."""
    songs = list(songs)
    random.Random(seed).shuffle(songs)
    n = max(1, int(len(songs) * valid_fraction)) if len(songs) > 1 else 0
    return songs[n:], songs[:n]
