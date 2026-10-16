#include "Takes.h"

#include "ClipEditing.h"
#include "LoopRange.h"

namespace purrlude::takes
{

namespace
{
    const juce::Identifier takesType ("PURRLUDE_TAKES"), takeType ("TAKE"), currentProperty ("current");

    juce::ValueTree getAudioTakes (tracktion::Clip& clip)
    {
        return dynamic_cast<tracktion::WaveAudioClip*> (&clip) != nullptr ? clip.state.getChildWithName (takesType)
                                                                          : juce::ValueTree();
    }

    tracktion::MidiClip* asMidi (tracktion::Clip& clip)     { return dynamic_cast<tracktion::MidiClip*> (&clip); }
}

int getCount (tracktion::Clip& clip)
{
    if (auto* midi = asMidi (clip))
        return midi->hasAnyTakes() ? midi->getNumTakes (false) : 0;

    return getAudioTakes (clip).getNumChildren();
}

int getCurrent (tracktion::Clip& clip)
{
    if (auto* midi = asMidi (clip))
        return midi->hasAnyTakes() ? midi->getCurrentTake() : 0;

    auto audioTakes = getAudioTakes (clip);
    return audioTakes.isValid() ? juce::jlimit (0, juce::jmax (0, audioTakes.getNumChildren() - 1), (int) audioTakes[currentProperty]) : 0;
}

void setCurrent (tracktion::Edit& edit, tracktion::Clip& clip, int index)
{
    if (! juce::isPositiveAndBelow (index, getCount (clip)) || index == getCurrent (clip))
        return;

    auto* um = &edit.getUndoManager();
    um->beginNewTransaction ("Choose take " + juce::String (index + 1));

    if (auto* midi = asMidi (clip))
    {
        midi->setCurrentTake (index);
        return;
    }

    auto audioTakes = getAudioTakes (clip);
    audioTakes.setProperty (currentProperty, index, um);
    clip.state.setProperty (tracktion::IDs::source, audioTakes.getChild (index)[tracktion::IDs::source], um);
}

void keepOnlyCurrent (tracktion::Edit& edit, tracktion::Clip& clip)
{
    if (getCount (clip) < 2)
        return;

    auto* um = &edit.getUndoManager();
    um->beginNewTransaction ("Delete other takes");

    if (auto* midi = asMidi (clip))
        midi->clearTakes();
    else
        clip.state.removeChild (getAudioTakes (clip), um);
}

void adoptRecordedTakes (tracktion::Edit& edit, tracktion::Clip& clip, tracktion::TimeDuration loopLength)
{
    auto* wave = dynamic_cast<tracktion::WaveAudioClip*> (&clip);

    if (wave == nullptr)
        return;

    auto recorded = clip.state.getChildWithName (tracktion::IDs::TAKES);

    if (! recorded.isValid())
        return;

    // Tracktion writes the first pass to the recording's file and each further one beside it
    // as "<name>_take_<n>". Its take list isn't read: it refers to them relative to the
    // project file (which may not exist yet), not as our path resolver does.
    juce::Array<juce::File> files;
    auto first = wave->getOriginalFile();

    if (first.existsAsFile())
    {
        files.add (first);

        for (int n = 2; ; ++n)
        {
            auto next = first.getSiblingFile (first.getFileNameWithoutExtension() + "_take_" + juce::String (n))
                             .withFileExtension (first.getFileExtension());

            if (! next.existsAsFile())
                break;

            files.add (next);
        }
    }

    auto* um = &edit.getUndoManager();
    clip.state.removeChild (recorded, um);

    if (files.size() < 2)
        return;

    // The last pass, unless recording stopped early in it.
    auto current = files.size() - 1;
    tracktion::AudioFile last (edit.engine, files.getLast());

    if (last.getLength() < loopLength.inSeconds() * 0.5)
        --current;

    juce::ValueTree audioTakes (takesType);
    audioTakes.setProperty (currentProperty, current, nullptr);

    for (auto& file : files)
        audioTakes.appendChild (juce::ValueTree (takeType, { { tracktion::IDs::source, file.getFullPathName() } }), nullptr);

    clip.state.appendChild (audioTakes, um);
    clip.state.setProperty (tracktion::IDs::source, files[current].getFullPathName(), um);
}

juce::StringArray getAudioSources (tracktion::Clip& clip)
{
    juce::StringArray sources;

    for (auto take : getAudioTakes (clip))
        sources.add (take[tracktion::IDs::source].toString());

    return sources;
}

void setAudioSource (tracktion::Clip& clip, int index, const juce::String& source)
{
    auto take = getAudioTakes (clip).getChild (index);

    if (take.isValid() && take[tracktion::IDs::source].toString() != source)
        take.setProperty (tracktion::IDs::source, source, nullptr);
}

void prepareMidiInputs (tracktion::Edit& edit)
{
    auto looping = edit.getTransport().looping.get();

    // A punch replaces what was in its range, like a new recording, rather than adding to it.
    auto merge = ! looping && ! loop_range::isPunching (edit);
    loop_range::sync (edit);

    for (auto& input : edit.engine.getDeviceManager().getMidiInDevices())
    {
        input->mergeRecordings = merge;
        input->replaceExistingClips = false;
    }
}

RecordingFinisher::RecordingFinisher (tracktion::Edit& e)
    : edit (e)
{
    edit.getTransport().addListener (this);
}

RecordingFinisher::~RecordingFinisher()
{
    edit.getTransport().removeListener (this);
}

void RecordingFinisher::recordingFinished (tracktion::InputDeviceInstance&, tracktion::EditItemID,
                                           const juce::ReferenceCountedArray<tracktion::Clip>& recorded)
{
    auto& transport = edit.getTransport();

    for (auto& clip : recorded)
    {
        if (clip == nullptr || clip->getClipTrack() == nullptr)
            continue;

        if (transport.looping)
            adoptRecordedTakes (edit, *clip, transport.getLoopRange().getLength());

        clips::makeRoomFor (edit, *clip);
    }
}

} // namespace purrlude::takes
