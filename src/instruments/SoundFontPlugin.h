#pragma once

#include <tracktion_engine/tracktion_engine.h>

struct tsf;

namespace purrlude
{

/** Built-in General MIDI instrument: plays a SoundFont (GeneralUser GS) with TinySoundFont.

    The track's notes play the chosen program (or the drum kit) on every MIDI
    channel. Incoming Program Change messages are ignored: the track's
    instrument choice is what you hear.

    Real-time safety: all TinySoundFont calls happen on the audio thread except
    setup in initialise(), which pre-allocates voices and all 16 channels so
    rendering never allocates. Program changes reach the audio thread through an
    atomic. The SoundFont is loaded once (in the background, see preload()) and
    shared by every instance.
*/
class SoundFontPlugin final : public tracktion::Plugin
{
public:
    SoundFontPlugin (tracktion::PluginCreationInfo);
    ~SoundFontPlugin() override;

    static const char* getPluginName()                  { return "Purrlude Instrument"; }
    static const char* xmlTypeName;
    static juce::ValueTree create();

    juce::String getName() const override              { return getPluginName(); }
    juce::String getPluginType() override              { return xmlTypeName; }
    juce::String getShortName (int) override           { return "Instrument"; }
    juce::String getSelectableDescription() override   { return getName(); }

    bool isSynth() override                            { return true; }
    bool takesMidiInput() override                     { return true; }
    bool takesAudioInput() override                    { return false; }
    bool producesAudioWhenNoAudioInput() override      { return true; }
    int getNumOutputChannelsGivenInputs (int) override { return 2; }

    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;
    void restorePluginStateFromValueTree (const juce::ValueTree&) override;

    /** A GM program number (0-127) or gm::drumKit. */
    void setProgram (int);
    int getProgram() const;

    //==============================================================================
    /** Where the bundled SoundFont is: next to the app, or the source tree in development builds. */
    static juce::File findSoundFont();

    /** Starts loading the SoundFont on a background thread, so the first instrument is ready sooner. */
    static void preload();

    /** True if the SoundFont exists and loaded (waits for a load in progress). */
    static bool isAvailable();

private:
    void applyProgram (int);

    juce::CachedValue<int> program;
    std::atomic<int> pendingProgram { -1 };

    juce::SpinLock synthLock;          // guards swapping the synth in initialise/deinitialise
    tsf* synth = nullptr;
    std::vector<float> scratch;        // unweaved stereo render buffer
    int scratchFrames = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundFontPlugin)
};

} // namespace purrlude
