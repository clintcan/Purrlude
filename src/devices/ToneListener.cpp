#include "ToneListener.h"

namespace purrlude
{

std::optional<DeviceTone> ToneParser::handle (const juce::MidiMessage& m)
{
    auto channel = m.getChannel() - 1;

    if (channel < 0)
        return std::nullopt;

    if (m.isController() && m.getControllerNumber() == 0)
        msb[(size_t) channel] = m.getControllerValue();
    else if (m.isController() && m.getControllerNumber() == 32)
        lsb[(size_t) channel] = m.getControllerValue();
    else if (m.isProgramChange())
        return DeviceTone { msb[(size_t) channel], lsb[(size_t) channel], m.getProgramChangeNumber(), {} };

    return std::nullopt;
}

//==============================================================================
ToneListener::Connection::Connection (juce::WeakReference<ToneListener> o, const juce::String& n)
    : owner (std::move (o)), name (n)
{
}

void ToneListener::Connection::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    // MIDI thread: parse, copy plain data, hand it to the message thread.
    if (auto tone = parser.handle (m))
    {
        juce::MessageManager::callAsync ([o = owner, deviceName = name, t = *tone]
        {
            if (auto* listener = o.get())
                listener->handleTone (deviceName, t);
        });
    }
}

//==============================================================================
ToneListener::ToneListener (MidiDeviceSettings& s)
    : settings (s)
{
    refreshDevices();
}

ToneListener::~ToneListener()
{
    for (auto& c : connections)
        c->input->stop();
}

void ToneListener::refreshDevices()
{
    auto available = juce::MidiInput::getAvailableDevices();

    if (shouldSkip != nullptr)
        available.removeIf ([this] (const juce::MidiDeviceInfo& d) { return shouldSkip (d.name); });

    // Drop connections to devices that went away.
    for (auto it = connections.begin(); it != connections.end();)
    {
        bool stillThere = std::any_of (available.begin(), available.end(),
                                       [&] (const juce::MidiDeviceInfo& d) { return d.name == (*it)->name; });

        if (stillThere)
        {
            ++it;
        }
        else
        {
            (*it)->input->stop();
            it = connections.erase (it);
        }
    }

    for (const auto& info : available)
    {
        bool connected = std::any_of (connections.begin(), connections.end(),
                                      [&] (const auto& c) { return c->name == info.name; });

        if (connected)
            continue;

        auto connection = std::make_unique<Connection> (juce::WeakReference<ToneListener> (this), info.name);
        connection->input = juce::MidiInput::openDevice (info.identifier, connection.get());

        if (connection->input != nullptr)
        {
            connection->input->start();
            connections.push_back (std::move (connection));
        }
    }
}

void ToneListener::handleTone (const juce::String& deviceName, DeviceTone tone)
{
    auto learned = settings.learnTone (deviceName, tone);

    if (onToneSelected != nullptr)
        onToneSelected (deviceName, learned);
}

} // namespace purrlude
