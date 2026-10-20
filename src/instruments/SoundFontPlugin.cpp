#include "SoundFontPlugin.h"

#include "GeneralMidi.h"

#include <condition_variable>
#include <map>
#include <set>
#include <mutex>
#include <thread>

#define TSF_IMPLEMENTATION
#include <tsf.h>

namespace purrlude
{

namespace
{
    const juce::Identifier programProperty { "program" }, bankProperty { "bank" }, soundFontProperty { "soundFont" };
    constexpr int maxVoices = 96;
    constexpr float outputGainDb = -3.0f;

    /** A program and bank in one atomic value: program (0-255) + bank * 256. */
    int pack (int program, int bank)    { return program + bank * 256; }

    /** Loads a SoundFont file, or returns null with the reason. */
    tsf* loadSoundFont (const juce::File& file, juce::String& error)
    {
        juce::MemoryBlock data;

        if (! file.existsAsFile() || ! file.loadFileAsData (data))
        {
            error = file.getFileName() + " couldn't be read.";
            return nullptr;
        }

        if (auto* loaded = tsf_load_memory (data.getData(), (int) data.getSize()))
            return loaded;

        error = file.getFileName() + " isn't a SoundFont (.sf2) Purrlude can play.";
        return nullptr;
    }

    /** Goes up each time any SoundFont finishes loading (SoundFontPlugin::getSoundFontGeneration). */
    std::atomic<int> loadsFinished { 0 };

    /** One SoundFont, loaded on a background thread: the app's (the bundled one unless the user
        chose another, see SoundFontPlugin::useSoundFont; one that won't load is replaced by the
        bundled one) or a track's own (one that won't load is null, with the reason). Plugins play
        tsf_copies of it, which share its data until the last one closes.

        Its destructor waits for a load still in progress: a loading thread left running into
        process exit crashes on Windows as statics are torn down.
    */
    class FontSource
    {
    public:
        FontSource (juce::File file, bool appFont) : chosen (std::move (file)), isApp (appFont) {}

        ~FontSource()
        {
            if (loader.joinable())
                loader.join();

            if (font != nullptr)
                tsf_close (font);
        }

        void startLoading()
        {
            const std::lock_guard<std::mutex> lock (mutex);

            if (! std::exchange (started, true))
                startLoader();
        }

        /** The app's: loads another SoundFont (none: the bundled one). Plugins move to it when it's loaded. */
        void choose (const juce::File& file)
        {
            if (loader.joinable())
                loader.join();   // a load in progress: loading takes a second or so

            const std::lock_guard<std::mutex> lock (mutex);
            chosen = file;

            if (started)
            {
                done = false;
                startLoader();
            }
        }

        juce::File getChosen()
        {
            const std::lock_guard<std::mutex> lock (mutex);
            return chosen;
        }

        /** Waits for the load if it's still running. Null if the SoundFont won't load. */
        tsf* get (int* loadedGeneration = nullptr)
        {
            startLoading();
            std::unique_lock<std::mutex> lock (mutex);
            loadedCondition.wait (lock, [this] { return done; });

            if (loadedGeneration != nullptr)
                *loadedGeneration = generation;

            return font;
        }

        /** Without waiting: false while it's loading; else the font (null if it won't load). */
        bool tryGet (tsf*& loaded, int& loadedGeneration)
        {
            startLoading();
            const std::lock_guard<std::mutex> lock (mutex);

            if (! done)
                return false;

            loaded = font;
            loadedGeneration = generation;
            return true;
        }

        bool isLoading()
        {
            const std::lock_guard<std::mutex> lock (mutex);
            return started && ! done;
        }

        /** The loaded SoundFont's presets; none while it's loading. */
        std::vector<SoundFontPlugin::Preset> getPresets()
        {
            const std::lock_guard<std::mutex> lock (mutex);
            std::vector<SoundFontPlugin::Preset> presets;

            if (done && font != nullptr)
                for (int i = 0; i < font->presetNum; ++i)
                    presets.push_back ({ font->presets[i].bank, font->presets[i].preset, juce::String (font->presets[i].presetName).trim() });

            return presets;
        }

        juce::String getError()
        {
            const std::lock_guard<std::mutex> lock (mutex);
            return error;
        }

