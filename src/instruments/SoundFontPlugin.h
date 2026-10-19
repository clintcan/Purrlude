#pragma once

#include <tracktion_engine/tracktion_engine.h>

struct tsf;

namespace purrlude
{

/** Built-in General MIDI instrument: plays a SoundFont (GeneralUser GS, or one the user
    chose) with TinySoundFont.

    The track's notes play the chosen program (or drum kit), in the chosen bank, on every MIDI
    channel. Incoming Program Change messages are ignored: the track's instrument choice is
    what you hear.

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

    /** A GM program number (0-127) or a drum kit (gm::isDrumKit), and the SoundFont bank it's
        played from (a variation; 0 is the General MIDI sound, and drum kits have their own).
        A bank the SoundFont hasn't got for the program plays bank 0.
    */
    void setProgram (int program, int bank = 0);
    int getProgram() const;
    int getBank() const;

    /** Plays or releases a note now, outside the track's own MIDI (e.g. previewing while
        editing). Lock-free; call from one thread (the message thread).
    */
    void queuePreview (int noteNumber, bool on);

    //==============================================================================
    /** Where the bundled SoundFont is: next to the app, or the source tree in development builds. */
    static juce::File findSoundFont();

    /** Starts loading the SoundFont on a background thread, so the first instrument is ready sooner. */
    static void preload();

    /** True if the SoundFont exists and loaded (waits for a load in progress). */
    static bool isAvailable();

    /** Plays this SoundFont (.sf2) instead of the bundled one, or the bundled one again for an
        empty File. It loads in the background, then every instrument moves to it; one that
        won't load is replaced by the bundled one (see getSoundFontError). Message thread.
    */
    static void useSoundFont (const juce::File&);

    /** The SoundFont the user chose, or an empty File for the bundled one. */
    static juce::File getChosenSoundFont();

    /** Why the chosen SoundFont isn't playing, or an empty string. */
    static juce::String getSoundFontError();

    /** Goes up each time a SoundFont finishes loading, for views listing its presets. */
    static int getSoundFontGeneration();

    struct Preset
    {
        int bank, program;   ///< bank 128 holds drum kits
        juce::String name;
    };

    /** The loaded SoundFont's presets (none while it loads). */
    static std::vector<Preset> getPresets();

    /** Moves every instrument to a newly loaded SoundFont (message thread). */
    static void fontChanged();

private:
    void applyProgram (int packed);
    void buildSynth();

    juce::CachedValue<int> program, bank;
    std::atomic<int> pendingProgram { -1 };
    int builtGeneration = 0;

    // Preview notes, message thread -> audio thread: note number, +128 for note-on.
    juce::AbstractFifo previewFifo { 64 };
    std::array<int, 64> previewQueue {};

    juce::SpinLock synthLock;          // guards swapping the synth in initialise/deinitialise
    tsf* synth = nullptr;
    std::vector<float> scratch;        // unweaved stereo render buffer
    int scratchFrames = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundFontPlugin)
};

} // namespace purrlude
