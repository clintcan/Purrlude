#include "DocScreenshots.h"

#include "MainComponent.h"

#include "model/Accompaniment.h"
#include "model/AudioImport.h"
#include "model/Automation.h"
#include "model/LoopLibrary.h"
#include "model/LoopRange.h"
#include "model/NoteEditing.h"
#include "ui/PluginWindows.h"
#include "model/SharedReverb.h"
#include "model/TrackEffects.h"
#include "model/TrackInstrument.h"

namespace purrlude
{

namespace
{
    constexpr float imageScale = 2.0f;   // sharp on high-resolution screens

    struct MelodyNote
    {
        int pitch;
        double beats;
    };

    // Eight bars over I–V–vi–IV in C, twice.
    const MelodyNote melody[] = {
        { 72, 1 }, { 71, 0.5 }, { 69, 0.5 }, { 67, 2 },
        { 67, 1 }, { 69, 0.5 }, { 71, 0.5 }, { 74, 2 },
        { 72, 1 }, { 71, 0.5 }, { 72, 0.5 }, { 76, 1.5 }, { 74, 0.5 },
        { 72, 3 }, { 0, 1 },
        { 76, 1 }, { 74, 0.5 }, { 72, 0.5 }, { 79, 2 },
        { 79, 1 }, { 77, 0.5 }, { 76, 0.5 }, { 74, 2 },
        { 72, 1 }, { 74, 1 }, { 76, 1 }, { 74, 0.5 }, { 72, 0.5 },
        { 72, 4 },
    };

