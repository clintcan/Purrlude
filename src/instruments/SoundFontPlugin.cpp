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
    const juce::Identifier programProperty { "program" };
    constexpr int maxVoices = 96;
    constexpr float outputGainDb = -3.0f;

    /** The SoundFont, loaded once and shared by every plugin (each plays a tsf_copy of it). */
    class SharedFont
    {
    public:
        void startLoading()
        {
            const std::lock_guard<std::mutex> lock (mutex);

            if (std::exchange (started, true))
                return;

            std::thread ([this]
            {
                tsf* loaded = nullptr;
                juce::MemoryBlock data;

                if (SoundFontPlugin::findSoundFont().loadFileAsData (data))
                    loaded = tsf_load_memory (data.getData(), (int) data.getSize());

                {
                    const std::lock_guard<std::mutex> l (mutex);
                    font = loaded;
                    done = true;
                }

                loadedCondition.notify_all();
            }).detach();
        }

        /** Waits for the load if it's still running. Null if the SoundFont is missing or broken. */
        tsf* get()
        {
            startLoading();
            std::unique_lock<std::mutex> lock (mutex);
            loadedCondition.wait (lock, [this] { return done; });
            return font;
        }

    private:
        std::mutex mutex;
        std::condition_variable loadedCondition;
        bool started = false, done = false;
        tsf* font = nullptr;   // lives for the whole process
    };

    SharedFont& getSharedFont()
    {
        static SharedFont* instance = new SharedFont();   // never destroyed: plugins may outlive statics
        return *instance;
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
    pendingProgram = program.get();
    preload();
}

SoundFontPlugin::~SoundFontPlugin()
{
    notifyListenersOfDeletion();
    deinitialise();
}

void SoundFontPlugin::initialise (const tracktion::PluginInitialisationInfo& info)
{
    auto* font = getSharedFont().get();

    if (font == nullptr)
        return;

    // Everything that allocates happens here, before rendering starts.
    auto* fresh = tsf_copy (font);
    tsf_set_output (fresh, TSF_STEREO_UNWEAVED, juce::roundToInt (info.sampleRate), outputGainDb);
    tsf_set_max_voices (fresh, maxVoices);

    for (int channel = 15; channel >= 0; --channel)   // highest first: allocates all 16 at once
        tsf_channel_set_presetnumber (fresh, channel, 0, 0);

    std::vector<float> freshScratch;
    auto frames = juce::jmax (512, info.blockSizeSamples);
    freshScratch.resize ((size_t) frames * 2);

    {
        const juce::SpinLock::ScopedLockType lock (synthLock);
        std::swap (synth, fresh);
        std::swap (scratch, freshScratch);
        scratchFrames = frames;
        applyProgram (program.get());
    }

    if (fresh != nullptr)
        tsf_close (fresh);
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

void SoundFontPlugin::applyProgram (int newProgram)
{
    // Channels already exist (see initialise), so this doesn't allocate.
    for (int channel = 0; channel < 16; ++channel)
    {
        if (newProgram == gm::drumKit)
            tsf_channel_set_presetnumber (synth, channel, 0, 1);
        else
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
    tracktion::copyPropertiesToCachedValues (v, program);
    pendingProgram = program.get();
}

void SoundFontPlugin::setProgram (int newProgram)
{
    newProgram = newProgram == gm::drumKit ? gm::drumKit : juce::jlimit (0, 127, newProgram);
    program = newProgram;
    pendingProgram = newProgram;
}

int SoundFontPlugin::getProgram() const
{
    return program.get();
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

} // namespace purrlude
