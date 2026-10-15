# 2. Projects

[← Getting Started](01-getting-started.md) · [Contents](README.md) · [Next: Transport →](03-transport.md)

A project is one song: its tracks, clips, tempo, effects and loop range. Projects are saved as `.purrlude` files.

## New, open and save

All of these are in the **File** menu.

| Command | Shortcut | What it does |
|---|---|---|
| New | Cmd+N | Starts an empty song with one instrument track |
| Open... | Cmd+O | Opens a `.purrlude` file |
| Save | Cmd+S | Saves the song. An untitled song asks for a name first |
| Save As... | Cmd+Shift+S | Saves under a new name or in a new place |

The default folder is `Documents/Purrlude`. The window's title shows the song's name, with a `*` in front when there are unsaved changes ("Untitled" until it's first saved).

If the song has unsaved changes when you start a new one, open another or quit, Purrlude asks **Save changes?** with **Save**, **Don't Save** and **Cancel**.

## Where the audio goes

Recordings and imported audio files are kept in a folder next to the project file, named after it: saving `My Song.purrlude` also creates `My Song Audio`. Saving gathers every audio file the song uses into that folder.

To move or back up a project, keep the `.purrlude` file and its `Audio` folder together. A project moved along with its folder still finds its audio.

Before the first save, recordings go to a temporary folder. They move into the project's folder when you save.

## Autosave and recovery

Purrlude saves a backup copy of your song every 30 seconds while you work, but not while recording. The backup is kept separately and never overwrites your own file.

- When you close normally, the backup is deleted.
- If Purrlude quits unexpectedly, the next launch asks **Recover unsaved work?**, naming the song and when the backup was made.
  - **Recover** opens the backed-up song. Save it to keep it.
  - **Discard** deletes the backup.

Backups live beside Purrlude's settings file:
- macOS: `~/Library/Application Support/Purrlude/Autosave`
- Windows: `%APPDATA%\Purrlude\Autosave`
- Linux: `~/.config/Purrlude/Autosave`

## Undo

**Edit > Undo** (Cmd+Z) and **Redo** (Cmd+Shift+Z) cover every change to the song:
- notes, clips and tracks
- mixer moves, effects and their knobs
- imports and conversions

A whole drag, such as moving a clip or turning a knob, is one step. Playback position, the loop range and the zoom aren't part of the song's history, so undo leaves them alone.
