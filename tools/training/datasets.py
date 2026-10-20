#!/usr/bin/env python3
"""Downloads the openly licensed recordings Purrlude's separation model is trained on from
scratch, and lists them by part in <data folder>/pools.json for train.py --pools.

    python tools/training/datasets.py <data folder> [dataset ...]   (default: all of DATASETS)
    python tools/training/datasets.py --list

Every dataset here allows training a model and sharing it freely, for any use (CC BY 4.0;
see each one's licence below): no non-commercial, no-derivatives or research-only data, so
the model isn't bound by terms like Meta's Demucs weights ("provided only for scientific
purposes"). Checked against MUSDB18, MoisesDB, MedleyDB, MAESTRO, GTSinger, M4Singer and
Opencpop, which are left out for that reason. CC BY asks for credit: CREDITS.md, written
next to pools.json, gives it, and goes with the model's release.

Each archive is downloaded (resuming), checked against the MD5 its publisher gives, unpacked
and deleted. Each audio file goes to one of the model's parts (drums, bass, other, vocals,
guitar, piano) by its dataset's rule, or is left out (e.g. a larynx microphone, which
doesn't sound like a voice, or a piece with piano and other instruments together). Steps
already done are skipped, so it can be run again after an interruption.
"""

import argparse
import csv
import hashlib
import json
import subprocess
import tarfile
import zipfile
from dataclasses import dataclass, field
from pathlib import Path

AUDIO = {".wav", ".flac"}


def zenodo(record, name):
    return f"https://zenodo.org/records/{record}/files/{name}?download=1"


@dataclass
class Dataset:
    title: str
    credit: str
    licence: str
    source: str
    archives: list            # (url, file name, md5)
    part: object              # (path relative to the dataset's folder, data folder) -> part or None
    weight: float = 1.0       # how often train.py picks it among its part's datasets
    notes: str = ""
    extra: list = field(default_factory=list)   # (url, file name, md5) kept as files, not unpacked


def everything(part):
    return lambda path, data: part


def dagstuhl(path, data):
    # One track per singer from dynamic and headset microphones, and room microphones; a larynx
    # microphone picks up the throat, not the voice.
    return None if "_LRX" in path.name else "vocals"


def cantoria(path, data):
    # Per-singer tracks (S, A, T, B) and their mix; the mix with the organ is left out.
    return None if path.stem.endswith("MixOrgan") else "vocals"


def musicnet(path, data):
    # By ensemble (musicnet_metadata.csv): solo piano is piano (15 h); strings and winds without
    # a keyboard are "other" (about 12 h); piano with other instruments is left out, and so is
    # "Accompanied Violin" and the like, which are accompanied by a piano.
    ensemble = musicnet_ensembles(data).get(path.stem)
    if ensemble is None:
        return None
    if ensemble == "Solo Piano":
        return "piano"
    return None if any(w in ensemble for w in ("Piano", "Voice", "Accompanied")) else "other"


_ensembles = None


def musicnet_ensembles(data):
    global _ensembles
    if _ensembles is None:
        with open(data / "musicnet" / "musicnet_metadata.csv", newline="") as f:
            _ensembles = {row["id"]: row["ensemble"] for row in csv.DictReader(f)}
    return _ensembles


DATASETS = {
    "vocadito": Dataset(
        "vocadito", "Rachel Bittner, Katherine Pasalo, Juan José Bosch, Gabriel Meseguer-Brocal, David Rubinstein",
        "CC BY 4.0", "https://zenodo.org/records/5578807",
        [(zenodo(5578807, "vocadito.zip"), "vocadito.zip", "dea40fd18f14d899643c4ba221b33a46")],
        everything("vocals")),
    "vocalset": Dataset(
        "VocalSet", "Julia Wilkins, Prem Seetharaman, Alison Wahl, Bryan Pardo",
        "CC BY 4.0", "https://zenodo.org/records/1193957",
        [(zenodo(1193957, "VocalSet.zip"), "VocalSet.zip", "c44f60d34b8724b9a6f6d15e0a3158a9")],
        everything("vocals"), weight=3.0),
    "choral-singing": Dataset(
        "Choral Singing Dataset", "Helena Cuesta, Emilia Gómez, Agustín Martorell, Felipe Loáiciga",
        "CC BY 4.0", "https://zenodo.org/records/2649950",
        [(zenodo(2649950, "ChoralSingingDataset.zip"), "ChoralSingingDataset.zip", "7a9643609a1c3902b5255d78dbba3303")],
        everything("vocals"), weight=0.5),
    "cantoria": Dataset(
        "Cantoría Dataset", "Helena Cuesta, Emilia Gómez",
        "CC BY 4.0", "https://zenodo.org/records/5851070",
        [(zenodo(5851070, "CantoriaDataset_v1.0.0.zip"), "CantoriaDataset_v1.0.0.zip", "93672d152aa08f4a739dcc8c2817d53b")],
        cantoria, weight=0.5),
    "dagstuhl-choirset": Dataset(
        "Dagstuhl ChoirSet", "Sebastian Rosenzweig, Helena Cuesta, Christof Weiß, Frank Scherbaum, Emilia Gómez, Meinard Müller",
        "CC BY 4.0", "https://zenodo.org/records/3897182",
        [(zenodo(3897182, "DagstuhlChoirSet.zip"), "DagstuhlChoirSet.zip", "6d7ccdd5e3f43e54981b0f12d19987f9")],
        dagstuhl, weight=1.0),
    "libritts": Dataset(
        "LibriTTS (train-clean-100)", "Heiga Zen, Viet Dang, Rob Clark, Yu Zhang, Ron J. Weiss, Ye Jia, Zhifeng Chen, Yonghui Wu",
        "CC BY 4.0", "https://www.openslr.org/60/",
        [("https://www.openslr.org/resources/60/train-clean-100.tar.gz", "train-clean-100.tar.gz", "4a8c202b78fe1bc0c47916a98f3a2ea8")],
        everything("vocals"), weight=0.5,
        notes="Read speech, not singing: a little of it teaches voice apart from instruments."),
    "guitarset": Dataset(
        "GuitarSet 1.1.0", "Qingyang Xi, Rachel M. Bittner, Johan Pauwels, Xuzhou Ye, Juan P. Bello",
        "CC BY 4.0 (version 1.1.0; 1.0.1 was CC BY-NC)", "https://zenodo.org/records/3371780",
        [(zenodo(3371780, "audio_mono-mic.zip"), "audio_mono-mic.zip", "275966d6610ac34999b58426beb119c3")],
        everything("guitar")),
    "musicnet": Dataset(
        "MusicNet", "John Thickstun, Zaid Harchaoui, Sham M. Kakade",
        "CC BY 4.0", "https://zenodo.org/records/5120004",
        [(zenodo(5120004, "musicnet.tar.gz"), "musicnet.tar.gz", "844764911fa0d5b97c97da944a057590")],
        musicnet,
        extra=[(zenodo(5120004, "musicnet_metadata.csv"), "musicnet_metadata.csv", "1caef62cee9c875235e62aac368b49d8")]),
    "e-gmd": Dataset(
        "E-GMD (Expanded Groove MIDI Dataset)", "Lee Callender, Curtis Hawthorne, Jesse Engel (Google Magenta)",
        "CC BY 4.0", "https://magenta.tensorflow.org/datasets/e-gmd",
        [("https://storage.googleapis.com/magentadata/datasets/e-gmd/v1.0.0/e-gmd-v1.0.0.zip", "e-gmd-v1.0.0.zip", "510af23329b0472a8349d1aaf8fb98dd")],
        everything("drums"), weight=2.0),
}


