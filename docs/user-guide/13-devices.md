# 13. Audio and MIDI Devices

[← Importing and Exporting](12-import-export.md) · [Contents](README.md) · [Next: Keyboard Shortcuts →](14-shortcuts.md)

## Audio settings

<img src="images/audio-settings.png" alt="The Audio Settings window: Output, Input, active channels, sample rate and buffer size" width="522">

Click **Audio Settings** in the toolbar to choose:
- **Output** and **Input:** the device you listen through and record from. This can be your computer's built-in speakers and microphone, headphones, or an audio interface. **Test** plays a tone, and the meter beside the input shows its level.
- **Active output and input channels:** which channels of a multi-channel interface are used. The active inputs are what audio tracks can [record from](06-recording.md#recording-audio).
- **Sample rate** and **Audio buffer size:** a smaller buffer means less delay between playing and hearing, but more risk of clicks on a busy computer.

Purrlude remembers these settings. Close the window with Esc or its close button.

## MIDI keyboards

<img src="images/midi-devices.png" alt="The MIDI Devices panel: Bluetooth MIDI with Scan, Playing the Instrument track, and MIDI outputs with a channel menu" width="340" align="right">

Click **MIDI Devices** in the toolbar for the devices panel.

**Connecting a keyboard:**
- **USB and other wired keyboards** work as soon as they're plugged in.
- Every connected keyboard plays the selected instrument track. They're listed under **Playing the Instrument track**.
- You don't need to set up any routing. To play a different track, click it.

**Mapping knobs and faders** (MIDI learn) isn't available yet.

<br clear="right">

## Bluetooth MIDI

Purrlude connects to Bluetooth LE MIDI keyboards and controllers directly, on macOS 13 or later and on Windows. On Linux it can't scan for them yet: connect the device in the system's Bluetooth settings, and with BlueZ's MIDI support it appears as an ordinary MIDI port.

1. Turn on your keyboard's Bluetooth MIDI. Its "pairing mode" isn't needed: that's for Bluetooth audio.
2. In the **Bluetooth MIDI** section, click **Scan**. Nearby devices appear with their signal strength.
3. Click **Connect**.

Once connected, the keyboard plays like a wired one.

**Remembered devices:**
- Purrlude remembers the devices you've connected and reconnects them at launch.
- If a connection drops, it reconnects when the device comes back.
- Each device shows its state: *Connected*, *Connecting...*, *Reconnecting...*, *Not in range*, or *Not found yet - turn it on to reconnect*.

**Disconnect** drops the connection for now. **Forget** disconnects and stops reconnecting automatically.

On Windows, devices that need a PIN must first be paired in Windows Settings.

## Playing a keyboard's own sounds

A keyboard with built-in sounds, such as a home keyboard or digital piano, can play a track instead of Purrlude's instruments:

1. In the track header's **sound menu**, choose the keyboard instead of *Built-in instruments*. A keyboard that isn't connected shows as *(not connected)*.
2. In the **instrument menu**, choose one of the keyboard's tones.
   - For keyboards Purrlude knows, such as the Roland GO:KEYS 3 and 5, you get the full tone list.
   - For others, choose a tone on the keyboard itself: Purrlude learns it and adds it under **Learned tones**. Use **Rename this tone...** to name it.
   - **Keyboard's current tone** leaves the keyboard's tone alone.
3. Under **MIDI outputs** in the devices panel, check the **channel** the keyboard plays its own sounds on. Known keyboards are set for you; a Roland GO:KEYS, for example, uses channel 4.

The tone is sent when you choose it, when the keyboard becomes the track's sound, and each time playback starts. Choosing a tone on the keyboard while it plays a track makes that the track's tone.

**If notes sound twice,** turn off **Local Control** on the keyboard. Otherwise it sounds both from its own keys and from what Purrlude sends back.

Purrlude effects and meters don't apply to a keyboard's own sounds. The track's volume and pan are sent to the keyboard instead.
