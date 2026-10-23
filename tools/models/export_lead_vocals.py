#!/usr/bin/env python3
"""Exports anvuew's karaoke BS-RoFormer to ONNX, for Purrlude's Split Lead and Backing Vocals.

The model (huggingface.co/anvuew/karaoke_bs_roformer) finds the lead vocal in a song or a vocals
part; what's left is the backing vocals (and the instrumental, if there was any). Its weights are
licensed GPL-3.0 by their author, which Purrlude (AGPLv3) can use and redistribute; the model card
says it was trained on a "dataset by becruily", which isn't published.

The network is exported without its spectrogram steps, which ONNX can't express; Purrlude does
them itself (src/analysis/LeadVocalSeparator.cpp). The ONNX model takes one chunk's spectrogram,
both channels (torch.stft with n_fft 2048, hop 512, a periodic Hann window, centred), as
[1, 2, 1025, 1251, 2] (batch, channel, bin, frame, real/imaginary), and returns the lead's
spectrogram in the same layout, its DC bins zeroed as the model does. 1251 frames are the
640,000 samples (14.5 s) the model was trained on.

It uses ZFTurbo's Music-Source-Separation-Training (MIT) at a pinned commit for the network's
code, and the checkpoint at a pinned revision (both checked by SHA-256). Run it in a virtual
environment:

    python3 -m venv venv
    venv/bin/pip install torch numpy einops beartype rotary-embedding-torch pyyaml packaging \
                         librosa onnx onnxscript onnxruntime
    venv/bin/python tools/models/export_lead_vocals.py lead-vocals-anvuew-1.onnx [download folder]

The checkpoint is kept in the download folder, if one is given, for the next run.

It checks the exported model against PyTorch on random audio before writing it.
"""

import hashlib
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

