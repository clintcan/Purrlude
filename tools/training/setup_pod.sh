#!/usr/bin/env bash
# Sets up a rented GPU machine (a RunPod pod from its PyTorch image, Ubuntu, run as root) for
# training, with the data on a network volume so a pod can be swapped without downloading it
# again:
#
#     git clone https://github.com/clintcan/Purrlude.git && cd Purrlude
#     tools/training/setup_pod.sh /workspace/data
#
# Then, on the GPU pod (see train.py for the options), with the environment it made:
#
#     cd tools/training && /workspace/data/venv/bin/python train.py /workspace/data/checkpoints \
#         /workspace/data/slakh2100_flac_redux/train /workspace/data/gm-renders --workers 12
#
# or, from scratch (no Meta weights; see train.py), with the recordings datasets.py fetched:
#
#     ... train.py /workspace/data/scratch /workspace/data/slakh2100_flac_redux/train \
#         /workspace/data/gm-renders --pools /workspace/data/pools/pools.json --from-scratch \
#         --lr 3e-4 --warmup 5000 --steps 300000 --batch 8 --workers 12
#
# It installs FluidSynth and FluidR3_GM (MIT), Demucs (MIT) without letting pip replace the
# image's PyTorch, downloads Slakh2100 (CC BY 4.0, 104 GB, checked against Zenodo's MD5) and
# GeneralUser GS (Purrlude's own General MIDI sounds, the pinned file and SHA-256 the build uses),
# renders GM_SONGS (default 600) of Slakh's training songs through both sound sets, and with
# POOLS=1 downloads datasets.py's recordings (CC BY 4.0; about 190 GB unpacked, E-GMD 141 GB of it,
# and its 96 GB archive sits beside it while it unpacks), or only the ones named in DATASETS. With
# Slakh (about 110 GB) and the renders, that peaks near 430 GB: make the volume 500 GB.
# Slakh's validation and test songs are kept out of training. Each step is skipped when done,
# so it can be run again after an interruption. The downloads and renders need no GPU: a cheap
# CPU pod with the same volume can do them.
set -euo pipefail

data=${1:?usage: setup_pod.sh <data folder>}
gm_songs=${GM_SONGS:-600}
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$data"

echo "== Packages"
apt-get update -qq
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq fluidsynth fluid-soundfont-gm > /dev/null

# A virtual environment on the volume that still sees the image's CUDA build of PyTorch (the
# image's own Python is externally managed, PEP 668), and pip may not change torch or torchaudio.
[[ -x "$data/venv/bin/python" ]] || python -m venv --system-site-packages "$data/venv"
python="$data/venv/bin/python"
"$python" - > "$data/constraints.txt" <<'EOF'
import importlib.metadata as m
for name in ("torch", "torchaudio"):
    try:
        print(f"{name}=={m.version(name)}")
    except m.PackageNotFoundError:
        pass
EOF
grep -q torchaudio "$data/constraints.txt" || { echo "This image has no torchaudio; use RunPod's PyTorch image" >&2; exit 1; }
"$python" -m pip install -q -c "$data/constraints.txt" demucs==4.0.1 soundfile pyyaml mido
"$python" -c "import torch; print('PyTorch', torch.__version__, 'CUDA', torch.cuda.is_available())"

echo "== Slakh2100"
if [[ ! -d "$data/slakh2100_flac_redux/train" ]]; then
    archive="$data/slakh2100_flac_redux.tar.gz"
    curl -fL --retry 5 -C - -o "$archive" "https://zenodo.org/records/4599666/files/slakh2100_flac_redux.tar.gz?download=1"
    echo "f4b71b6c45ac9b506f59788456b3f0c4  $archive" | md5sum --check --quiet
    # Network volumes refuse chown, which tar (as root) tries for every file.
    tar -xzf "$archive" -C "$data" --no-same-owner
    rm "$archive"
fi

echo "== Sound sets"
generaluser="$data/GeneralUser-GS.sf2"
if [[ ! -f "$generaluser" ]]; then
    curl -fL --retry 5 -o "$generaluser" \
        https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/684543d5e5efaef08d02be50dcda8d552478fa60/GeneralUser-GS.sf2
fi
echo "9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe  $generaluser" | sha256sum --check --quiet
fluidr3=/usr/share/sounds/sf2/FluidR3_GM.sf2
[[ -f "$fluidr3" ]] || { echo "No $fluidr3" >&2; exit 1; }

# The open recordings download while the songs render: one is mostly waiting on the network,
# the other on FluidSynth, which uses one core per song, so one renderer runs per core.
if [[ "${POOLS:-0}" == 1 ]]; then
    echo "== Openly licensed recordings (log: $data/pools.log)"
    # shellcheck disable=SC2086
    "$python" "$here/datasets.py" "$data/pools" ${DATASETS:-} > "$data/pools.log" 2>&1 &
    pools=$!
fi

# nproc may count the host's cores, not the pod's: set RENDERERS to the pod's vCPUs, less one.
renderers=${RENDERERS:-$(nproc)}
echo "== General MIDI renders ($gm_songs songs, $renderers at a time)"
pids=()
for i in $(seq 1 "$renderers"); do
    "$python" "$here/render_gm.py" "$data/slakh2100_flac_redux/train" "$data/gm-renders" "$generaluser" "$fluidr3" \
        --limit "$gm_songs" --part "$i/$renderers" &
    pids+=($!)
done
for pid in "${pids[@]}"; do wait "$pid"; done

if [[ -n "${pools:-}" ]]; then
    wait "$pools" || { echo "Downloading the recordings failed:" >&2; tail -5 "$data/pools.log" >&2; exit 1; }
    tail -12 "$data/pools.log"
fi

echo "Ready: $(du -sh "$data" | cut -f1) in $data"