    constexpr double tempoBpm = 100.0;
    constexpr int hummedNotes = 14;   // the first four bars
}

DocScreenshots::DocScreenshots (MainComponent& m, juce::File f)
    : main (m), folder (std::move (f))
{
    folder.createDirectory();

    // Start from a clean slate: no recovery prompt from an earlier run. This only ever runs
    // with its own settings folder (Main.cpp), never the user's.
    auto autosaves = main.getAutosaveDirectory();

    if (autosaves.getFullPathName().contains ("Doc Screenshots"))
        autosaves.deleteRecursively();

    addSteps();
    startTimer (2000);   // let the window appear
}

DocScreenshots::~DocScreenshots()
{
    stopTimer();
}

void DocScreenshots::then (int waitMs, std::function<void()> action)
{
    steps.push_back ({ waitMs, std::move (action) });
}

void DocScreenshots::timerCallback()
{
    stopTimer();

    if (next >= steps.size())
    {
        DBG ("Doc screenshots: done, " << failures << " problems");
        juce::JUCEApplication::getInstance()->quit();
        return;
    }

    // Pop-up menus close when the app isn't in front.
    juce::Process::makeForegroundProcess();
    steps[next++].action();
    startTimer (next < steps.size() ? juce::jmax (1, steps[next].waitMs) : 500);
}

//==============================================================================
void DocScreenshots::addSteps()
{
    auto getEdit = [this]() -> tracktion::Edit& { return main.project->getEdit(); };
    auto trackAt = [getEdit] (int i) { return getSongTracks (getEdit())[i]; };

    auto headerFor = [this, trackAt] (int i) -> TrackHeader*
    {
        auto* track = trackAt (i);

        for (int n = 0;; ++n)
        {
            auto* header = findChild<TrackHeader> (*main.arrangement, n);

            if (header == nullptr || (track != nullptr && header->getTrackID() == track->itemID))
                return header;
        }
    };

    auto scrollArrangement = [this] (bool toBottom)
    {
        main.arrangementViewport.setViewPosition (0, toBottom ? 100000 : 0);
    };

    auto showEditors = [this] (bool show, int tab)
    {
        main.editorsButton.setToggleState (show, juce::dontSendNotification);
        main.editorTabs.setVisible (show);

        if (tab >= 0)
            main.editorTabs.setCurrentTabIndex (tab);

        main.resized();
    };

    auto clipOn = [trackAt] (int i) -> tracktion::Clip*
    {
        auto* track = trackAt (i);
        return track != nullptr && ! track->getClips().isEmpty() ? track->getClips()[0] : nullptr;
    };

    // The demo song, in the window size the guide shows.
    then (0, [this, showEditors]
    {
        main.setSize (1440, 900);

        if (auto* window = main.getTopLevelComponent())
            window->centreWithSize (window->getWidth(), window->getHeight());

        buildDemoSong();
        main.showSidePanel (main.loopsPanel.get());
        showEditors (true, 0);
    });

    // The whole window, with the score engraved.
    then (6000, [this] { capture ("overview"); });
    then (0, [this] { capture ("transport-bar", { 0, 0, main.getWidth(), 44 }); });

    // The arrangement on its own, looping the first four bars.
    then (0, [this, showEditors, getEdit]
    {
        main.showSidePanel (nullptr);
        showEditors (false, -1);
        loop_range::set (getEdit(), { tracktion::BeatPosition(), tracktion::BeatPosition::fromBeats (16.0) });
        loop_range::setOn (getEdit(), true);
    });

    then (1500, [this] { capture ("arrangement"); });

    // Automation: the melody fades in over two bars, holds, and dips at the end.
    then (0, [getEdit, trackAt]
    {
        auto& edit = getEdit();
        auto* melody = trackAt (0);
        auto targets = automation::getTargets (*melody);

        if (targets.empty())
            return;

        auto& volume = *automation::findParameter (*melody, targets[0]);
        auto at = [&] (double beat) { return edit.tempoSequence.toTime (tracktion::BeatPosition::fromBeats (beat)); };
        auto db = [] (float decibels) { return tracktion::decibelsToVolumeFaderPosition (decibels); };

        automation::addPoint (edit, volume, at (0.0), db (-30.0f));
        automation::addPoint (edit, volume, at (8.0), db (0.0f));
        automation::addPoint (edit, volume, at (24.0), db (0.0f));
        automation::addPoint (edit, volume, at (28.0), db (-9.0f));
        automation::addPoint (edit, volume, at (32.0), db (-3.0f));
        automation::setShown (*melody, targets[0]);
    });

    then (1200, [this, headerFor]
    {
        if (auto* header = headerFor (0))
            capture ("automation", boundsInMain (*header).withRight (main.getWidth()).withTop (boundsInMain (*header).getY() - 40).expanded (0, 6));
    });

    then (0, [getEdit, trackAt]
    {
        auto* melody = trackAt (0);

        if (auto targets = automation::getTargets (*melody); ! targets.empty())
        {
            automation::clear (getEdit(), *automation::findParameter (*melody, targets[0]));
            automation::setShown (*melody, std::nullopt);
        }
    });

    then (0, [this]
    {
        auto viewport = boundsInMain (main.arrangementViewport);
        capture ("ruler", viewport.withHeight (36));
        capture ("zoom-bar", boundsInMain (main.arrangement->getZoomBar()).expanded (0, 4));
    });

    then (0, [this, headerFor]
    {
        if (auto* header = headerFor (0))
            capture ("track-header", boundsInMain (*header).expanded (4));
    });

    // Menus: track header, a MIDI clip, + Track.
    then (0, [this, headerFor] { if (auto* header = headerFor (0)) rightClick (*header, { 80, 60 }); });
    then (800, [this, headerFor] { if (auto* header = headerFor (0)) capture ("track-menu", around (*header)); closePopups(); });

    then (300, [this, clipOn]
    {
        if (auto* clip = clipOn (0))
            rightClick (*main.arrangement, main.arrangement->getClipBounds (clip->itemID).getCentre().withX (main.arrangement->getClipBounds (clip->itemID).getX() + 120));
    });

    then (800, [this] { capture ("clip-menu-midi", popupArea().expanded (220, 30).withX (juce::jmax (0, popupArea().getX() - 320))); closePopups(); });

    // Lower down: the audio track and + Track.
    then (300, [scrollArrangement] { scrollArrangement (true); });

    then (800, [this, headerFor]
    {
        if (auto* header = headerFor (4))
            capture ("audio-track-header", boundsInMain (*header).expanded (4));
    });

    then (0, [this, clipOn]
    {
        if (auto* clip = clipOn (4))
        {
            auto bounds = main.arrangement->getClipBounds (clip->itemID);
            rightClick (*main.arrangement, { bounds.getX() + 60, bounds.getY() + 30 });
        }
    });

    then (800, [this]
    {
        capture ("clip-menu-audio", popupArea().expanded (220, 30).withX (juce::jmax (0, popupArea().getX() - 320)));
        closePopups();
    });

    then (300, [this]
    {
        if (auto* button = findButton (*main.arrangement, "+ Track"))
            button->triggerClick();
    });

    then (800, [this]
    {
        if (auto* button = findButton (*main.arrangement, "+ Track"))
            capture ("add-track-menu", around (*button, 40));

        closePopups();
    });

    // The time signature menu.
    then (300, [this]
    {
        if (auto* button = findButton (*main.transportBar, "4/4"))
            button->triggerClick();
    });

    then (800, [this]
    {
        if (auto* button = findButton (*main.transportBar, "4/4"))
            capture ("time-signature-menu", around (*button, 30));

        closePopups();
    });

    // Effects on the melody, and the shared reverb.
    then (300, [scrollArrangement] { scrollArrangement (false); });

    then (500, [this, headerFor]
    {
        if (auto* header = headerFor (0))
            if (auto* fx = findButton (*header, "FX"))
                fx->triggerClick();
    });

    then (1200, [this, headerFor]
    {
        if (auto* header = headerFor (0))
            capture ("effects", around (*header, 20));

        for (auto* popup : getPopups())
            if (auto* button = findButton (*popup, "Reverb Settings..."))
            {
                button->triggerClick();
                break;
            }
    });

    then (1200, [this, headerFor]
    {
        if (auto* header = headerFor (0))
            capture ("reverb-settings", around (*header, 20));

        closePopups();
    });

    // The piano roll and the score.
    then (500, [this, showEditors, clipOn]
    {
        showEditors (true, 1);

        if (auto* clip = clipOn (0); clip != nullptr && main.arrangement->onClipDoubleClicked != nullptr)
            main.arrangement->onClipDoubleClicked (clip->itemID);
    });

    then (1500, [this] { capture ("piano-roll"); });

    // Playing, silently: the notes under the playhead light up.
    then (0, [this]
    {
        auto& edit = main.project->getEdit();

        if (auto master = edit.getMasterVolumePlugin())
        {
            savedMasterDb = master->getVolumeDb();
            master->setVolumeDb (-100.0f);
        }

        edit.getTransport().setPosition (edit.tempoSequence.toTime (tracktion::BeatPosition::fromBeats (9.1)));
        edit.getTransport().play (false);
    });

    then (300, [this] { capture ("piano-roll-playing"); });

    then (0, [this]
    {
        auto& edit = main.project->getEdit();
        edit.getTransport().stop (false, false);
        edit.getTransport().setPosition (edit.tempoSequence.toTime (tracktion::BeatPosition::fromBeats (10.0)));

        if (auto master = edit.getMasterVolumePlugin())
            master->setVolumeDb (savedMasterDb);
    });
    then (0, [showEditors] { showEditors (true, 0); });
    then (5000, [this] { capture ("score"); });
    then (0, [this] { main.setEditorsFullHeight (true); });
    then (2000, [this] { capture ("score-full-height"); });
    then (0, [this] { main.setEditorsFullHeight (false); });

    // Side panels.
    then (0, [this]
    {
        main.showSidePanel (main.loopsPanel.get());

        if (auto* list = findChild<juce::ListBox> (*main.loopsPanel))
            list->selectRow (1);
    });

    then (1000, [this] { capture ("loops-drums", boundsInMain (*main.loopsPanel)); });

    then (0, [this]
    {
        if (auto* button = findButton (*main.loopsPanel, "Chords"))
            button->triggerClick();
    });

    then (500, [this]
    {
        if (auto* list = findChild<juce::ListBox> (*main.loopsPanel))
            list->selectRow (0);
    });

    then (800, [this] { capture ("loops-chords", boundsInMain (*main.loopsPanel)); });
    then (0, [this] { main.showSidePanel (&main.devicesPanel); });
    then (1000, [this] { capture ("midi-devices", boundsInMain (main.devicesPanel).withHeight (330)); });

    // Dialogs.
    then (0, [this] { main.exportAudio(); });

    then (1000, [this]
    {
        capture ("export-audio", popupArea());
        closePopups();
    });

    then (300, [this] { main.showAudioSettings(); });

    then (1500, [this]
    {
        capture ("audio-settings", popupArea());
        closePopups();
    });

    then (300, [this] { plugin_windows::showPluginList (main.engine); });

    then (1500, [this]
    {
        capture ("plugins", popupArea());
        closePopups();
    });

    // Converting the hummed melody to MIDI.
    then (300, [this, showEditors, scrollArrangement, clipOn]
    {
        main.showSidePanel (nullptr);
        showEditors (false, -1);
        scrollArrangement (true);

        if (auto* clip = clipOn (4))
            main.convertToMidi (clip->itemID, audio_to_midi::Mode::melody);
    });

    then (150, [this]
    {
        if (! popupArea().isEmpty())
            capture ("converting", popupArea());
    });

    then (8000, [scrollArrangement] { scrollArrangement (true); });
    then (1000, [this] { capture ("converted"); });
}

//==============================================================================
void DocScreenshots::buildDemoSong()
{
    auto& edit = main.project->getEdit();
    auto& live = main.project->getInstrument();
    auto& tempo = edit.tempoSequence;
    tempo.getTempo (0)->setBpm (tempoBpm);

    // The melody, on the song's first track.
    auto* lead = getSongTracks (edit)[0];
    lead->setName ("Melody");
    TrackInstrument (edit, *lead).setProgram (73);   // Flute

    if (auto clip = lead->insertMIDIClip ("Melody", { tempo.toTime (tracktion::BeatPosition()),
                                                      tempo.toTime (tracktion::BeatPosition::fromBeats (32.0)) }, nullptr))
    {
        double beat = 0.0;

        for (auto& note : melody)
        {
            if (note.pitch > 0)
                notes::add (*clip, note.pitch, beat, note.beats, 96, &edit.getUndoManager());

            beat += note.beats;
        }
    }

    // Chords, bass and drums from the Loops panel's content.
    accompaniment::Key key;   // C major
    const accompaniment::Progression* progression = nullptr;

    for (auto& p : accompaniment::getProgressions())
        if (p.id == "pop")
            progression = &p;

    for (auto* id : { "chords-pop-eighths", "bass-pop-eighths" })
        for (auto& pattern : accompaniment::getPatterns())
            if (progression != nullptr && pattern.id == id)
                accompaniment::insert (edit, live, pattern, *progression, key, 0, 8);

    for (auto& loop : LoopLibrary::getAll())
        if (loop.style == "Pop")
        {
            LoopLibrary::insertLoop (edit, live, loop, 0, 8);
            break;
        }

    tempo.getTempo (0)->setBpm (tempoBpm);   // the loop may have brought its own

    // Some reverb and an echo on the melody.
    if (auto* send = shared_reverb::getOrCreateSend (edit, *lead))
        send->setGainDb (-12.0f);

    track_effects::add (edit, *lead, track_effects::Kind::delay);
    track_effects::add (edit, *lead, track_effects::Kind::eq);

    // A hummed idea on an audio track, after the band.
    auto hummed = writeHummedMelody (folder.getParentDirectory().getChildFile ("Hummed Idea.wav"));
    auto imported = audio_import::importFile (edit, live, hummed, 8, main.project->getAudioFolder());
    hummed.deleteFile();

    if (imported.error.isNotEmpty())
    {
        DBG ("Doc screenshots: " << imported.error);
        ++failures;
    }

    live.selectTrack (lead->itemID);
    edit.getTransport().setPosition (tempo.toTime (tracktion::BeatPosition::fromBeats (10.0)));
    edit.getUndoManager().clearUndoHistory();
}

juce::File DocScreenshots::writeHummedMelody (const juce::File& file) const
{
    constexpr double rate = 44100.0;
    const double secondsPerBeat = 60.0 / tempoBpm;
    double totalBeats = 0.0;

    for (int i = 0; i < hummedNotes; ++i)
        totalBeats += melody[i].beats;

    juce::AudioBuffer<float> audio (2, (int) ((totalBeats * secondsPerBeat + 0.5) * rate));
    audio.clear();
    double start = 0.0;

    for (int i = 0; i < hummedNotes; ++i)
    {
        auto& note = melody[i];
        auto length = note.beats * secondsPerBeat * 0.92;   // a breath between notes
        auto hz = 440.0 * std::pow (2.0, (note.pitch - 12 - 69) / 12.0);   // an octave down: sung
        double phase = 0.0;
        auto first = (int) (start * rate), count = (int) (length * rate);

        for (int n = 0; n < count && first + n < audio.getNumSamples(); ++n)
        {
            auto t = n / rate;
            auto vibrato = std::pow (2.0, 0.25 / 12.0 * std::sin (juce::MathConstants<double>::twoPi * 5.0 * t) * juce::jmin (1.0, t / 0.3));
            phase += juce::MathConstants<double>::twoPi * hz * vibrato / rate;
            auto envelope = juce::jmin (1.0, t / 0.04) * juce::jmin (1.0, (length - t) / 0.08);
            auto sample = std::sin (phase) + 0.45 * std::sin (2 * phase) + 0.2 * std::sin (3 * phase) + 0.08 * std::sin (4 * phase);

            for (int c = 0; c < 2; ++c)
                audio.addSample (c, first + n, (float) (0.22 * envelope * sample));
        }

        start += note.beats * secondsPerBeat;
    }

    file.deleteFile();
    juce::WavAudioFormat wav;

    if (auto stream = file.createOutputStream())
        if (std::unique_ptr<juce::AudioFormatWriter> writer { wav.createWriterFor (stream.release(), rate, 2, 24, {}, 0) })
            writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());

