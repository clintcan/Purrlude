#!/usr/bin/env python3
"""Writes a Slakh-style song's reference.mid for [.separate-score]: the MIDI of each instrument
that has audio, one track per instrument named "<class> <stem>", drums on channel 10, timed in
seconds (at 120 BPM). It goes by which files exist (BabySlakh's metadata says nothing was
rendered). The song's mix is mix.flac or mix.wav (render_gm.py's songs have no mix: score Slakh's).

    python tools/training/make_reference.py <song folder> [more ...]
"""
import sys, os, mido, yaml


def make_reference(track):
    meta = yaml.safe_load(open(os.path.join(track, 'metadata.yaml')))
    out = mido.MidiFile(type=1, ticks_per_beat=960)

    for name, stem in sorted(meta['stems'].items()):
        path = os.path.join(track, 'MIDI', name + '.mid')
        has_audio = any(os.path.exists(os.path.join(track, 'stems', name + suffix)) for suffix in ('.wav', '.flac'))
        if not os.path.exists(path) or not has_audio:
            continue
        t, events = 0.0, []
        for msg in mido.MidiFile(path):   # absolute seconds through the file's tempo map
            t += msg.time
            if msg.type in ('note_on', 'note_off'):
                events.append((t, msg))
        channel = 9 if stem.get('is_drum') else 0
        trk = mido.MidiTrack()
        trk.append(mido.MetaMessage('track_name', name=f"{stem.get('inst_class')} {name}"))
        now = 0
        for sec, msg in events:          # written at 120 BPM (no tempo event): 1920 ticks a second
            tick = round(sec * 1920)
            trk.append(msg.copy(channel=channel, time=tick - now))
            now = tick
        out.tracks.append(trk)

    out.save(os.path.join(track, 'reference.mid'))
    print(os.path.basename(track), len(out.tracks), 'parts')


if __name__ == '__main__':
    for track in sys.argv[1:]:
        make_reference(track)
