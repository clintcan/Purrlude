# 3. Transport, Tempo and Looping

[← Projects](02-projects.md) · [Contents](README.md) · [Next: Tracks and Mixing →](04-tracks.md)

<img src="images/transport-bar.png" alt="The transport bar: |&lt;, Play, Record, Loop, Punch, Automate, the position, the tempo, 4/4, Metronome and Count-in, then Editors, Audio, Plugins, MIDI Learn, Loops and MIDI Devices" width="100%">

## Playing

| Control | Key | What it does |
|---|---|---|
| **\|<** | Home | Moves the playhead back to the start |
| **Play** / **Stop** | Space | Starts and stops playback |
| **Record** / **Stop Rec** | R | Records onto the armed tracks; see [Recording](06-recording.md) |
| **Loop** | L | Repeats the loop range; see below |
| **Punch** | P | Records only inside the loop range; see [Punch in/out](06-recording.md#punch-inout) |
| **Automate** | A | Records the controls you move while the song plays; see [Recording automation](04-tracks.md#recording-automation) |

**Where playback starts:**
- It starts from the playhead, the yellow line.
- To move the playhead, click a bar number on the ruler, or click an empty part of a track's lane.
- Pressing Play at or past the end of the song starts from the beginning.
- Playback stops by itself a second after the last clip ends (not while looping or recording).

The position display shows the playhead as **bar.beat** and as **minutes:seconds**. For example, `3.3   0:06` is the third beat of bar 3, six seconds in.

## Tempo

The tempo is the number before **BPM**: the tempo at the playhead. Click it, type a new value between 20 and 400, and press Return.
- Notes stay on their beats, so the song speeds up or slows down. If the song changes tempo, this changes the tempo the playhead is in.
- Importing a MIDI file or adding a drum loop to an empty song sets the tempo for you.

### Tempo changes

A song can change tempo partway through: a slower bridge, say, or a big ritardando at the end.

<img src="images/ruler-tempo.png" alt="The ruler with a tempo change and a time signature change at bar 5: a line, half green and half purple, and 3/4 beside the bar number" width="100%">

<img src="images/tempo-menu.png" alt="The ruler's menu at bar 5: the tempo change (Change Tempo..., Gradual, Remove Tempo Change) and the time signature change (Change To, Remove Time Signature Change)" width="468" align="right">

- **Add one:** right-click the ruler in the bar where the new tempo should start, choose **Add Tempo Change at Bar n...**, and type the tempo. It takes effect at the start of that bar.
- **See them:** each change is a green line on the ruler, with its tempo beside the bar number. When the bar is too narrow for the label, the line still shows.
- **Change or remove one:** right-click it on the ruler.
- **Gradual:** a change can be **Gradual**: the tempo then speeds up or slows down steadily from the change before (an *accel.* or *rit.*), arriving at the new tempo on its bar. The ruler shows it as **→ 120 BPM**.

**What stays where:** clips, notes, automation and the loop range stay on their bars and beats; only how fast they go by changes. Audio clips aren't stretched: they start on their beat and play at their own speed.

Every change is one undo step, and the score shows the changes as tempo marks.

<br clear="right">

## Time signature

<img src="images/time-signature-menu.png" alt="The time signature menu: 2/4, 3/4, 4/4, 5/4, 6/8, 7/8, 9/8, 12/8 and Other..." width="162" align="right">

Click the time signature (it says **4/4**) and choose another, or **Other...** for any other.
- The **Other...** dialog asks how many beats are in a bar (1–32) and which note gets the beat (2, 4, 8 or 16).
- **Changing it keeps the music where it is**: notes don't move, only the bar lines do. For example, a song in 4/4 changed to 3/4 sounds the same, barred in threes.
- The change is one undo step.
- If the song changes time signature (see below), the button shows the one at the playhead, and changes that one.

### Time signature changes

A song can change time signature from a bar on: a bar of 2/4 in a 4/4 song, or a section in 3/4.

- **Add one:** right-click the ruler in the bar where it should start, open **Time Signature from Bar n**, and choose one, or **Other...** to type how many beats are in a bar.
- **See them:** each is a purple line on the ruler, with the signature beside the bar number.
- **Change or remove one:** right-click it on the ruler. Removing a one-bar change (a bar of 2/4 between bars of 4/4) also removes the change back, so the bars are as they were.
- **The music stays where it is**, as with the whole song's time signature: only the bar lines after the change move. Bar numbers, the grid, snapping, loops and the score all follow.
- Every change is one undo step.

**Limits:**
- **The note that gets the beat stays the same** through a song: a 4/4 song can have bars of 2/4, 3/4 or 5/4, and a 6/8 song bars of 9/8, but a 4/4 song can't change to 6/8 partway. To change it for the whole song, remove the changes first.
- **Loops and accompaniment:** the built-in drum loops and accompaniment are written in 4/4.
- The time signature can't be changed while recording.

<br clear="right">

## Metronome and count-in

- **Metronome** clicks on every beat while playing and recording. It's never included in exported audio.
- **Count-in** plays one bar of clicks before recording starts.

## Loop playback

<img src="images/ruler.png" alt="The ruler with an orange loop band over bars 1 to 4" width="100%">

The thin strip along the top of the ruler holds the **loop range**, shown as a band. While looping is on, the band is orange and the lanes under it are shaded. When **Punch** is on (and Loop is off), the same range is the punch range, and the band is vermillion. Otherwise the band is grey.

**Setting the loop:**
- Turn looping on and off with the **Loop** button or **L**.
  - Turning it on with no range set loops four bars from the playhead's bar.
- Drag along the top strip to set a new range. It snaps to bars; hold **Shift** to snap to beats.
- Drag the band to move it, or drag either end to change its length.
- Click the band to turn looping on or off.
- Right-click a clip and choose **Loop This Clip** to loop exactly that clip.
- Right-click the ruler and choose **Remove Loop Range** to take the band away (and **Loop** to turn looping on or off).

**While looping:**
- Playback repeats the range until you stop it.
- **Play** from outside the range starts at the range's start.
- Recording while looping records a new take on each pass; see [Recording](06-recording.md#loop-recording-and-takes).
- The range can't be changed while recording.

The loop range is saved with the song. It's measured in beats, so it stays on the same bars when the tempo changes.