    private:
        void startLoader()   // with the mutex held
        {
            loader = std::thread ([this, file = chosen]
            {
                juce::String loadError;
                auto* loaded = file != juce::File() ? loadSoundFont (file, loadError) : nullptr;

                if (loaded == nullptr && isApp)
                {
                    juce::String bundledError;
                    loaded = loadSoundFont (SoundFontPlugin::findSoundFont(), bundledError);

                    if (file == juce::File())
                        loadError = bundledError;
                    else
                        loadError += " Playing the built-in sounds instead.";
                }

                tsf* old = nullptr;

                {
                    const std::lock_guard<std::mutex> l (mutex);
                    old = std::exchange (font, loaded);
                    error = loadError;
                    done = true;
                    ++generation;
                }

                ++loadsFinished;
                loadedCondition.notify_all();

                // Plugins play copies, which share the font's data until the last one closes.
                if (old != nullptr)
                    tsf_close (old);

                juce::MessageManager::callAsync ([] { SoundFontPlugin::fontChanged(); });
            });
        }

        std::mutex mutex;
        std::condition_variable loadedCondition;
        std::thread loader;
        bool started = false, done = false;
        tsf* font = nullptr;
        juce::File chosen;
        const bool isApp;
        juce::String error;
        int generation = 0;
    };

    /** The app's SoundFont and the tracks' own, by file, shared by every plugin. Tracks'
        SoundFonts load when a plugin first wants one and unload when none does (message thread).
        Deleted when JUCE shuts down.
    */
    class FontCache final : public juce::DeletedAtShutdown
    {
    public:
        ~FontCache() override   { clearSingletonInstance(); }

        FontSource& getApp()    { return app; }

        /** A track's SoundFont, loading in the background if it isn't yet. */
        FontSource& get (const juce::File& file)
        {
            auto& source = sources[file.getFullPathName()];

            if (source == nullptr)
            {
                source = std::make_unique<FontSource> (file, false);
                source->startLoading();
            }

            return *source;
        }

        FontSource* find (const juce::File& file)
        {
            auto found = sources.find (file.getFullPathName());
            return found != sources.end() ? found->second.get() : nullptr;
        }

        /** Unloads the tracks' SoundFonts that no plugin plays any more. */
        void releaseUnused (const std::set<juce::String>& used)
        {
            for (auto it = sources.begin(); it != sources.end();)
                it = used.count (it->first) == 0 ? sources.erase (it) : std::next (it);
        }

        JUCE_DECLARE_SINGLETON_INLINE (FontCache, false)

    private:
        FontSource app { {}, true };
        std::map<juce::String, std::unique_ptr<FontSource>> sources;
    };

    FontCache& getFonts()
    {
        return *FontCache::getInstance();
    }

    /** Every plugin, to move them to a newly loaded SoundFont (message thread). */
    juce::Array<SoundFontPlugin*>& getPlugins()
    {
        static juce::Array<SoundFontPlugin*> plugins;
        return plugins;
    }

