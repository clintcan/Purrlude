#include "TransportBar.h"

namespace purrlude
{

namespace
{
    const auto recordColour = juce::Colour (0xffd55e00);   // vermillion: colourblind-safe red
}

TransportBar::TransportBar (tracktion::Edit& e)
    : edit (e)
{
    startButton.setTooltip ("Return to start (Home)");
    playButton.setTooltip ("Play / stop (Space)");
    recordButton.setTooltip ("Record onto armed tracks (R)");

    startButton.onClick = [this] { returnToStart(); };
    playButton.onClick = [this] { togglePlay(); };
    recordButton.onClick = [this] { toggleRecord(); };

    metronomeButton.setToggleState (edit.clickTrackEnabled.get(), juce::dontSendNotification);
    metronomeButton.onClick = [this] { edit.clickTrackEnabled = metronomeButton.getToggleState(); };

    countInButton.setToggleState (edit.getCountInMode() != tracktion::Edit::CountIn::none, juce::dontSendNotification);
    countInButton.onClick = [this]
    {
        edit.setCountInMode (countInButton.getToggleState() ? tracktion::Edit::CountIn::oneBar
                                                            : tracktion::Edit::CountIn::none);
    };

    tempoLabel.setEditable (true);
    tempoLabel.setJustificationType (juce::Justification::centredRight);
    tempoLabel.setTooltip ("Tempo: double-click to type a new value");
    tempoLabel.setColour (juce::Label::outlineColourId, juce::Colours::grey);
    tempoLabel.onTextChange = [this] { setTempo (tempoLabel.getText().getDoubleValue()); };

    positionLabel.setFont (juce::FontOptions (18.0f));
    positionLabel.setJustificationType (juce::Justification::centred);

    for (auto* c : std::initializer_list<juce::Component*> { &startButton, &playButton, &recordButton,
                                                            &metronomeButton, &countInButton,
                                                            &tempoLabel, &tempoUnitLabel, &positionLabel })
        addAndMakeVisible (c);

    updateButtons();
    startTimerHz (20);
}

TransportBar::~TransportBar()
{
    stopTimer();
}

void TransportBar::togglePlay()
{
    auto& transport = edit.getTransport();

    if (transport.isPlaying())
        transport.stop (false, false);
    else
        transport.play (false);

    updateButtons();
}

void TransportBar::toggleRecord()
{
    auto& transport = edit.getTransport();

    if (transport.isRecording())
        transport.stop (false, false);   // keeps the take; it becomes a clip
    else
        transport.record (false);

    updateButtons();
}

void TransportBar::returnToStart()
{
    edit.getTransport().setPosition (tracktion::TimePosition());
}

void TransportBar::setTempo (double bpm)
{
    if (bpm >= 20.0 && bpm <= 400.0)
        if (auto* tempo = edit.tempoSequence.getTempo (0))
            tempo->setBpm (bpm);

    updateButtons();
}

void TransportBar::updateButtons()
{
    auto& transport = edit.getTransport();
    bool recording = transport.isRecording();

    playButton.setButtonText (transport.isPlaying() ? "Stop" : "Play");
    recordButton.setButtonText (recording ? "Stop Rec" : "Record");
    recordButton.setColour (juce::TextButton::buttonColourId,
                            recording ? recordColour : getLookAndFeel().findColour (juce::TextButton::buttonColourId));

    if (! tempoLabel.isBeingEdited())
        if (auto* tempo = edit.tempoSequence.getTempo (0))
            tempoLabel.setText (juce::String (tempo->getBpm(), 1), juce::dontSendNotification);
}

void TransportBar::timerCallback()
{
    auto& transport = edit.getTransport();
    auto time = transport.getPosition();
    auto barsBeats = edit.tempoSequence.toBarsAndBeats (time);
    auto seconds = juce::jmax (0.0, time.inSeconds());

    positionLabel.setText (juce::String (barsBeats.bars + 1) + "." + juce::String ((int) barsBeats.getWholeBeats() + 1)
                             + "   " + juce::String ((int) seconds / 60) + ":" + juce::String ((int) seconds % 60).paddedLeft ('0', 2),
                           juce::dontSendNotification);

    updateButtons();
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (6);
    auto gap = [&r] { r.removeFromLeft (6); };

    startButton.setBounds (r.removeFromLeft (40));      gap();
    playButton.setBounds (r.removeFromLeft (70));       gap();
    recordButton.setBounds (r.removeFromLeft (90));     gap();
    positionLabel.setBounds (r.removeFromLeft (150));   gap();
    tempoLabel.setBounds (r.removeFromLeft (60));
    tempoUnitLabel.setBounds (r.removeFromLeft (40));   gap();
    metronomeButton.setBounds (r.removeFromLeft (110));
    countInButton.setBounds (r.removeFromLeft (100));
}

} // namespace purrlude
