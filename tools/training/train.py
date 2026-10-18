#!/usr/bin/env python3
"""Fine-tunes Demucs's six-instrument model (htdemucs_6s, MIT) on Slakh-style songs (slakh.py):
Slakh2100 and General MIDI renders of it (render_gm.py).

    python tools/training/train.py <checkpoint folder> <song folder> [more ...]
        [--steps 20000] [--batch 4] [--lr 3e-5] [--workers 8] [--valid 0.05]

Each step mixes random 7.8 s segments (the model's segment): the song's instruments grouped into
the model's six parts, each at a random gain, and with --remix a part from another song now and
then (as Demucs's own training does). The loss is the L1 distance of each estimated part from the
real one. Slakh has no vocals, so the vocals part is left out of the loss: training the model to
always answer silence there would make it forget how to separate singing.

It saves checkpoint.pt every --save-every steps and carries on from it when run again with the
same folder; weights.th (the model's state dict) is what tools/models/export_demucs.py --weights
exports. On CUDA it trains in bfloat16 (autocast); on Apple GPUs (MPS) and CPUs in float32.
Validation reports each part's SDR (dB, higher is better) on fixed segments of held-out songs.
"""

import argparse
import math
import random
import time
from pathlib import Path

import numpy as np
import torch
import julius
import torch.nn.functional as F

from slakh import PARTS, Song, find_songs, split

SAMPLE_RATE = 44100


class Segments(torch.utils.data.IterableDataset):
    """Endless random training examples: (parts [6, 2, T], mixture [2, T])."""

    def __init__(self, songs, frames, remix, seed):
        self.songs, self.frames, self.remix, self.seed = songs, frames, remix, seed

    def read(self, song, rng):
        # A segment at the model's rate (resampled if the song isn't at 44.1 kHz).
        source_frames = math.ceil(self.frames * song.sample_rate / SAMPLE_RATE) + 1
        start = rng.randrange(0, max(1, song.frames - source_frames))
        parts = torch.from_numpy(song.read(start, source_frames))

        if song.sample_rate != SAMPLE_RATE:
            parts = julius.resample_frac(parts, song.sample_rate, SAMPLE_RATE)

        parts = parts[..., :self.frames]
        return F.pad(parts, (0, self.frames - parts.shape[-1]))

    def __iter__(self):
        info = torch.utils.data.get_worker_info()
        rng = random.Random(self.seed + (info.id if info else 0) + int(time.time()))
        songs = [Song(s) for s in self.songs]
        songs = [s for s in songs if s.frames > 0]

        while True:
            song = rng.choice(songs)
            parts = self.read(song, rng)

            # Now and then a part from another song instead (Demucs's "remix").
            if self.remix > 0:
                for p in range(len(PARTS)):
                    if rng.random() < self.remix:
                        parts[p] = self.read(rng.choice(songs), rng)[p]

            gains = torch.tensor([rng.uniform(0.25, 1.25) for _ in PARTS]).view(-1, 1, 1)
            parts = parts * gains
            mixture = parts.sum(0)

            if mixture.abs().max() < 1e-3:   # silence teaches nothing
                continue

            yield parts, mixture