    /** Unloads the tracks' SoundFonts no plugin plays any more. */
    void releaseUnusedFonts()
    {
        auto* fonts = FontCache::getInstanceWithoutCreating();

        if (fonts == nullptr)
            return;

        std::set<juce::String> used;

        for (auto* plugin : getPlugins())
            if (auto file = plugin->getSoundFont(); file != juce::File())
                used.insert (file.getFullPathName());

        fonts->releaseUnused (used);
    }
}

const char* SoundFontPlugin::xmlTypeName = "purrludeSoundFont";

juce::ValueTree SoundFontPlugin::create()
{
    return tracktion::createValueTree (tracktion::IDs::PLUGIN, tracktion::IDs::type, xmlTypeName);
}

SoundFontPlugin::SoundFontPlugin (tracktion::PluginCreationInfo info)
    : Plugin (info)
{
    program.referTo (state, programProperty, getUndoManager(), 0);
    bank.referTo (state, bankProperty, getUndoManager(), 0);
    soundFont.referTo (state, soundFontProperty, getUndoManager(), {});
    pendingProgram = pack (program.get(), bank.get());
    preload();
    getPlugins().add (this);

    if (auto file = getSoundFont(); file != juce::File())
        getFonts().get (file);   // a saved track's own: loading while the song opens
}

SoundFontPlugin::~SoundFontPlugin()
{
    getPlugins().removeAllInstancesOf (this);
    notifyListenersOfDeletion();
    deinitialise();
    releaseUnusedFonts();
}

void SoundFontPlugin::initialise (const tracktion::PluginInitialisationInfo&)
{
    buildSynth();   // with Plugin's sampleRate and blockSizeSamples, already set
}

tsf* SoundFontPlugin::chooseFont (juce::String& signature, bool wait)
{
    // The track's own SoundFont once it's loaded; until then, or if it won't load, the app's.
    if (auto file = getSoundFont(); file != juce::File())
    {
        tsf* font = nullptr;
        int generation = 0;

        if (getFonts().get (file).tryGet (font, generation) && font != nullptr)
        {
            signature = file.getFullPathName() + "#" + juce::String (generation);
            return font;
        }
    }

    tsf* font = nullptr;
    int generation = 0;

    if (wait)
        font = getFonts().getApp().get (&generation);
    else if (! getFonts().getApp().tryGet (font, generation))
        return nullptr;

    signature = "#" + juce::String (generation);
    return font;
}

void SoundFontPlugin::buildSynth()
{
    juce::String signature;
    auto* font = chooseFont (signature, true);

    if (font == nullptr || sampleRate <= 0.0)
        return;

    // Everything that allocates happens here, before rendering starts.
    auto* fresh = tsf_copy (font);
    tsf_set_output (fresh, TSF_STEREO_UNWEAVED, juce::roundToInt (sampleRate), outputGainDb);
    tsf_set_max_voices (fresh, maxVoices);

    for (int channel = 15; channel >= 0; --channel)   // highest first: allocates all 16 at once
        tsf_channel_set_presetnumber (fresh, channel, 0, 0);

    std::vector<float> freshScratch;
    auto frames = juce::jmax (512, blockSizeSamples);
    freshScratch.resize ((size_t) frames * 2);

    {
        const juce::SpinLock::ScopedLockType lock (synthLock);
        std::swap (synth, fresh);
        std::swap (scratch, freshScratch);
        scratchFrames = frames;
        applyProgram (pack (program.get(), bank.get()));
    }

    builtFrom = signature;

    if (fresh != nullptr)
        tsf_close (fresh);
}

void SoundFontPlugin::rebuildIfFontChanged()
{
    juce::String signature;

    if (synth != nullptr && chooseFont (signature, false) != nullptr && signature != builtFrom)
        buildSynth();
}

void SoundFontPlugin::fontChanged()
{
    for (auto* plugin : getPlugins())
        plugin->rebuildIfFontChanged();
}

void SoundFontPlugin::deinitialise()
{
    tsf* old = nullptr;

    {
        const juce::SpinLock::ScopedLockType lock (synthLock);
        std::swap (synth, old);
    }

    if (old != nullptr)
        tsf_close (old);
}

void SoundFontPlugin::applyProgram (int packed)
{
    auto newProgram = packed % 256, newBank = packed / 256;

    // Channels already exist (see initialise), so this doesn't allocate. A bank the SoundFont
    // hasn't got for this program plays its sound in bank 0.
    for (int channel = 0; channel < 16; ++channel)
    {
        tsf_channel_set_bank (synth, channel, 0);

        if (gm::isDrumKit (newProgram))
            tsf_channel_set_presetnumber (synth, channel, gm::getKitNumber (newProgram), 1);
        else if (newBank == 0 || ! tsf_channel_set_bank_preset (synth, channel, newBank, newProgram))
            tsf_channel_set_presetnumber (synth, channel, juce::jlimit (0, 127, newProgram), 0);
    }
}

void SoundFontPlugin::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    const auto start = fc.bufferStartSample, numSamples = fc.bufferNumSamples;
    fc.destBuffer->clear (start, numSamples);

    // Never wait on the audio thread: if the synth is being swapped, skip this block.
    const juce::SpinLock::ScopedTryLockType lock (synthLock);

    if (! lock.isLocked() || synth == nullptr)
        return;

    if (auto newProgram = pendingProgram.exchange (-1); newProgram >= 0)
        applyProgram (newProgram);

    previewFifo.read (previewFifo.getNumReady()).forEach ([this] (int index)
    {
        auto entry = previewQueue[(size_t) index];

        if (entry >= 128)
            tsf_channel_note_on (synth, 0, entry - 128, 0.8f);
        else
            tsf_channel_note_off (synth, 0, entry);
    });

    auto* midi = fc.bufferForMidiMessages;

    if (midi != nullptr && midi->isAllNotesOff)
        tsf_note_off_all (synth);

    int position = 0;
    auto* dest = fc.destBuffer;

    auto renderUpTo = [&] (int end)
    {
        while (position < end)
        {
            auto n = std::min (end - position, scratchFrames);
            tsf_render_float (synth, scratch.data(), n, 0);   // left [0, n), right [n, 2n)

            dest->copyFrom (0, start + position, scratch.data(), n);

            if (dest->getNumChannels() > 1)
                dest->copyFrom (1, start + position, scratch.data() + n, n);

            position += n;
        }
    };

