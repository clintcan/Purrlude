#include "MidiDevicesPanel.h"

namespace purrlude
{

namespace
{
    constexpr int rowHeight = 56;
    constexpr int margin = 10;

    juce::String describe (const BluetoothMidiManager::Device& d)
    {
        if (d.info.connected)     return "Connected";
        if (d.info.connecting)    return "Connecting...";
        if (d.waiting)            return "Waiting for the device to reset...";

        if (! d.inRange)
            return d.reconnecting ? "Not found yet - turn it on to reconnect" : "Not in range";

        if (d.reconnecting)       return "Reconnecting...";

       #if JUCE_MAC
        // macOS reports devices CoreMIDI remembers; BLE MIDI devices usually don't bond.
        if (d.info.paired)        return "Remembered";
       #else
        if (d.info.paired)        return "Paired";
       #endif

        return d.info.rssi != 0 ? "Available (" + juce::String (d.info.rssi) + " dBm)" : "Available";
    }
}

//==============================================================================
class MidiDevicesPanel::DeviceRow final : public juce::Component
{
public:
    DeviceRow (BluetoothMidiManager& manager, const BluetoothMidiManager::Device& d)
    {
        name.setText (d.info.name, juce::dontSendNotification);
        name.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        status.setText (describe (d), juce::dontSendNotification);
        status.setFont (juce::FontOptions (13.0f));
        status.setColour (juce::Label::textColourId, juce::Colours::lightgrey);

        bool active = d.info.connected || d.info.connecting || d.waiting;
        connectButton.setButtonText (d.waiting ? "Cancel" : active ? "Disconnect" : "Connect");
        connectButton.setEnabled (active || d.inRange);
        connectButton.onClick = [&manager, id = d.info.id, active]
        {
            if (active)
                manager.disconnect (id);
            else
                manager.connect (id);
        };

        forgetButton.setTooltip ("Disconnect and stop reconnecting automatically");
        forgetButton.onClick = [&manager, id = d.info.id] { manager.forget (id); };
        forgetButton.setVisible (d.remembered || d.info.paired);

        addAndMakeVisible (name);
        addAndMakeVisible (status);
        addAndMakeVisible (connectButton);
        addChildComponent (forgetButton);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 4);
        auto buttons = r.removeFromRight (96);
        connectButton.setBounds (buttons.removeFromTop (r.getHeight() / 2).reduced (0, 1));
        forgetButton.setBounds (buttons.reduced (0, 1));

        r.removeFromRight (6);
        name.setBounds (r.removeFromTop (r.getHeight() / 2));
        status.setBounds (r);
    }

private:
    juce::Label name, status;
    juce::TextButton connectButton, forgetButton { "Forget" };
};

//==============================================================================
/** A MIDI output: whether it's multi-part, and the channel Purrlude sends to it on. */
class MidiDevicesPanel::OutputRow final : public juce::Component
{
public:
    OutputRow (MidiDeviceSettings& settings, const juce::String& deviceName)
    {
        name.setText (deviceName, juce::dontSendNotification);
        name.setFont (juce::FontOptions (14.0f));
        name.setMinimumHorizontalScale (0.7f);

        for (int channel = 1; channel <= 16; ++channel)
            channelBox.addItem ("Ch " + juce::String (channel), channel);

        channelBox.setSelectedId (settings.getOutputChannel (deviceName), juce::dontSendNotification);
        channelBox.onChange = [&settings, deviceName, this]
        {
            settings.setOutputChannel (deviceName, channelBox.getSelectedId());
        };

        multiButton.setClickingTogglesState (true);
        multiButton.setToggleState (settings.isMultitimbral (deviceName), juce::dontSendNotification);
        multiButton.setTooltip ("Multi-part: the device plays a different sound on each channel, like a sound module "
                                "or a software synth such as FluidSynth. Each track playing through it gets a channel "
                                "of its own, and General MIDI sounds to choose from.");
        multiButton.onClick = [&settings, deviceName, this]
        {
            settings.setMultitimbral (deviceName, multiButton.getToggleState());
            updateChannelBox();
        };

        updateChannelBox();
        addAndMakeVisible (name);
        addAndMakeVisible (multiButton);
        addAndMakeVisible (channelBox);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 3);
        channelBox.setBounds (r.removeFromRight (80));
        multiButton.setBounds (r.removeFromRight (64).withTrimmedRight (6));
        name.setBounds (r.withTrimmedRight (6));
    }

private:
    void updateChannelBox()
    {
        // A multi-part device's tracks each have their own channel.
        auto multi = multiButton.getToggleState();
        channelBox.setEnabled (! multi);
        channelBox.setTooltip (multi ? "Each track playing through this device has its own channel: see the track's menu"
                                     : "The channel this device plays its own sounds on");
    }

    juce::Label name;
    juce::TextButton multiButton { "Multi" };
    juce::ComboBox channelBox;
};

