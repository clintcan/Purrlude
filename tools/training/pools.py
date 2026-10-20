"""Recordings of one part each (a singer, a guitar, a drum kit), for training the separation
model from scratch: datasets.py downloads them and lists them in pools.json.

None of these datasets is a band's multitrack, so train.py makes mixes out of them: a segment
of each of a few parts from unrelated recordings, as Demucs's own training "remixes" songs, and
adds pooled vocals over Slakh songs (which have none). Each segment is placed in the stereo
field like a mixed part (Augment), since most of these recordings are mono close microphones.
"""

import json
import math
import random
from pathlib import Path

import julius
import numpy as np
import soundfile as sf
import torch
import torch.nn.functional as F

from slakh import PARTS


class Pools:
    """pools.json's recordings by part, each part's datasets picked by their weight."""

    def __init__(self, manifest, valid_fraction=0.03, seed=0):
        manifest = Path(manifest)
        data = manifest.parent
        self.train, self.valid = {}, {}   # part -> [(weight, [files])]

        for pool in json.loads(manifest.read_text())["pools"]:
            files = [data / f for f in pool["files"]]
            random.Random(seed).shuffle(files)
            held = max(1, int(len(files) * valid_fraction)) if len(files) > 1 else 0
            self.train.setdefault(pool["part"], []).append((pool["weight"], files[held:]))
            if held:
                self.valid.setdefault(pool["part"], []).append((pool["weight"], files[:held]))

        self.lengths = {}   # path -> (frames, sample rate), read once per process

    @property
    def parts(self):
        return [p for p in PARTS if p in self.train]

    def info(self, path):
        if path not in self.lengths:
            i = sf.info(str(path))
            self.lengths[path] = (i.frames, i.samplerate)
        return self.lengths[path]

    def pick(self, part, rng, valid=False):
        pools = (self.valid if valid else self.train).get(part)
        if not pools:
            return None
        weights = [w for w, files in pools if files]
        chosen = rng.choices([f for _, f in pools if f], weights=weights)[0]
        return rng.choice(chosen)

    def segment(self, part, frames, sample_rate, rng, valid=False, tries=8):
        """A random stretch of a random recording of the part, [2, frames] at sample_rate, that
        isn't silent; None if it found none."""
        for _ in range(tries):
            path = self.pick(part, rng, valid)
            if path is None:
                return None
            length, rate = self.info(path)
            source_frames = math.ceil(frames * rate / sample_rate) + 1
            start = rng.randrange(0, max(1, length - source_frames))
            audio, _ = sf.read(str(path), start=start, frames=source_frames, dtype="float32", always_2d=True)
            audio = torch.from_numpy(audio.T.copy())[:2]
            if audio.shape[0] == 1:
                audio = audio.repeat(2, 1)
            if rate != sample_rate:
                audio = julius.resample_frac(audio, rate, sample_rate)
            audio = F.pad(audio[..., :frames], (0, max(0, frames - audio.shape[-1])))
            peak = audio.abs().max()
            if peak > 1e-3:
                return audio / peak * 0.5   # recordings differ in level; train.py sets the gain
        return None


class Augment:
    """What a mixing engineer might have done to a part: placed it left or right, made it
    wider or narrower, turned it up or down, flipped its polarity."""

    def __init__(self, rng):
        self.rng = rng

    def __call__(self, audio, mono_source=None):
        rng = self.rng
        if mono_source is None:
            mono_source = torch.allclose(audio[0], audio[1])
        if mono_source:
            # A mono recording, panned (constant power); centred more often than not.
            pan = 0.0 if rng.random() < 0.4 else rng.uniform(-0.8, 0.8)
            angle = (pan + 1) * math.pi / 4
            audio = torch.stack([audio[0] * math.cos(angle), audio[0] * math.sin(angle)]) * math.sqrt(2)
        else:
            if rng.random() < 0.5:
                audio = audio.flip(0)
            width = rng.uniform(0.5, 1.2)
            mid, side = (audio[0] + audio[1]) / 2, (audio[0] - audio[1]) / 2 * width
            audio = torch.stack([mid + side, mid - side])
        if rng.random() < 0.5:
            audio = -audio
        return audio