    if (midi != nullptr)
    {
        for (const auto& m : *midi)
        {
            // Timestamps are sample positions in the destination buffer.
            renderUpTo (juce::jlimit (position, numSamples, (int) m.getTimeStamp() - start));

            auto channel = m.getChannel() - 1;

            if (channel < 0)
                continue;

            if (m.isNoteOn())
                tsf_channel_note_on (synth, channel, m.getNoteNumber(), m.getFloatVelocity());
            else if (m.isNoteOff())
                tsf_channel_note_off (synth, channel, m.getNoteNumber());
            else if (m.isController())
                tsf_channel_midi_control (synth, channel, m.getControllerNumber(), m.getControllerValue());
            else if (m.isPitchWheel())
                tsf_channel_set_pitchwheel (synth, channel, m.getPitchWheelValue());
            // Program changes are ignored: the track's instrument choice wins.
        }
    }

    renderUpTo (numSamples);

    for (int channel = 2; channel < dest->getNumChannels(); ++channel)
        dest->clear (channel, start, numSamples);
}

void SoundFontPlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
{
    tracktion::copyPropertiesToCachedValues (v, program, bank, soundFont);
    pendingProgram = pack (program.get(), bank.get());

    if (auto file = getSoundFont(); file != juce::File())
        getFonts().get (file);

    rebuildIfFontChanged();
    releaseUnusedFonts();
}

void SoundFontPlugin::setSoundFont (const juce::File& file)
{
    if (file == getSoundFont())
        return;

    if (file == juce::File())
    {
        soundFont.resetToDefault();   // not written: tracks on the app's SoundFont stay as they were
    }
    else
    {
        soundFont = file.getFullPathName();
        getFonts().get (file);        // starts loading now, so its state shows before anything plays
    }

    rebuildIfFontChanged();   // at once if it's loaded; otherwise when it is (fontChanged)
    releaseUnusedFonts();
}

juce::File SoundFontPlugin::getPlayingSoundFont() const
{
    auto path = builtFrom.upToLastOccurrenceOf ("#", false, false);   // "<path>#<generation>", or "#<generation>"
    return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
}

juce::File SoundFontPlugin::getSoundFont() const
{
    auto path = soundFont.get();
    return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
}

void SoundFontPlugin::setProgram (int newProgram, int newBank)
{
    newProgram = gm::validProgram (newProgram);
    newBank = gm::isDrumKit (newProgram) ? 0 : juce::jlimit (0, 127, newBank);
    program = newProgram;

    if (newBank == 0)
        bank.resetToDefault();   // not written: songs without variations stay as they were
    else
        bank = newBank;

    pendingProgram = pack (newProgram, newBank);
}

int SoundFontPlugin::getProgram() const
{
    return program.get();
}

int SoundFontPlugin::getBank() const
{
    return bank.get();
}

void SoundFontPlugin::queuePreview (int noteNumber, bool on)
{
    previewFifo.write (1).forEach ([this, noteNumber, on] (int index)
    {
        previewQueue[(size_t) index] = juce::jlimit (0, 127, noteNumber) + (on ? 128 : 0);
    });
}

//==============================================================================
juce::File SoundFontPlugin::findSoundFont()
{
    constexpr const char* fileName = "GeneralUser-GS.sf2";
    auto app = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

    for (auto candidate : { app.getChildFile ("Contents/Resources/soundfonts").getChildFile (fileName),   // macOS bundle
                            exe.getSiblingFile ("soundfonts").getChildFile (fileName) })                  // Windows / Linux
        if (candidate.existsAsFile())
            return candidate;

   #ifdef PURRLUDE_SOUNDFONT
    return juce::File (PURRLUDE_SOUNDFONT);
   #else
    return {};
   #endif
}

void SoundFontPlugin::preload()
{
    getFonts().getApp().startLoading();
}

bool SoundFontPlugin::isAvailable()
{
    return getFonts().getApp().get() != nullptr;
}

void SoundFontPlugin::useSoundFont (const juce::File& file)
{
    getFonts().getApp().choose (file);
}

juce::File SoundFontPlugin::getChosenSoundFont()
{
    return getFonts().getApp().getChosen();
}

juce::String SoundFontPlugin::getSoundFontError()
{
    return getFonts().getApp().getError();
}

int SoundFontPlugin::getSoundFontGeneration()
{
    return loadsFinished.load();
}

std::vector<SoundFontPlugin::Preset> SoundFontPlugin::getPresets()
{
    return getFonts().getApp().getPresets();
}

std::vector<SoundFontPlugin::Preset> SoundFontPlugin::getPresets (const juce::File& file)
{
    if (file == juce::File())
        return getPresets();

    auto* source = getFonts().find (file);
    return source != nullptr ? source->getPresets() : std::vector<Preset>();
}

SoundFontPlugin::TrackFontState SoundFontPlugin::getState (const juce::File& file)
{
    auto* source = file != juce::File() ? getFonts().find (file) : nullptr;

    if (source == nullptr || source->isLoading())
        return { source != nullptr, {} };

    return { false, source->getError() };
}

} // namespace purrlude
