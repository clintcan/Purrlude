#!/usr/bin/env python3
"""Builds content/loops/groove.json: drum loops from the Groove MIDI Dataset.

The Groove MIDI Dataset (Google Magenta) is CC BY 4.0: Purrlude ships loops derived
from it and must show the attribution in the app (see content/LICENSES.md).

    python3 -I tools/content/build_groove_loops.py <download-dir>

Downloads the dataset (pinned URL, SHA-256 checked) into <download-dir>, picks up to
three 4/4 beats per style, cuts the steadiest 4-bar stretch of each (skipping intros
and fills), maps the Roland TD-11 kit to General MIDI drum notes and writes the loops
in quarter-note beats, so they follow the project tempo and keep the drummer's feel.
"""

import csv
import hashlib
import io
import json
import os
import struct
import sys
import urllib.request
import zipfile

URL = "https://storage.googleapis.com/magentadata/datasets/groove/groove-v1.0.0-midionly.zip"
SHA256 = "651cbc524ffb891be1a3e46d89dc82a1cecb09a57c748c7b45b844c4841dcc1e"

OUT = os.path.join(os.path.dirname(__file__), "..", "..", "content", "loops", "groove.json")

LOOP_BARS = 4
BEATS_PER_BAR = 4
LOOPS_PER_STYLE = 3
EARLY = 0.125          # beats: hits this close before a downbeat are played on it

# User-facing styles and the dataset styles they draw from, in menu order.
STYLES = [
    ("Rock", ["rock"]), ("Pop", ["pop"]), ("Funk", ["funk"]), ("Soul", ["soul"]),
    ("Hip-Hop", ["hiphop"]), ("Jazz", ["jazz"]), ("Latin", ["latin", "afrocuban"]),
    ("Reggae", ["reggae"]), ("Afrobeat", ["afrobeat", "highlife"]), ("Disco & Dance", ["dance"]),
    ("Country", ["country"]), ("Blues & Shuffle", ["blues"]), ("Punk", ["punk"]),
    ("New Orleans", ["neworleans"]),
]

# Roland TD-11 notes in the dataset -> General MIDI drums (per the dataset's documentation).
TD11_TO_GM = {
    36: 36,                       # kick
    38: 38, 40: 38, 37: 37,       # snare head, snare rim -> snare; cross-stick -> side stick
    48: 48, 50: 48,               # tom 1 (+rim) -> hi-mid tom
    45: 45, 47: 45,               # tom 2 (+rim) -> low tom
    43: 43, 58: 43,               # floor tom (+rim) -> high floor tom
    46: 46, 26: 46,               # open hi-hat (+edge)
    42: 42, 22: 42,               # closed hi-hat (+edge)
    44: 44,                       # pedal hi-hat
    49: 49, 55: 49,               # crash 1 (+edge)
    57: 57, 52: 57,               # crash 2 (+edge)
    51: 51, 59: 51, 53: 53,       # ride, ride edge, ride bell
}


def download(directory):
    path = os.path.join(directory, "groove-v1.0.0-midionly.zip")

    if not os.path.exists(path):
        print("Downloading", URL)
        urllib.request.urlretrieve(URL, path)

    with open(path, "rb") as f:
        data = f.read()

    if hashlib.sha256(data).hexdigest() != SHA256:
        sys.exit("Checksum mismatch for " + path)

    return zipfile.ZipFile(io.BytesIO(data))


def read_notes(midi_bytes):
    """Minimal Standard MIDI File reader: (start beat, length beats, note, velocity) for each note."""
    if midi_bytes[:4] != b"MThd":
        raise ValueError("not a MIDI file")

    _, _, division = struct.unpack(">HHH", midi_bytes[8:14])
    pos, notes = 14, []

    while pos < len(midi_bytes):
        chunk, length = midi_bytes[pos:pos + 4], struct.unpack(">I", midi_bytes[pos + 4:pos + 8])[0]
        track, pos = midi_bytes[pos + 8:pos + 8 + length], pos + 8 + length

        if chunk != b"MTrk":
            continue

        i, ticks, status, held = 0, 0, 0, {}

        def varlen():
            nonlocal i
            value = 0
            while True:
                byte = track[i]; i += 1
                value = (value << 7) | (byte & 0x7F)
                if not byte & 0x80:
                    return value

        while i < len(track):
            ticks += varlen()
            byte = track[i]

            # (Read lengths first: "i += varlen()" would read i before varlen() advances it.)
            if byte == 0xFF:                              # meta event
                i += 2
                length = varlen()
                i += length
                continue
            if byte in (0xF0, 0xF7):                      # sysex
                i += 1
                length = varlen()
                i += length
                continue
            if byte & 0x80:
                status = byte; i += 1                     # otherwise running status

            kind = status & 0xF0
            size = 1 if kind in (0xC0, 0xD0) else 2
            a, b = track[i], track[i + 1] if size == 2 else 0
            i += size

            if kind == 0x90 and b > 0:
                held.setdefault(a, []).append((ticks, b))
            elif kind == 0x80 or (kind == 0x90 and b == 0):
                if held.get(a):
                    start, velocity = held[a].pop(0)
                    notes.append((start / division, (ticks - start) / division, a, velocity))

    return sorted(notes)


