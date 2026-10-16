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

    /** A third-party plugin's own controls: not the wet and dry levels Tracktion adds to it. */
    bool isPluginControl (tracktion::ExternalPlugin& plugin, tracktion::AutomatableParameter& parameter)
    {
        return &parameter != plugin.dryGain.get() && &parameter != plugin.wetGain.get();
    }

    Target makePluginTarget (tracktion::ExternalPlugin& plugin, tracktion::AutomatableParameter& parameter)
    {
        auto name = parameter.getParameterName().trim();
        return { plugin.itemID, parameter.paramID, plugin.getName() + ": " + name, plugin.getName(), name };
    }

    bool isExternalSoundSource (tracktion::AudioTrack& track)
    {
        return TrackInstrument (track.edit, track).getSoundSourceID().isNotEmpty();
    }
}

std::vector<Target> getTargets (tracktion::AudioTrack& track)
{
    // A track heard on an external device: its sound isn't Purrlude's to change.
    if (isExternalSoundSource (track))
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

    for (auto& plugin : getPluginControls (track))
        for (auto& control : plugin.controls)
            if (auto* parameter = findParameter (track, control); parameter != nullptr && isAutomated (*parameter))
                targets.push_back (control);

    return targets;
}

std::vector<PluginControls> getPluginControls (tracktion::AudioTrack& track)
{
    if (isExternalSoundSource (track))
        return {};

    std::vector<PluginControls> plugins;

    for (auto* plugin : track.pluginList)
    {
        auto* external = dynamic_cast<tracktion::ExternalPlugin*> (plugin);

        if (external == nullptr || external->getAudioPluginInstance() == nullptr)
            continue;   // not loaded (missing on this computer): no controls to show

        PluginControls controls { external->itemID, external->getName(), {} };

        for (auto* parameter : external->getAutomatableParameters())
            if (isPluginControl (*external, *parameter))
                controls.controls.push_back (makePluginTarget (*external, *parameter));

        plugins.push_back (std::move (controls));
    }

    return plugins;
}

//==============================================================================
TouchedControls::TouchedControls (tracktion::Edit& e)
    : edit (e)
{
    edit.getParameterChangeHandler().addListener (*this);
}

TouchedControls::~TouchedControls()
{
    edit.getParameterChangeHandler().removeListener (*this);
}

juce::StringArray TouchedControls::getRecent (tracktion::EditItemID plugin) const
{
    auto found = recent.find (plugin);
    return found != recent.end() ? found->second : juce::StringArray();
}

void TouchedControls::touched (tracktion::AutomatableParameter& parameter)
{
    auto* plugin = dynamic_cast<tracktion::ExternalPlugin*> (parameter.getPlugin());

    if (plugin == nullptr || ! isPluginControl (*plugin, parameter))
        return;

    auto& list = recent[plugin->itemID];
    list.removeString (parameter.paramID);
    list.insert (0, parameter.paramID);
    list.removeRange (maxRemembered, list.size());
}