    return file;
}

//==============================================================================
juce::Rectangle<int> DocScreenshots::boundsInMain (juce::Component& c) const
{
    return c.getScreenBounds() - main.getScreenPosition();
}

std::vector<juce::Component*> DocScreenshots::getPopups() const
{
    std::vector<juce::Component*> popups;
    auto& desktop = juce::Desktop::getInstance();

    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* c = desktop.getComponent (i); c != nullptr && c->isVisible() && c != main.getTopLevelComponent()
                                                && c->getWidth() > 0 && c->getHeight() > 0)
            popups.push_back (c);

    return popups;
}

juce::Rectangle<int> DocScreenshots::popupArea() const
{
    juce::Rectangle<int> area;

    for (auto* popup : getPopups())
        area = area.getUnion (boundsInMain (*popup));

    return area;
}

juce::Rectangle<int> DocScreenshots::around (juce::Component& c, int margin) const
{
    auto area = boundsInMain (c);

    for (auto* popup : getPopups())
        area = area.getUnion (boundsInMain (*popup));

    return area.expanded (margin);
}

void DocScreenshots::capture (const juce::String& name, juce::Rectangle<int> area)
{
    if (area.isEmpty())
        area = main.getLocalBounds();

    juce::Image image (juce::Image::ARGB, juce::roundToInt ((float) area.getWidth() * imageScale),
                       juce::roundToInt ((float) area.getHeight() * imageScale), true);

    {
        juce::Graphics g (image);
        g.fillAll (main.getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
        g.addTransform (juce::AffineTransform::translation ((float) -area.getX(), (float) -area.getY()).scaled (imageScale));

        std::vector<juce::Component*> layers { &main };

        for (auto* popup : getPopups())
            layers.push_back (popup);

        for (auto* layer : layers)
        {
            auto bounds = layer == &main ? main.getLocalBounds() : boundsInMain (*layer);

            if (! bounds.intersects (area))
                continue;

            auto snapshot = layer->createComponentSnapshot (layer->getLocalBounds(), true, imageScale);
            g.drawImage (snapshot, bounds.toFloat());
        }
    }

    auto file = folder.getChildFile (name + ".png");
    file.deleteFile();
    juce::PNGImageFormat png;

    if (auto stream = file.createOutputStream(); stream == nullptr || ! png.writeImageToStream (image, *stream))
    {
        DBG ("Doc screenshots: couldn't write " << file.getFullPathName());
        ++failures;
    }
}

void DocScreenshots::closePopups()
{
    juce::PopupMenu::dismissAllActiveMenus();

    for (auto* popup : getPopups())
    {
        if (auto* callOut = dynamic_cast<juce::CallOutBox*> (popup))
            callOut->dismiss();
        else if (auto* dialog = dynamic_cast<juce::DialogWindow*> (popup))
            dialog->exitModalState (0);
    }
}

//==============================================================================
juce::Button* DocScreenshots::findButton (juce::Component& in, const juce::String& text) const
{
    if (auto* button = dynamic_cast<juce::Button*> (&in); button != nullptr && button->getButtonText() == text && button->isShowing())
        return button;

    for (auto* child : in.getChildren())
        if (auto* found = findButton (*child, text))
            return found;

    return nullptr;
}

template <typename Type>
Type* DocScreenshots::findChild (juce::Component& in, int index) const
{
    std::function<Type* (juce::Component&, int&)> search = [&search] (juce::Component& c, int& remaining) -> Type*
    {
        for (auto* child : c.getChildren())
        {
            if (auto* match = dynamic_cast<Type*> (child))
                if (remaining-- == 0)
                    return match;

            if (auto* found = search (*child, remaining))
                return found;
        }

        return nullptr;
    };

    return search (in, index);
}

void DocScreenshots::rightClick (juce::Component& c, juce::Point<int> position)
{
    auto now = juce::Time::getCurrentTime();
    juce::MouseEvent event (juce::Desktop::getInstance().getMainMouseSource(), position.toFloat(),
                            juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier | juce::ModifierKeys::popupMenuClickModifier),
                            juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f,
                            &c, &c, now, position.toFloat(), now, 1, false);
    c.mouseDown (event);
}

} // namespace purrlude
