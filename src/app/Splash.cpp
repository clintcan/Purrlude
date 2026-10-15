#include "Splash.h"

#include <PurrludeBranding.h>

namespace purrlude
{

Splash::Splash (const juce::String& v)
    : logo (getLogo()), version (v)
{
    setOpaque (false);   // rounded corners
    setAlwaysOnTop (true);
    centreWithSize (logoSize, logoSize + textHeight);
    addToDesktop (juce::ComponentPeer::windowHasDropShadow | juce::ComponentPeer::windowIsTemporary);
    setVisible (true);
    toFront (false);

    // The app is busy starting up and the message loop isn't running yet: draw now, so the
    // splash isn't an empty window until it's done.
    if (auto* peer = getPeer())
        peer->performAnyPendingRepaintsNow();

    shownAt = juce::Time::getMillisecondCounter();
}

Splash::~Splash()
{
    stopTimer();
}

void Splash::dismiss (int minimumMs, std::function<void()> callback)
{
    // A click while starting up has already set closeAt: keep it.
    closeAt = juce::jmin (closeAt, shownAt + (juce::uint32) juce::jmax (0, minimumMs));
    onGone = std::move (callback);
    startTimer (30);
}

void Splash::timerCallback()
{
    if (juce::Time::getMillisecondCounter() < closeAt || ! isVisible())
        return;

    stopTimer();
    juce::Desktop::getInstance().getAnimator().fadeOut (this, 300);
    setVisible (false);

    if (auto callback = std::exchange (onGone, nullptr))
        callback();
}

void Splash::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (getBackgroundColour());
    g.fillRoundedRectangle (bounds, 14.0f);

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (logo, bounds.removeFromTop ((float) logoSize), juce::RectanglePlacement::centred);

    auto text = bounds.reduced (16.0f, 0.0f);
    g.setColour (juce::Colours::white.withAlpha (0.75f));
    g.setFont (juce::FontOptions (14.0f));
    g.drawText ("Write, record, arrange and mix, and read it as sheet music",
                text.removeFromTop (22.0f), juce::Justification::centred);

    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Version " + version + juce::String::fromUTF8 (" \xc2\xb7 Free and open source"),
                text.removeFromTop (20.0f), juce::Justification::centred);
}

juce::Image Splash::getLogo()
{
    return juce::ImageCache::getFromMemory (PurrludeBranding::purrludelogo_jpg, PurrludeBranding::purrludelogo_jpgSize);
}

} // namespace purrlude
