#include "TransportBar.h"

#include "model/AudioExport.h"
#include "model/Automation.h"
#include "model/LoopRange.h"
#include "model/Takes.h"
#include "model/TimeSignature.h"

namespace purrlude
{

namespace
{
    const auto recordColour = juce::Colour (0xffd55e00);   // vermillion: colourblind-safe red
    const auto loopColour   = juce::Colour (0xffe69f00);   // orange
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

    loopButton.setClickingTogglesState (false);
    loopButton.setTooltip ("Loop (L): playback repeats the loop range, the orange band on the ruler. "
                           "Drag along the top of the ruler to set it.");
    loopButton.setColour (juce::TextButton::buttonOnColourId, loopColour);
    loopButton.onClick = [this] { toggleLoop(); };

    punchButton.setClickingTogglesState (false);
    punchButton.setTooltip ("Punch in/out (P): Record plays from a bar before the range on the ruler, records only "
                            "inside it, replacing what was there, and stops a bar after it. With Loop on too, "
                            "each pass round the range becomes a take.");
    punchButton.setColour (juce::TextButton::buttonOnColourId, recordColour);
    punchButton.onClick = [this] { togglePunch(); };

    automateButton.setClickingTogglesState (false);
    automateButton.setTooltip ("Record automation (A): while it's on, moving a control as the song plays "
                               "(a fader, a knob, a plugin's control, or a knob on your MIDI controller) records "
                               "it into the control's automation. Let go, and it follows what was there again.");
    automateButton.setColour (juce::TextButton::buttonOnColourId, recordColour);
    automateButton.onClick = [this] { toggleAutomationRecording(); };

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
    tempoLabel.setTooltip ("Tempo: click to type a new value");
    tempoLabel.setColour (juce::Label::outlineColourId, juce::Colours::grey);
    tempoLabel.onTextChange = [this] { setTempo (tempoLabel.getText().getDoubleValue()); };

    timeSignatureButton.setTooltip ("Time signature: click to change it. The music stays where it is; only the bars change.");
    timeSignatureButton.setWantsKeyboardFocus (false);
    timeSignatureButton.onClick = [this] { showTimeSignatureMenu(); };

    positionLabel.setFont (juce::FontOptions (18.0f));
    positionLabel.setJustificationType (juce::Justification::centred);

    for (auto* c : std::initializer_list<juce::Component*> { &startButton, &playButton, &recordButton, &loopButton, &punchButton, &automateButton,
                                                            &metronomeButton, &countInButton,
                                                            &tempoLabel, &tempoUnitLabel, &timeSignatureButton, &positionLabel })
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
    {
        transport.stop (false, false);
    }
    else
    {
        // From the end of the song, play starts over; looping, from outside the loop, at its start.
        auto song = AudioExport::getSongRange (edit);

        if (loop_range::isOn (edit))
            transport.setPosition (loop_range::getPlayStart (edit));
        else if (! song.isEmpty() && transport.getPosition() >= song.getEnd())
            transport.setPosition (tracktion::TimePosition());

        transport.play (false);
    }

    updateButtons();
}

void TransportBar::stopAtEndOfSong()
{
    auto& transport = edit.getTransport();

    // Recording may go past what's there, and a loop goes round until stopped; only
    // playback stops by itself.
    if (! transport.isPlaying() || transport.isRecording() || loop_range::isOn (edit))
        return;

    // A moment after the last clip ends, for notes and effects to ring out.
    auto song = AudioExport::getSongRange (edit);

    if (! song.isEmpty() && transport.getPosition() >= song.getEnd() + tracktion::TimeDuration::fromSeconds (endTailSeconds))
        transport.stop (false, false);
}

void TransportBar::toggleRecord()
{
    auto& transport = edit.getTransport();

    if (transport.isRecording())
    {
        transport.stop (false, false);   // keeps the take; it becomes a clip
    }
    else
    {
        // Looping, each pass becomes a take; recording starts at the loop's start, where the
        // passes are counted from.
        takes::prepareMidiInputs (edit);

        // Punching, it starts a little before the punch-in, to play along into it.
        transport.setPosition (loop_range::getRecordStart (edit));
        transport.record (false);
    }

    updateButtons();
}

void TransportBar::returnToStart()
{
    edit.getTransport().setPosition (tracktion::TimePosition());
}

void TransportBar::toggleLoop()
{
    if (edit.getTransport().isRecording())
        return;

    loop_range::setOn (edit, ! loop_range::isOn (edit));
    updateButtons();
}

void TransportBar::togglePunch()
{
    if (edit.getTransport().isRecording())
        return;

    loop_range::setPunchOn (edit, ! loop_range::isPunchOn (edit));
    updateButtons();
}

void TransportBar::toggleAutomationRecording()
{
    automation::setWriting (edit, ! automation::isWriting (edit));
    updateButtons();
}

void TransportBar::setTempo (double bpm)
{
    if (bpm >= 20.0 && bpm <= 400.0)
        if (auto* tempo = edit.tempoSequence.getTempo (0))
            tempo->setBpm (bpm);

    updateButtons();
}

void TransportBar::showTimeSignatureMenu()
{
    static const std::array<std::pair<int, int>, 8> common
    {{ { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 }, { 9, 8 }, { 12, 8 } }};

    auto* sig = edit.tempoSequence.getTimeSig (0);
    juce::PopupMenu menu;

    for (size_t i = 0; i < common.size(); ++i)
    {
        auto [numerator, denominator] = common[i];
        menu.addItem ((int) i + 1, juce::String (numerator) + "/" + juce::String (denominator), true,
                      sig->numerator.get() == numerator && sig->denominator.get() == denominator);
    }

    menu.addSeparator();
    menu.addItem (100, "Other...");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&timeSignatureButton),
                        [safe = juce::Component::SafePointer<TransportBar> (this)] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        if (result == 100)
            safe->askForTimeSignature();
        else
            changeTimeSignature (safe->edit, common[(size_t) result - 1].first, common[(size_t) result - 1].second);