void TouchedControls::pluginParameterChanged (tracktion::AutomatableParameter& parameter, bool isFollowingAutomation)
{
    // Automation playback calls this from the audio thread: leave at once.
    if (isFollowingAutomation)
        return;

    // A change while a mouse button is down is the user's. Plugins also send changes of their
    // own, e.g. when their window opens.
    if (juce::MessageManager::existsAndIsCurrentThread()
         && juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown())
        touched (parameter);
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

    // Any control of a third-party plugin, looked up directly (it may have hundreds).
    if (! isExternalSoundSource (track))
    {
        auto pluginID = tracktion::EditItemID::fromVar (shown.upToFirstOccurrenceOf ("/", false, false));

        if (auto* external = dynamic_cast<tracktion::ExternalPlugin*> (findPlugin (track, pluginID)))
            if (auto parameter = external->getAutomatableParameterByID (shown.fromFirstOccurrenceOf ("/", false, false)))
                if (isPluginControl (*external, *parameter))
                    return makePluginTarget (*external, *parameter);
    }

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

std::optional<Target> findTarget (tracktion::AudioTrack& track, const tracktion::AutomatableParameter& parameter)
{
    auto* plugin = parameter.getPlugin();

    if (plugin == nullptr)
        return std::nullopt;

    for (auto& target : getTargets (track))
        if (target.plugin == plugin->itemID && target.parameterID == parameter.paramID)
            return target;

    if (auto* external = dynamic_cast<tracktion::ExternalPlugin*> (plugin))
        for (auto& controls : getPluginControls (track))
            if (controls.plugin == external->itemID)
                for (auto& control : controls.controls)
                    if (control.parameterID == parameter.paramID)
                        return control;

    return std::nullopt;
}

void userMoved (tracktion::AutomatableParameter& parameter)
{
    parameter.getEdit().getParameterChangeHandler().parameterChanged (parameter, false);
}

//==============================================================================
bool isWriting (tracktion::Edit& edit)
{
    return edit.getAutomationRecordManager().isWritingAutomation();
}

void setWriting (tracktion::Edit& edit, bool shouldWrite)
{
    auto& manager = edit.getAutomationRecordManager();

    // Turning it off mid-pass keeps what was recorded so far.
    if (! shouldWrite)
        manager.punchOut (false);

    manager.setWritingAutomation (shouldWrite);
}

bool isRecordingPass (tracktion::Edit& edit)
{
    auto& transport = edit.getTransport();
    return isWriting (edit) && (transport.isPlaying() || transport.isRecording());
}

std::optional<Target> findRecording (tracktion::AudioTrack& track)
{
    auto& manager = track.edit.getAutomationRecordManager();

    if (! manager.isRecordingAutomation())
        return std::nullopt;

    for (auto* plugin : track.pluginList)
        for (auto* parameter : plugin->getAutomatableParameters())
            if (manager.isParameterRecording (parameter))
                return findTarget (track, *parameter);

    return std::nullopt;
}

Gesture::Gesture (tracktion::AutomatableParameter& p)
    : parameter (&p)
{
    parameter->parameterChangeGestureBegin();
}

Gesture::~Gesture()
{
    parameter->parameterChangeGestureEnd();
}

Recorder::Recorder (tracktion::Edit& e)
    : edit (e)
{
    // Tracktion keeps the switch in the song (on by default with its newer recorder); in
    // Purrlude it starts off every time.
    edit.getAutomationRecordManager().setWritingAutomation (false);
    edit.getAutomationRecordManager().addListener (this);
    edit.getTransport().addChangeListener (this);
}

Recorder::~Recorder()
{
    edit.getTransport().removeChangeListener (this);
    edit.getAutomationRecordManager().removeListener (this);
    pass.reset();
}

void Recorder::update()
{
    auto recording = isRecordingPass (edit);

    if (recording && pass == nullptr)
    {
        // Touch mode: a control is recorded while it's held, then follows its curve again.
        // Not undoable: it's how recording works, not part of the music.
        for (auto* track : tracktion::getAllTracks (edit))
            if (track->automationMode.get() != tracktion::AutomationMode::touch)
                track->state.setProperty (tracktion::IDs::automationMode,
                                          tracktion::toString (tracktion::AutomationMode::touch), nullptr);

        edit.getUndoManager().beginNewTransaction ("Record automation");
        pass = std::make_unique<tracktion::Edit::UndoTransactionInhibitor> (edit);
    }
    else if (! recording && pass != nullptr && ! edit.getTransport().isPlaying() && ! edit.getTransport().isRecording())
    {
        // Stopped: Tracktion writes what's still held into the curves, in this step.
        edit.getAutomationRecordManager().punchOut (false);
        pass.reset();
        edit.getUndoManager().beginNewTransaction();
    }
}

} // namespace purrlude::automation