MSST_REPO = "https://github.com/ZFTurbo/Music-Source-Separation-Training.git"
MSST_COMMIT = "6fdad13686bb7e84d029ec66f9dbe76e65c93ebf"
MODEL_REVISION = "0d4423d42e12cf2ba39ae09171028507b8a2a7be"
MODEL_URL = "https://huggingface.co/anvuew/karaoke_bs_roformer/resolve/" + MODEL_REVISION + "/"
CHECKPOINT = ("karaoke_bs_roformer_anvuew.ckpt", "206d04757cb5f75ca3b55f8a0a48f5c26aa2351d4ff3c7adbfc9affa30ea3ae4")
CONFIG = ("karaoke_bs_roformer_anvuew.yaml", "5cb3f127ecbc6a8e37f31ea7e05f60f360a44da43e857bde805b7b68558f6338")
FRAMES = 1251


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def fetch(folder, name, expected):
    path = folder / name

    if not path.exists() or sha256(path) != expected:
        print("Downloading", name)
        urllib.request.urlretrieve(MODEL_URL + name, path)

    if sha256(path) != expected:
        sys.exit(name + " doesn't have the expected SHA-256")

    return path


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)

    destination = Path(sys.argv[1]).resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    downloads = Path(sys.argv[2]).resolve() if len(sys.argv) == 3 else None

    with tempfile.TemporaryDirectory() as work:
        work = Path(work)
        source = work / "msst"
        subprocess.run(["git", "clone", "--quiet", MSST_REPO, str(source)], check=True)
        subprocess.run(["git", "-C", str(source), "checkout", "--quiet", MSST_COMMIT], check=True)
        sys.path.insert(0, str(source))

        if downloads is not None:
            downloads.mkdir(parents=True, exist_ok=True)

        checkpoint = fetch(downloads or work, *CHECKPOINT)
        config_file = fetch(downloads or work, *CONFIG)

        import numpy as np
        import torch
        import yaml
        from einops import rearrange
        from models.bs_roformer import attend
        from models.bs_roformer.bs_roformer import BSRoformer

        config = yaml.load(open(config_file), Loader=yaml.FullLoader)
        options = dict(config["model"])
        options["use_torch_checkpoint"] = False
        options["flash_attn"] = False   # plain attention exports to plain ONNX ops
        model = BSRoformer(**options)
        model.load_state_dict(torch.load(checkpoint, map_location="cpu", weights_only=True))
        model.eval()

        class Network(torch.nn.Module):
            """BSRoformer.forward between its STFT and its inverse STFT."""

            def __init__(self, roformer):
                super().__init__()
                self.m = roformer

            def forward(self, spec):   # [b, s, f, t, c]
                m = self.m
                stft_repr = rearrange(spec, "b s f t c -> b (f s) t c")
                x = rearrange(stft_repr, "b f t c -> b t (f c)")
                x = m.band_split(x)

                for time_transformer, freq_transformer in m.layers:
                    x = rearrange(x, "b t f d -> b f t d")
                    b, f, t, d = x.shape
                    x = time_transformer(x.reshape(b * f, t, d)).reshape(b, f, t, d)
                    x = rearrange(x, "b f t d -> b t f d")
                    x = freq_transformer(x.reshape(b * t, f, d)).reshape(b, t, f, d)

                x = m.final_norm(x)
                mask = rearrange(m.mask_estimators[0](x), "b t (f c) -> b f t c", c=2)
                re = stft_repr[..., 0] * mask[..., 0] - stft_repr[..., 1] * mask[..., 1]
                im = stft_repr[..., 0] * mask[..., 1] + stft_repr[..., 1] * mask[..., 0]
                out = rearrange(torch.stack([re, im], dim=-1), "b (f s) t c -> b s f t c", s=2)
                return torch.cat([torch.zeros_like(out[:, :, :1]), out[:, :, 1:]], dim=2)

        assert options["linear_transformer_depth"] == 0 and not options.get("skip_connection", False)
        network = Network(model).eval()

        # The whole model on random audio, against the network between our own STFTs.
        torch.manual_seed(1)
        audio = 0.1 * torch.randn(1, 2, 512 * (FRAMES - 1))
        window = torch.hann_window(2048)

        with torch.inference_mode():
            expected = model(audio)[0, 0]
            spec = torch.view_as_real(torch.stft(audio[0], 2048, 512, window=window, return_complex=True))[None]
            out = torch.view_as_complex(network(spec)[0].contiguous())
            ours = torch.istft(out, 2048, 512, window=window, length=audio.shape[-1])

        def snr(reference, estimate):
            return 10 * np.log10((reference ** 2).sum() / max(1e-30, ((reference - estimate) ** 2).sum()))

        print("network vs whole model: %.1f dB" % snr(expected, ours))

        # Attention as plain matrix multiplies: the same sums, but MSST's einsums export as Einsum,
        # which ONNX Runtime ran about twice as slowly as PyTorch on the CPU (and DirectML no faster).
        def matmul_attention(self, q, k, v):
            scale = self.scale if self.scale is not None else q.shape[-1] ** -0.5
            return torch.matmul(torch.matmul(q * scale, k.transpose(-1, -2)).softmax(dim=-1), v)

        attend.Attend.forward = matmul_attention

        with torch.inference_mode():
            out = torch.view_as_complex(network(spec)[0].contiguous())
            matmuls = torch.istft(out, 2048, 512, window=window, length=audio.shape[-1])

        print("matrix multiplies vs einsums: %.1f dB" % snr(expected, matmuls))

        onnx_file = work / "model.onnx"
        torch.onnx.export(network, (spec,), str(onnx_file), input_names=["spectrogram"], output_names=["lead"],
                          opset_version=18, dynamo=True, external_data=False)

        import onnxruntime as ort
        session = ort.InferenceSession(str(onnx_file), providers=["CPUExecutionProvider"])
        onnx_out = session.run(None, {"spectrogram": spec.numpy()})[0]

        with torch.inference_mode():
            reference = network(spec).numpy()

        match = snr(reference, onnx_out)
        print("ONNX vs PyTorch: %.1f dB" % match)

        if match < 60:
            sys.exit("The exported model doesn't match PyTorch")

        destination.write_bytes(onnx_file.read_bytes())
        print("Wrote", destination, "SHA-256", sha256(destination))


if __name__ == "__main__":
    main()
