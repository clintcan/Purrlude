#!/usr/bin/env python3
"""Scores separation models on Slakh-style songs with Purrlude's own pipeline ([.separate-score]),
one song at a time, and compares them: right-track F (each note's pitch and start) and F with
note ends too, cleaned up as the app does, averaged over the songs, with how many songs each
model beats the first one on.

    python tools/training/evaluate.py build/tests/PurrludeTests_artefacts/Debug/PurrludeTests \\
        <songs folder> <logs folder> original=htdemucs_6s.onnx tuned=finetuned.onnx

Each song is a folder with its mix (mix.flac or mix.wav) and reference.mid (make_reference.py).
Use songs the model wasn't trained on: Slakh's test songs. A song already scored (its log has a
result) is not scored again, so it can be stopped and run again.
"""

import os
import re
import subprocess
import sys
from pathlib import Path


def score(test, mix, reference, model, log):
    if not (log.exists() and "All tests passed" in log.read_text(errors="replace")):
        env = dict(os.environ, PURRLUDE_SWEEP="0", PURRLUDE_AUDIO_FILE=str(mix),
                   PURRLUDE_MIDI_FILE=str(reference), PURRLUDE_SEPARATION_MODEL=str(model))
        with open(log, "w") as out:
            subprocess.run([str(test), "[.separate-score]"], env=env, stdout=out, stderr=subprocess.STDOUT)

    text = log.read_text(errors="replace")
    m = re.search(r"before [\d.]+, defaults ([\d.]+)\nwith ends: before [\d.]+, defaults ([\d.]+)", text)
    return (float(m.group(1)), float(m.group(2))) if m and "All tests passed" in text else None


def main():
    if len(sys.argv) < 5:
        sys.exit(__doc__)

    test, songs, logs = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    models = [(arg.split("=", 1)[0], Path(arg.split("=", 1)[1]).resolve()) for arg in sys.argv[4:]]
    logs.mkdir(parents=True, exist_ok=True)

    folders = sorted(f for f in songs.iterdir() if (f / "reference.mid").exists())
    results = {name: {} for name, _ in models}

    for folder in folders:
        mix = next((folder / n for n in ("mix.flac", "mix.wav") if (folder / n).exists()), None)
        if mix is None:
            continue
        for name, model in models:
            r = score(test, mix, folder / "reference.mid", model, logs / f"{folder.name}-{name}.log")
            if r:
                results[name][folder.name] = r
        print(folder.name, "  ".join(f"{name} {results[name].get(folder.name, ('-', '-'))}" for name, _ in models), flush=True)

    # Only songs every model was scored on.
    common = sorted(set.intersection(*(set(r) for r in results.values())))
    if not common:
        sys.exit("No song was scored with every model")

    first = models[0][0]
    mean = lambda name, i: sum(results[name][s][i] for s in common) / len(common)
    print(f"\n{len(common)} songs           right track   with ends   songs better than {first} (F / ends)")
    for name, _ in models:
        better = sum(results[name][s][0] > results[first][s][0] for s in common)
        better_ends = sum(results[name][s][1] > results[first][s][1] for s in common)
        print(f"  {name:16s} {mean(name, 0):.3f}         {mean(name, 1):.3f}       " +
              ("" if name == first else f"{better} / {better_ends}"))


if __name__ == "__main__":
    main()
