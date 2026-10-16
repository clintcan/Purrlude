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

    juce::String describe (TrackMix::Control control)
    {
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
    um.beginNewTransaction (describe (control));
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