def md5(path):
    digest = hashlib.md5()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 22), b""):
            digest.update(block)
    return digest.hexdigest()


def download(url, target, checksum):
    """The file at url, checked against its MD5; resumes a partial download."""
    if target.exists() and md5(target) == checksum:
        return
    part = target.with_name(target.name + ".part")
    subprocess.run(["curl", "-fL", "--retry", "5", "-C", "-", "-o", str(part), url], check=True)
    actual = md5(part)
    if actual != checksum:
        part.unlink()
        raise SystemExit(f"{target.name}: MD5 {actual}, expected {checksum}; deleted, run again")
    part.replace(target)


def unpack(archive, folder):
    print(f"Unpacking {archive.name}", flush=True)
    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as z:
            z.extractall(folder)
    else:
        with tarfile.open(archive) as t:
            t.extractall(folder, filter="data")


def fetch(name, data):
    dataset, folder = DATASETS[name], data / name
    done = folder / ".done"
    if done.exists():
        return
    folder.mkdir(parents=True, exist_ok=True)

    for url, file_name, checksum in dataset.extra:
        download(url, folder / file_name, checksum)

    for url, file_name, checksum in dataset.archives:
        archive = data / file_name
        print(f"== {dataset.title}: {file_name}", flush=True)
        download(url, archive, checksum)
        unpack(archive, folder)
        archive.unlink()

    done.touch()


def list_files(name, data):
    """Each audio file of a dataset, with its part, as (path relative to data, part)."""
    dataset, folder = DATASETS[name], data / name
    files = []
    for path in sorted(folder.rglob("*")):
        # Archives made on a Mac carry __MACOSX/._* copies that aren't audio.
        if path.suffix.lower() in AUDIO and not path.name.startswith("._") and "__MACOSX" not in path.parts:
            part = dataset.part(path.relative_to(folder), data)
            if part is not None:
                files.append((str(path.relative_to(data)), part))
    return files


def write_pools(names, data):
    pools, credits = [], ["# Training data\n", "The separation model was trained on these recordings, "
                          "each under the licence given (CC BY 4.0 asks for this credit).\n"]
    for name in names:
        dataset = DATASETS[name]
        by_part = {}
        for path, part in list_files(name, data):
            by_part.setdefault(part, []).append(path)
        for part, files in sorted(by_part.items()):
            pools.append({"dataset": name, "part": part, "weight": dataset.weight, "files": files})
            print(f"{name}: {len(files)} files of {part}")
        credits.append(f"- **{dataset.title}** by {dataset.credit}. {dataset.licence}. {dataset.source}"
                       + (f" {dataset.notes}" if dataset.notes else "") + "\n")

    (data / "pools.json").write_text(json.dumps({"pools": pools}, indent=1))
    (data / "CREDITS.md").write_text("\n".join(credits[:2]) + "\n" + "".join(credits[2:]))
    print(f"Wrote {data / 'pools.json'} and {data / 'CREDITS.md'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("data", type=Path, nargs="?")
    parser.add_argument("datasets", nargs="*", help=f"some of: {', '.join(DATASETS)}")
    parser.add_argument("--list", action="store_true", help="list the datasets and stop")
    args = parser.parse_args()

    if args.list or args.data is None:
        for name, d in DATASETS.items():
            print(f"{name:18} {d.licence:12} {d.title}: {d.source}")
        return

    names = args.datasets or list(DATASETS)
    unknown = [n for n in names if n not in DATASETS]
    if unknown:
        raise SystemExit(f"Unknown: {', '.join(unknown)}")

    args.data.mkdir(parents=True, exist_ok=True)
    for name in names:
        fetch(name, args.data)
    write_pools(names, args.data)


if __name__ == "__main__":
    main()
