#include "SoundFontPlugin.h"

#include "GeneralMidi.h"

#include <condition_variable>
#include <mutex>
#include <thread>

#define TSF_IMPLEMENTATION
#include <tsf.h>

namespace purrlude
{

namespace
{
    const juce::Identifier programProperty { "program" }, bankProperty { "bank" };
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

    /** The SoundFont, loaded on a background thread and shared by every plugin, each of which
        plays a tsf_copy of it. It's the bundled one unless the user chose another
        (SoundFontPlugin::useSoundFont); one that won't load is replaced by the bundled one.

        Deleted when JUCE shuts down, and waits for a load still in progress first: a loading
        thread left running into process exit crashes on Windows as statics are torn down.
    */
    class SharedFont final : public juce::DeletedAtShutdown
    {
    public:
        ~SharedFont() override
        {
            if (loader.joinable())
                loader.join();

            if (font != nullptr)
                tsf_close (font);

            clearSingletonInstance();
        }

        void startLoading()
        {
            const std::lock_guard<std::mutex> lock (mutex);

            if (! std::exchange (started, true))
                startLoader();
        }

        /** Loads another SoundFont (none: the bundled one). Plugins move to it when it's loaded. */
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

        /** Waits for the load if it's still running. Null if no SoundFont would load. */
        tsf* get (int* loadedGeneration = nullptr)
        {
            startLoading();
            std::unique_lock<std::mutex> lock (mutex);
            loadedCondition.wait (lock, [this] { return done; });

            if (loadedGeneration != nullptr)
                *loadedGeneration = generation;

            return font;
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

        int getGeneration() const   { return generation; }

        JUCE_DECLARE_SINGLETON_INLINE (SharedFont, false)

    private:
        void startLoader()   // with the mutex held
        {
            loader = std::thread ([this, file = chosen]
            {
                juce::String loadError;
                auto* loaded = file != juce::File() ? loadSoundFont (file, loadError) : nullptr;

                if (loaded == nullptr)
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
        juce::String error;
        std::atomic<int> generation { 0 };
    };

    SharedFont& getSharedFont()
    {
        return *SharedFont::getInstance();
    }

    /** Every plugin, to move them to a newly loaded SoundFont (message thread). */
    juce::Array<SoundFontPlugin*>& getPlugins()
    {
        static juce::Array<SoundFontPlugin*> plugins;
        return plugins;
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
    pendingProgram = pack (program.get(), bank.get());
    preload();
    getPlugins().add (this);
}

SoundFontPlugin::~SoundFontPlugin()
{
    getPlugins().removeAllInstancesOf (this);
    notifyListenersOfDeletion();
    deinitialise();
}

void SoundFontPlugin::initialise (const tracktion::PluginInitialisationInfo&)
{
    buildSynth();   // with Plugin's sampleRate and blockSizeSamples, already set
}

void SoundFontPlugin::buildSynth()
{
    int generation = 0;
    auto* font = getSharedFont().get (&generation);

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

    builtGeneration = generation;

    if (fresh != nullptr)
        tsf_close (fresh);
}

void SoundFontPlugin::fontChanged()
{
    for (auto* plugin : getPlugins())
        if (plugin->synth != nullptr && plugin->builtGeneration != getSharedFont().getGeneration())
            plugin->buildSynth();
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
    tracktion::copyPropertiesToCachedValues (v, program, bank);
    pendingProgram = pack (program.get(), bank.get());
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
    getSharedFont().startLoading();
}

bool SoundFontPlugin::isAvailable()
{
    return getSharedFont().get() != nullptr;
}

void SoundFontPlugin::useSoundFont (const juce::File& file)
{
    getSharedFont().choose (file);
}

juce::File SoundFontPlugin::getChosenSoundFont()
{
    return getSharedFont().getChosen();
}

juce::String SoundFontPlugin::getSoundFontError()
{
    return getSharedFont().getError();
}

int SoundFontPlugin::getSoundFontGeneration()
{
    return getSharedFont().getGeneration();
}

std::vector<SoundFontPlugin::Preset> SoundFontPlugin::getPresets()
{
    return getSharedFont().getPresets();
}

} // namespace purrlude
