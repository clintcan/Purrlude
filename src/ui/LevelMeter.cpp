#include "LevelMeter.h"

namespace purrlude
{

namespace
{
    // Okabe-Ito colours, which stay distinct with colour blindness.
    const auto lowColour  = juce::Colour (0xff009e73);   // bluish green
    const auto highColour = juce::Colour (0xfff0e442);   // yellow
    const auto clipColour = juce::Colour (0xffd55e00);   // vermillion

    constexpr float minDb = -60.0f, maxDb = 6.0f, highDb = -6.0f;
    constexpr float fallPerTick = 1.5f;   // dB per update, at 30 Hz about 45 dB/s
    constexpr int peakHold = 45;          // updates (1.5 s)
}

LevelMeter::LevelMeter (Source s, bool h)
    : source (std::move (s)), horizontal (h)
{
    setTooltip ("Level. A vermillion light means it went over 0 dB: click to clear.");
    startTimerHz (30);
}

LevelMeter::~LevelMeter()
{
    stopTimer();
    attach (nullptr);
}

float LevelMeter::dbToProportion (float dB)
{
    return juce::jlimit (0.0f, 1.0f, (dB - minDb) / (maxDb - minDb));
}

void LevelMeter::attach (tracktion::LevelMeasurer* measurer)
{
    if (measurer == attached.get())
        return;

    // A measurer that has gone has already forgotten its clients.
    if (auto* previous = attached.get())
        previous->removeClient (client);

    attached = measurer;

    if (measurer != nullptr)
        measurer->addClient (client);
}

void LevelMeter::timerCallback()
{
    attach (source != nullptr ? source() : nullptr);

    auto changed = false;
    auto stereo = attached != nullptr && client.getNumChannelsUsed() > 1;

    for (int channel = 0; channel < 2; ++channel)
    {
        auto incoming = -100.0f;

        if (attached != nullptr)
            incoming = client.getAndClearAudioLevel (stereo ? channel : 0).dB;

        // Rise at once, fall gradually.
        auto level = juce::jmax (incoming, levels[channel] - fallPerTick);

        if (incoming >= peaks[channel])
        {
            peaks[channel] = incoming;
            peakHoldTicks[channel] = peakHold;
        }
        else if (--peakHoldTicks[channel] < 0)
        {
            peaks[channel] = juce::jmax (level, peaks[channel] - fallPerTick);
        }

        if (incoming > 0.0f)
            clipped = true;

        changed = changed || std::abs (level - levels[channel]) > 0.01f || peakHoldTicks[channel] >= 0;
        levels[channel] = level;
    }

    if (changed)
        repaint();
}

void LevelMeter::mouseDown (const juce::MouseEvent&)
{
    clipped = false;
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (bounds, 2.0f);

    // The clip light takes the far end.
    auto lightSize = 4.0f;
    auto light = horizontal ? bounds.removeFromRight (lightSize) : bounds.removeFromTop (lightSize);
    g.setColour (clipped ? clipColour : juce::Colours::white.withAlpha (0.08f));
    g.fillRect (light.reduced (0.5f));

    bounds = bounds.reduced (1.0f);
    auto gap = 1.0f;
    auto across = (horizontal ? bounds.getHeight() : bounds.getWidth()) - gap;

    for (int channel = 0; channel < 2; ++channel)
    {
        auto lane = horizontal ? juce::Rectangle<float> (bounds.getX(), bounds.getY() + channel * (across / 2 + gap), bounds.getWidth(), across / 2)
                               : juce::Rectangle<float> (bounds.getX() + channel * (across / 2 + gap), bounds.getY(), across / 2, bounds.getHeight());
        auto length = horizontal ? lane.getWidth() : lane.getHeight();

        // The part of the lane from the bottom (or left) up to a level.
        auto upTo = [&] (float dB)
        {
            auto extent = dbToProportion (dB) * length;
            return horizontal ? lane.withWidth (extent) : lane.withTop (lane.getBottom() - extent);
        };

        auto bar = upTo (levels[channel]);
        auto highStart = upTo (highDb);

        g.setColour (lowColour);
        g.fillRect (bar.getIntersection (highStart));

        // Above -6 dB the bar turns yellow, above 0 dB vermillion.
        if (levels[channel] > highDb)
        {
            g.setColour (highColour);
            g.fillRect (horizontal ? bar.withLeft (highStart.getRight()) : bar.withBottom (highStart.getY()));
        }

        if (levels[channel] > 0.0f)
        {
            auto zero = upTo (0.0f);
            g.setColour (clipColour);
            g.fillRect (horizontal ? bar.withLeft (zero.getRight()) : bar.withBottom (zero.getY()));
        }

        // Peak hold.
        if (peaks[channel] > minDb)
        {
            auto peak = upTo (peaks[channel]);
            g.setColour (juce::Colours::white.withAlpha (0.8f));

            if (horizontal)
                g.fillRect (peak.getRight() - 1.0f, lane.getY(), 1.5f, lane.getHeight());
            else
                g.fillRect (lane.getX(), peak.getY(), lane.getWidth(), 1.5f);
        }
    }

    // A tick at 0 dB.
    auto zero = dbToProportion (0.0f);
    g.setColour (juce::Colours::white.withAlpha (0.35f));

    if (horizontal)
        g.fillRect (bounds.getX() + zero * bounds.getWidth(), bounds.getY(), 1.0f, bounds.getHeight());
    else
        g.fillRect (bounds.getX(), bounds.getBottom() - zero * bounds.getHeight(), bounds.getWidth(), 1.0f);
}

} // namespace purrlude
