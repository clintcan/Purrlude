#include "Automation.h"

#include "SharedReverb.h"
#include "TrackEffects.h"
#include "TrackInstrument.h"

namespace purrlude::automation
{

namespace
{
    const juce::Identifier shownProperty { "purrludeAutomation" };   // "<plugin id>/<parameter id>", or absent

    tracktion::Plugin* findPlugin (tracktion::AudioTrack& track, tracktion::EditItemID id)
    {
        for (auto* plugin : track.pluginList)
            if (plugin->itemID == id)
                return plugin;

        return nullptr;
    }
}

std::vector<Target> getTargets (tracktion::AudioTrack& track)
{
    // A track heard on an external device: its sound isn't Purrlude's to change.
    if (TrackInstrument (track.edit, track).getSoundSourceID().isNotEmpty())
        return {};

    std::vector<Target> targets;

    if (auto* volume = track.getVolumePlugin())
    {
        targets.push_back ({ volume->itemID, volume->volParam->paramID, "Volume" });
        targets.push_back ({ volume->itemID, volume->panParam->paramID, "Pan" });
    }

    if (auto* send = shared_reverb::findSend (track))
        targets.push_back ({ send->itemID, send->gain->paramID, "Reverb send" });

    for (auto* effect : track_effects::getEffects (track))
    {
        auto kind = track_effects::getKind (*effect);

        for (auto& control : track_effects::getControls (*effect))
            targets.push_back ({ effect->itemID, control.parameter->paramID,
                                 (kind ? track_effects::getName (*kind) : effect->getName()) + " " + control.label });
    }

    return targets;
}

tracktion::AutomatableParameter* findParameter (tracktion::AudioTrack& track, const Target& target)
{
    if (auto* plugin = findPlugin (track, target.plugin))
        if (auto parameter = plugin->getAutomatableParameterByID (target.parameterID))
            return parameter.get();

    return nullptr;
}

std::optional<Target> getShown (tracktion::AudioTrack& track)
{
    auto shown = track.state[shownProperty].toString();

    if (shown.isEmpty())
        return std::nullopt;

    for (auto& target : getTargets (track))
        if (target.plugin.toString() + "/" + target.parameterID == shown)
            return target;

    return std::nullopt;   // its effect was removed, or the track now plays on a device
}

void setShown (tracktion::AudioTrack& track, std::optional<Target> target)
{
    // A view setting, saved with the song but not part of undo (like the zoom).
    if (target)
        track.state.setProperty (shownProperty, target->plugin.toString() + "/" + target->parameterID, nullptr);
    else
        track.state.removeProperty (shownProperty, nullptr);
}

bool isAutomated (const tracktion::AutomatableParameter& parameter)
{
    return parameter.hasAutomationPoints();
}

int addPoint (tracktion::Edit& edit, tracktion::AutomatableParameter& parameter, tracktion::TimePosition time, float value)
{
    edit.getUndoManager().beginNewTransaction ("Add automation point");
    auto range = parameter.getValueRange();
    return parameter.getCurve().addPoint (time, juce::jlimit (range.getStart(), range.getEnd(), value), 0.0f);
}

int movePoint (tracktion::AutomatableParameter& parameter, int index, tracktion::TimePosition time, float value)
{
    auto& curve = parameter.getCurve();

    if (! juce::isPositiveAndBelow (index, curve.getNumPoints()))
        return index;

    // Between its neighbours, so the points keep their order.
    auto earliest = index > 0 ? curve.getPointTime (index - 1) : tracktion::TimePosition();
    auto latest = index + 1 < curve.getNumPoints() ? curve.getPointTime (index + 1) : tracktion::TimePosition::fromSeconds (1.0e9);
    time = juce::jlimit (earliest, latest, juce::jmax (tracktion::TimePosition(), time));

    auto range = parameter.getValueRange();
    curve.setPointTime (index, time);
    curve.setPointValue (index, juce::jlimit (range.getStart(), range.getEnd(), value));
    return index;
}

void removePoint (tracktion::Edit& edit, tracktion::AutomatableParameter& parameter, int index)
{
    if (! juce::isPositiveAndBelow (index, parameter.getCurve().getNumPoints()))
        return;

    edit.getUndoManager().beginNewTransaction ("Remove automation point");
    parameter.getCurve().removePoint (index);
}

void clear (tracktion::Edit& edit, tracktion::AutomatableParameter& parameter)
{
    edit.getUndoManager().beginNewTransaction ("Clear automation");
    parameter.getCurve().clear();
}

PointDrag::PointDrag (tracktion::Edit& edit)
    : inhibitor (edit)
{
    edit.getUndoManager().beginNewTransaction ("Move automation point");
}

} // namespace purrlude::automation