def fixed_validation(songs, frames, count):
    """The same segments every time: the middle of each held-out song, at most count of them."""
    examples = []
    for folder in songs[:count]:
        song = Song(folder)
        if song.frames == 0:
            continue
        source_frames = math.ceil(frames * song.sample_rate / SAMPLE_RATE) + 1
        parts = torch.from_numpy(song.read(max(0, song.frames // 2 - source_frames // 2), source_frames))
        if song.sample_rate != SAMPLE_RATE:
            parts = julius.resample_frac(parts, song.sample_rate, SAMPLE_RATE)
        parts = F.pad(parts[..., :frames], (0, max(0, frames - parts.shape[-1])))
        examples.append((parts, parts.sum(0)))
    return examples


def sdr(estimate, reference):
    """Signal-to-distortion ratio in dB (as in Demucs's evaluation, without its averaging)."""
    num = (reference ** 2).sum()
    den = ((reference - estimate) ** 2).sum()
    return 10 * math.log10((num + 1e-8) / (den + 1e-8))


def validate(model, examples, device, autocast):
    model.eval()
    scores = {p: [] for p in PARTS}
    with torch.no_grad():
        for parts, mixture in examples:
            with autocast():
                estimate = model(mixture.unsqueeze(0).to(device)).float().cpu()[0]
            for i, p in enumerate(PARTS):
                if parts[i].abs().max() > 1e-3:   # only parts that play
                    scores[p].append(sdr(estimate[i], parts[i]))
    model.train()
    return {p: sum(v) / len(v) for p, v in scores.items() if v}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("checkpoints", type=Path)
    parser.add_argument("songs", type=Path, nargs="+", help="folders with Slakh-style songs")
    parser.add_argument("--steps", type=int, default=20000)
    parser.add_argument("--batch", type=int, default=4)
    parser.add_argument("--lr", type=float, default=3e-5)
    parser.add_argument("--remix", type=float, default=0.1, help="chance of a part from another song")
    parser.add_argument("--workers", type=int, default=4, help="processes reading audio")
    parser.add_argument("--valid", type=float, default=0.05, help="share of songs held out")
    parser.add_argument("--valid-count", type=int, default=16, help="held-out songs scored")
    parser.add_argument("--valid-every", type=int, default=500)
    parser.add_argument("--save-every", type=int, default=250)
    parser.add_argument("--device", default="auto", help="cuda, mps or cpu")
    args = parser.parse_args()

    device = args.device if args.device != "auto" else \
        "cuda" if torch.cuda.is_available() else "mps" if torch.backends.mps.is_available() else "cpu"

    from demucs.pretrained import get_model
    model = get_model("htdemucs_6s").models[0].to(device)
    assert model.sources == PARTS
    frames = int(model.segment * model.samplerate)

    songs = find_songs(args.songs)
    train_songs, valid_songs = split(songs, args.valid)
    print(f"{len(songs)} songs: {len(train_songs)} to train on, {len(valid_songs)} held out; device {device}", flush=True)

    optimizer = torch.optim.Adam(model.parameters(), lr=args.lr)
    args.checkpoints.mkdir(parents=True, exist_ok=True)
    checkpoint = args.checkpoints / "checkpoint.pt"
    step = 0

    if checkpoint.exists():
        state = torch.load(checkpoint, map_location=device)
        model.load_state_dict(state["model"])
        optimizer.load_state_dict(state["optimizer"])
        step = state["step"]
        print(f"Carrying on from step {step}", flush=True)

    def autocast():
        if device == "cuda":
            return torch.autocast("cuda", dtype=torch.bfloat16)
        return torch.autocast("cpu", enabled=False)

    # Vocals aren't in the data: leave them out of the loss (see above).
    weights = torch.tensor([0.0 if p == "vocals" else 1.0 for p in PARTS], device=device).view(1, -1, 1, 1)

    loader = torch.utils.data.DataLoader(Segments(train_songs, frames, args.remix, seed=step),
                                         batch_size=args.batch, num_workers=args.workers,
                                         persistent_workers=args.workers > 0)
    examples = fixed_validation(valid_songs, frames, args.valid_count)

    def save():
        state = {"model": model.state_dict(), "optimizer": optimizer.state_dict(), "step": step, "args": vars(args)}
        torch.save(state, str(checkpoint) + ".tmp")
        Path(str(checkpoint) + ".tmp").replace(checkpoint)
        torch.save(model.state_dict(), args.checkpoints / "weights.th")

    if step == 0 and examples:
        print("before:", "  ".join(f"{p} {v:.2f}" for p, v in validate(model, examples, device, autocast).items()), flush=True)

    model.train()
    started, losses = time.time(), []

    for parts, mixture in loader:
        if step >= args.steps:
            break

        parts, mixture = parts.to(device), mixture.to(device)

        with autocast():
            estimate = model(mixture)

        loss = (F.l1_loss(estimate.float(), parts, reduction="none") * weights).mean() * len(PARTS) / weights.sum()
        optimizer.zero_grad(set_to_none=True)
        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), 5.0)
        optimizer.step()

        step += 1
        losses.append(loss.item())

        if step % 25 == 0:
            rate = 25 / (time.time() - started)
            print(f"step {step}  loss {sum(losses) / len(losses):.4f}  {rate:.2f} steps/s", flush=True)
            started, losses = time.time(), []

        if step % args.save_every == 0 or step == args.steps:
            save()

        if examples and (step % args.valid_every == 0 or step == args.steps):
            print(f"step {step} valid:", "  ".join(f"{p} {v:.2f}" for p, v in validate(model, examples, device, autocast).items()), flush=True)

    save()
    print(f"Done at step {step}: {args.checkpoints / 'weights.th'}", flush=True)


if __name__ == "__main__":
    main()