        safe->updateButtons();
    });
}

void TransportBar::askForTimeSignature()
{
    auto* sig = edit.tempoSequence.getTimeSig (0);

    timeSignatureWindow = std::make_unique<juce::AlertWindow> ("Time signature", "How many beats in a bar, and which note gets the beat?",
                                                               juce::MessageBoxIconType::NoIcon);
    timeSignatureWindow->addTextEditor ("beats", juce::String (sig->numerator.get()), "Beats per bar (1-32)");
    timeSignatureWindow->addComboBox ("unit", { "2 (half note)", "4 (quarter note)", "8 (eighth note)", "16 (sixteenth note)" }, "Beat");

    auto* unit = timeSignatureWindow->getComboBoxComponent ("unit");
    unit->setSelectedItemIndex (juce::jmax (0, juce::StringArray { "2", "4", "8", "16" }.indexOf (juce::String (sig->denominator.get()))));

    timeSignatureWindow->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    timeSignatureWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    timeSignatureWindow->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<TransportBar> (this)] (int result)
    {
        if (safe == nullptr || safe->timeSignatureWindow == nullptr)
            return;

        if (result == 1)
        {
            auto beats = safe->timeSignatureWindow->getTextEditorContents ("beats").getIntValue();
            auto unitText = safe->timeSignatureWindow->getComboBoxComponent ("unit")->getText();
            changeTimeSignature (safe->edit, juce::jlimit (1, 32, beats), unitText.getIntValue());
        }

        safe->updateButtons();
    }), false);
}

void TransportBar::updateButtons()
{
    auto& transport = edit.getTransport();
    bool recording = transport.isRecording();

    playButton.setButtonText (transport.isPlaying() ? "Stop" : "Play");
    loopButton.setToggleState (loop_range::isOn (edit), juce::dontSendNotification);
    loopButton.setEnabled (! recording);
    punchButton.setToggleState (loop_range::isPunchOn (edit), juce::dontSendNotification);
    punchButton.setEnabled (! recording);
    automateButton.setToggleState (automation::isWriting (edit), juce::dontSendNotification);
    recordButton.setButtonText (recording ? "Stop Rec" : "Record");
    recordButton.setColour (juce::TextButton::buttonColourId,
                            recording ? recordColour : getLookAndFeel().findColour (juce::TextButton::buttonColourId));

    if (! tempoLabel.isBeingEdited())
        if (auto* tempo = edit.tempoSequence.getTempo (0))
            tempoLabel.setText (juce::String (tempo->getBpm(), 1), juce::dontSendNotification);

    // Changed here, by undo, or by an imported file.
    if (auto* sig = edit.tempoSequence.getTimeSig (0))
        timeSignatureButton.setButtonText (juce::String (sig->numerator.get()) + "/" + juce::String (sig->denominator.get()));

    // No changing the meter in the middle of a take.
    timeSignatureButton.setEnabled (! recording);
}

void TransportBar::timerCallback()
{
    auto& transport = edit.getTransport();

    // Loop points follow tempo changes.
    loop_range::sync (edit);

    stopAtEndOfSong();

    // A punch stops by itself a bar after the punch-out.
    if (transport.isRecording())
        if (auto stopAt = loop_range::getPunchStopTime (edit); stopAt && transport.getPosition() >= *stopAt)
            transport.stop (false, false);

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
    playButton.setBounds (r.removeFromLeft (64));       gap();
    recordButton.setBounds (r.removeFromLeft (82));     gap();
    loopButton.setBounds (r.removeFromLeft (52));       gap();
    punchButton.setBounds (r.removeFromLeft (58));      gap();
    automateButton.setBounds (r.removeFromLeft (82));   gap();
    positionLabel.setBounds (r.removeFromLeft (100));   gap();
    tempoLabel.setBounds (r.removeFromLeft (60));
    tempoUnitLabel.setBounds (r.removeFromLeft (36));   gap();
    timeSignatureButton.setBounds (r.removeFromLeft (52)); gap();
    metronomeButton.setBounds (r.removeFromLeft (102));
    countInButton.setBounds (r.removeFromLeft (90));
}

} // namespace purrlude