//==============================================================================
MidiDevicesPanel::MidiDevicesPanel (BluetoothMidiManager& b, tracktion::Engine& e, MidiDeviceSettings& s)
    : bluetooth (b), engine (e), deviceSettings (s)
{
    outputsHint.setFont (juce::FontOptions (12.0f));
    outputsHint.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    outputsHint.setJustificationType (juce::Justification::topLeft);
    outputsHint.setText ("To hear a keyboard's own sounds, choose it as the track's Sound. Notes go out on the "
                         "channel set here (a Roland GO:KEYS plays channel 4). If notes sound twice, turn the "
                         "keyboard's Local Control off. Turn on Multi for a sound module or software synth that "
                         "plays a different sound on each channel.",
                         juce::dontSendNotification);
    addAndMakeVisible (outputsHint);

    for (auto* heading : { &bluetoothHeading, &inputsHeading, &outputsHeading })
    {
        heading->setFont (juce::FontOptions (18.0f, juce::Font::bold));
        addAndMakeVisible (*heading);
    }

    scanButton.onClick = [this] { bluetooth.setUserScanning (! bluetooth.isUserScanning()); };
    addAndMakeVisible (scanButton);

    statusLabel.setFont (juce::FontOptions (13.0f));
    statusLabel.setJustificationType (juce::Justification::topLeft);
    statusLabel.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (statusLabel);

    emptyLabel.setFont (juce::FontOptions (13.0f));
    emptyLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    emptyLabel.setText ("No devices yet. Turn on your keyboard's Bluetooth MIDI and press Scan. "
                        "Pairing mode isn't needed: it's for Bluetooth audio.",
                        juce::dontSendNotification);
    emptyLabel.setJustificationType (juce::Justification::topLeft);
    addChildComponent (emptyLabel);

    inputsLabel.setFont (juce::FontOptions (13.0f));
    inputsLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (inputsLabel);

    bluetooth.addListener (this);
    engine.getDeviceManager().addChangeListener (this);
    refresh();
}

MidiDevicesPanel::~MidiDevicesPanel()
{
    engine.getDeviceManager().removeChangeListener (this);
    bluetooth.removeListener (this);
}

juce::String MidiDevicesPanel::getStatusText() const
{
    if (auto error = bluetooth.getLastError(); error.isNotEmpty())
        return error;

    if (! bluetooth.isAvailable())
        return "Bluetooth is unavailable on this computer, or turned off.";

    return bluetooth.isScanning() ? "Searching for Bluetooth MIDI devices..." : juce::String();
}

void MidiDevicesPanel::refresh()
{
    scanButton.setButtonText (bluetooth.isUserScanning() ? "Stop" : "Scan");
    scanButton.setEnabled (bluetooth.isAvailable() || bluetooth.isUserScanning());

    auto error = bluetooth.getLastError();
    statusLabel.setText (getStatusText(), juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, error.isNotEmpty() ? juce::Colour (0xffffb000)  // amber
                                                                         : juce::Colours::lightgrey);

    rows.clear();

    for (const auto& device : bluetooth.getDevices())
        addAndMakeVisible (rows.add (new DeviceRow (bluetooth, device)));

    emptyLabel.setVisible (rows.isEmpty());

    juce::StringArray inputs;

    for (auto& device : engine.getDeviceManager().getMidiInDevices())
        if (device->getDeviceType() == tracktion::InputDevice::physicalMidiDevice && device->isEnabled())
            inputs.add (device->getName());

    inputsLabel.setText (inputs.isEmpty() ? juce::String ("No MIDI keyboards connected.")
                                          : inputs.joinIntoString ("\n"),
                         juce::dontSendNotification);

    juce::StringArray outputs;
    auto& devices = engine.getDeviceManager();

    for (int i = 0; i < devices.getNumMidiOutDevices(); ++i)
        if (auto* device = devices.getMidiOutDevice (i))
            outputs.add (device->getName());

    // Rebuilding would close an open channel menu, so only do it when the list changes.
    if (outputs != outputNames)
    {
        outputNames = outputs;
        outputRows.clear();

        for (const auto& name : outputNames)
            addAndMakeVisible (outputRows.add (new OutputRow (deviceSettings, name)));
    }

    resized();
}

void MidiDevicesPanel::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId).brighter (0.06f));

    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.drawVerticalLine (0, 0.0f, (float) getHeight());
}

void MidiDevicesPanel::resized()
{
    auto r = getLocalBounds().reduced (margin);

    auto header = r.removeFromTop (28);
    scanButton.setBounds (header.removeFromRight (70));
    bluetoothHeading.setBounds (header);

    r.removeFromTop (4);
    statusLabel.setBounds (r.removeFromTop (statusLabel.getText().isEmpty() ? 0 : 36));

    if (rows.isEmpty())
        emptyLabel.setBounds (r.removeFromTop (60));

    for (auto* row : rows)
        row->setBounds (r.removeFromTop (rowHeight));

    r.removeFromTop (16);
    inputsHeading.setBounds (r.removeFromTop (28));
    auto lines = juce::jmax (1, juce::StringArray::fromLines (inputsLabel.getText()).size());
    inputsLabel.setBounds (r.removeFromTop (20 * lines + 4));

    r.removeFromTop (16);
    outputsHeading.setBounds (r.removeFromTop (28));

    for (auto* row : outputRows)
        row->setBounds (r.removeFromTop (32));

    r.removeFromTop (4);
    outputsHint.setBounds (r.removeFromTop (96));
}

} // namespace purrlude
