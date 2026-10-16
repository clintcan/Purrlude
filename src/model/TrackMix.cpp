#include "TrackMix.h"

namespace purrlude
{

namespace
{
    class MixChange final : public juce::UndoableAction
    {
    public:
        MixChange (tracktion::Edit& e, tracktion::EditItemID id, TrackMix::Control c, float newValue, float oldValue)
            : edit (e), trackID (id), control (c), to (newValue), from (oldValue)
        {
        }

        bool perform() override     { TrackMix::set (edit, trackID, control, to);   return true; }
        bool undo() override        { TrackMix::set (edit, trackID, control, from); return true; }
        int getSizeInUnits() override  { return 16; }

        juce::UndoableAction* createCoalescedAction (juce::UndoableAction* next) override
        {
            if (auto* later = dynamic_cast<MixChange*> (next))
                if (&later->edit == &edit && later->trackID == trackID && later->control == control)
                    return new MixChange (edit, trackID, control, later->to, from);

            return nullptr;
        }

    private:
        tracktion::Edit& edit;
        const tracktion::EditItemID trackID;
        const TrackMix::Control control;
        const float to, from;
    };

    // The master's volume and pan live in its volume plugin's state, as a track's do; the
    // parameters (what's heard) follow it.
    tracktion::VolumeAndPanPlugin* getMasterVolume (tracktion::Edit& edit)
    {
        return edit.getMasterVolumePlugin().get();
    }

    float getMaster (tracktion::Edit& edit, TrackMix::Control control)
    {
        auto* master = getMasterVolume (edit);

        if (master == nullptr)
            return 0.0f;

        if (control == TrackMix::Control::pan)
            return (float) master->state.getProperty (tracktion::IDs::pan, 0.0f);

        if (control == TrackMix::Control::volumeDb)
            return tracktion::volumeFaderPositionToDB ((float) master->state.getProperty (tracktion::IDs::volume,
                                                                                          tracktion::decibelsToVolumeFaderPosition (0.0f)));

        return 0.0f;
    }

    void setMaster (tracktion::Edit& edit, TrackMix::Control control, float value)
    {
        auto* master = getMasterVolume (edit);

        if (master == nullptr)
            return;

        if (control == TrackMix::Control::pan)
            master->state.setProperty (tracktion::IDs::pan, juce::jlimit (-1.0f, 1.0f, value), nullptr);
        else if (control == TrackMix::Control::volumeDb)
            master->state.setProperty (tracktion::IDs::volume,
                                       tracktion::decibelsToVolumeFaderPosition (juce::jlimit (-100.0f, 6.0f, value)), nullptr);

        for (auto* parameter : master->getAutomatableParameters())
            parameter->updateFromAttachedValue();
    }

    juce::String describe (tracktion::EditItemID id, TrackMix::Control control)
    {
        if (id == TrackMix::masterID())
            return control == TrackMix::Control::pan ? "Change master balance" : "Change master volume";

        switch (control)
        {
            case TrackMix::Control::volumeDb:   return "Change volume";
            case TrackMix::Control::pan:        return "Change pan";
            case TrackMix::Control::mute:       return "Mute";
            case TrackMix::Control::solo:       return "Solo";
        }

        return {};
    }
}

float TrackMix::get (tracktion::Edit& edit, tracktion::EditItemID id, Control control)
{
    if (id == masterID())
        return getMaster (edit, control);

    TrackInstrument instrument (edit, id);

    switch (control)
    {
        case Control::volumeDb:     return instrument.getVolumeDb();
        case Control::pan:          return instrument.getPan();
        case Control::mute:         return instrument.isMuted() ? 1.0f : 0.0f;
        case Control::solo:         return instrument.isSolo() ? 1.0f : 0.0f;
    }

    return 0.0f;
}

void TrackMix::set (tracktion::Edit& edit, tracktion::EditItemID id, Control control, float value)
{
    if (id == masterID())
        return setMaster (edit, control, value);

    TrackInstrument instrument (edit, id);

    switch (control)
    {
        case Control::volumeDb:     instrument.setVolumeDb (value);     break;
        case Control::pan:          instrument.setPan (value);          break;
        case Control::mute:         instrument.setMuted (value > 0.5f); break;
        case Control::solo:         instrument.setSolo (value > 0.5f);  break;
    }
}

void TrackMix::change (tracktion::Edit& edit, tracktion::EditItemID id, Control control, float newValue, float previousValue)
{
    auto& um = edit.getUndoManager();
    um.beginNewTransaction (describe (id, control));
    um.perform (new MixChange (edit, id, control, newValue, previousValue));
    um.beginNewTransaction();
}

void TrackMix::changeInCurrentStep (tracktion::Edit& edit, tracktion::EditItemID id, Control control, float newValue,
                                    std::optional<float> previousValue)
{
    auto previous = previousValue.value_or (get (edit, id, control));

    if (! juce::exactlyEqual (previous, newValue))
        edit.getUndoManager().perform (new MixChange (edit, id, control, newValue, previous));
}

} // namespace purrlude