def best_window(notes, total_bars):
    """The steadiest LOOP_BARS-bar stretch (similar note counts per bar), skipping the first bar."""
    counts = [0] * total_bars

    for start, _, _, _ in notes:
        bar = int(start // BEATS_PER_BAR)
        if bar < total_bars:
            counts[bar] += 1

    best, best_score = None, None

    for first in range(1, total_bars - LOOP_BARS + 1):
        window = counts[first:first + LOOP_BARS]
        if min(window) == 0:
            continue
        score = (max(window) - min(window)) / max(window)   # 0 = every bar alike
        if best_score is None or score < best_score:
            best, best_score = first, score

    return best


def is_shuffle(notes):
    """True if a good share of the onsets fall on 8th-note triplets (a swung or 12/8 feel)."""
    triplets = straight = 0

    for start, _, _, _ in notes:
        f = start % 1
        to_sixteenth = min(abs(f - k / 4) for k in range(5))
        to_triplet = min(abs(f - 1 / 3), abs(f - 2 / 3))

        if to_triplet < 0.06 and to_triplet < to_sixteenth:
            triplets += 1
        elif to_sixteenth < 0.06:
            straight += 1

    return triplets > 0.3 * (triplets + straight)


def title(style):
    parts = style.split("/")
    if len(parts) == 1:
        return ""
    return parts[1].replace("-", " ").replace("groove", "Groove ").title().replace("Groove  ", "Groove ").strip()


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)

    archive = download(sys.argv[1])
    rows = list(csv.DictReader(io.TextIOWrapper(archive.open("groove/info.csv"), encoding="utf-8")))
    beats = [r for r in rows if r["beat_type"] == "beat" and r["time_signature"] == "4-4"]

    loops = []

    for name, sources in STYLES:
        candidates = [r for r in beats if r["style"].split("/")[0] in sources]
        candidates.sort(key=lambda r: (r["style"], r["drummer"], r["id"]))
        chosen, seen = [], set()

        # Prefer different sub-styles and drummers.
        for r in candidates:
            key = (r["style"], r["drummer"])
            if key in seen:
                continue

            notes = read_notes(archive.read("groove/" + r["midi_filename"]))
            total_bars = int(max((n[0] for n in notes), default=0) // BEATS_PER_BAR)
            first = best_window(notes, total_bars) if total_bars >= LOOP_BARS + 1 else None

            if first is None:
                continue

            start, end = first * BEATS_PER_BAR, (first + LOOP_BARS) * BEATS_PER_BAR
            loop_notes = []

            # Hits played just ahead of a downbeat belong to it: the window is shifted back by
            # EARLY, so the loop's last bar doesn't end on the next repeat's first hit.
            for s, length, pitch, velocity in notes:
                if start - EARLY <= s < end - EARLY and pitch in TD11_TO_GM:
                    loop_notes.append([round(max(0.0, s - start), 3), round(max(0.05, min(length, end - s)), 3),
                                       TD11_TO_GM[pitch], velocity])

            loop_notes.sort()
            seen.add(key)
            subtitle = title(r["style"])
            shuffle = is_shuffle(loop_notes)

            if shuffle and "Shuffle" not in subtitle:
                subtitle = (subtitle + " shuffle") if subtitle else "Shuffle feel"

            chosen.append({
                "id": r["id"].replace("/", "-"),
                "name": "",
                "shuffle": shuffle,
                "description": (subtitle + " · " if subtitle else "") + r["bpm"] + " BPM",
                "style": name,
                "bpm": int(r["bpm"]),
                "beatsPerBar": BEATS_PER_BAR,
                "lengthBeats": LOOP_BARS * BEATS_PER_BAR,
                "source": r["midi_filename"],
                "notes": loop_notes,
            })

            if len(chosen) == LOOPS_PER_STYLE:
                break

        # Straight loops first: shuffles can't be notated until triplets are supported.
        chosen.sort(key=lambda loop: loop.pop("shuffle"))

        for i, loop in enumerate(chosen):
            loop["name"] = name + " " + str(i + 1)

        loops += chosen
        print(f"{name}: {len(chosen)} loops")

    doc = {
        "name": "Groove MIDI Dataset drum loops",
        "attribution": "Drum loops from the Groove MIDI Dataset by Jon Gillick, Adam Roberts, Jesse Engel, "
                       "Douglas Eck and David Bamman (Google Magenta), CC BY 4.0. "
                       "https://magenta.tensorflow.org/datasets/groove",
        "license": "CC BY 4.0",
        "changes": "4-bar excerpts; Roland TD-11 drum notes mapped to General MIDI.",
        "loops": loops,
    }

    with open(OUT, "w") as f:
        json.dump(doc, f, separators=(",", ":"))

    print(f"Wrote {len(loops)} loops to {os.path.normpath(OUT)} ({os.path.getsize(OUT) // 1024} KB)")


if __name__ == "__main__":
    main()
