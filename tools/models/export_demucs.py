#!/usr/bin/env python3
"""Exports Demucs v4's six-instrument model ("htdemucs_6s") to ONNX for Purrlude.

Demucs's code is MIT, but its pretrained weights aren't: Meta provides them only for
scientific purposes (github.com/facebookresearch/demucs/issues/327), and so does any model
made from them.

The network is exported without its spectrogram steps, which ONNX can't express; Purrlude
does them itself (src/analysis/SourceSeparator.cpp). This uses sevagh/demucs.onnx's copy of
Demucs that has them moved out of the network.

Run it in a virtual environment with PyTorch 2.5.1 (other versions may export a different
graph):

    python3 -m venv venv
    venv/bin/pip install torch==2.5.1 torchaudio==2.5.1 "numpy<2.3" einops julius pyyaml tqdm \
                         onnx dora-search openunmix diffq
    venv/bin/python tools/models/export_demucs.py .cpm-cache/demucs/htdemucs_6s.onnx

With --weights checkpoints/weights.th it exports fine-tuned weights instead (from
tools/training/train.py, a state dict of the same model). Purrlude's model is the models-2
release's htdemucs_6s-purrlude-1-weights.th exported this way (htdemucs_6s-purrlude-1.onnx,
SHA-256 96dda58d164ee5233df6d69a39fd2dadf1f0a64cad6033b7229586646476231b with PyTorch 2.5.1
on macOS arm64).

It downloads demucs.onnx at a pinned commit and Demucs's pretrained weights (checked by
SHA-256), then writes the model (115 MB). The export is reproducible: with PyTorch 2.5.1 on
macOS arm64 it came out with SHA-256
7924dfb5d8e940cfb16100b634dfafb370e8036a71f968ec9193f4b482ed5b71.
"""

import hashlib
import os
import subprocess
import sys
import tempfile
from pathlib import Path

DEMUCS_ONNX_REPO = "https://github.com/sevagh/demucs.onnx.git"
DEMUCS_ONNX_COMMIT = "81fa192e6fcc88e35e887f6e6ccce91227f4e6f5"
WEIGHTS = "5c90dfd2-34c22ccb.th"
WEIGHTS_SHA256 = "34c22ccb381c6f9fdbf324f04e1e2fe21aaaf293f5ded163a162697ff9a02ddd"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def main():
    args = sys.argv[1:]
    weights_file = None

    if len(args) == 3 and args[0] == "--weights":
        weights_file = Path(args[1]).resolve()
        args = args[2:]

    if len(args) != 1:
        sys.exit(__doc__)

    destination = Path(args[0]).resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as work:
        work = Path(work)
        source = work / "demucs.onnx"
        subprocess.run(["git", "clone", "--quiet", DEMUCS_ONNX_REPO, str(source)], check=True)
        subprocess.run(["git", "-C", str(source), "checkout", "--quiet", DEMUCS_ONNX_COMMIT], check=True)
        sys.path.insert(0, str(source / "demucs-for-onnx"))
        os.environ["TORCH_HOME"] = str(work / "torch")

        import torch
        from torch.nn import functional as F
        from demucs.pretrained import get_model
        from demucs.htdemucs import HTDemucs, standalone_magnitude, standalone_spec

        if torch.__version__.split("+")[0] != "2.5.1":
            print(f"Warning: PyTorch {torch.__version__}, not 2.5.1; the export may differ.")

        bag = get_model("htdemucs_6s")
        weights = work / "torch" / "hub" / "checkpoints" / WEIGHTS

        if sha256(weights) != WEIGHTS_SHA256:
            sys.exit(f"{WEIGHTS} doesn't match its SHA-256")

        model = bag.models[0] if hasattr(bag, "models") else bag
        assert isinstance(model, HTDemucs)

        if weights_file is not None:
            # Fine-tuned weights: the same network's state dict (upstream Demucs's names).
            model.load_state_dict(torch.load(weights_file, map_location="cpu"))
            print(f"Using the fine-tuned weights in {weights_file} (SHA-256 {sha256(weights_file)})")

        assert model.sources == ["drums", "bass", "other", "vocals", "guitar", "piano"]

        # As sevagh/demucs.onnx's scripts/convert-pth-to-onnx.py: a segment's audio and its
        # spectrogram in, the stems' spectrograms and audio out.
        length = int(model.segment * model.samplerate)
        audio = F.pad(torch.randn(1, 2, 343980), (0, length - 343980))
        spectrum = standalone_magnitude(standalone_spec(audio))
        torch.onnx.export(model, (audio, spectrum), str(destination), export_params=True,
                          opset_version=17, do_constant_folding=True,
                          input_names=["input"], output_names=["output"])

    print(f"Wrote {destination} ({destination.stat().st_size / 1e6:.0f} MB), SHA-256 {sha256(destination)}")


if __name__ == "__main__":
    main()
