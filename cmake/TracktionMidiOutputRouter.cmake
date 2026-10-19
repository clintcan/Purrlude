# Adds tracktion::MidiOutputDevice::Router: lets Purrlude send some MIDI outputs' messages itself
# instead of through their MIDI ports, which aren't opened. Windows' Bluetooth MIDI driver stops
# passing messages to a device after a while, so Purrlude writes a Bluetooth device's MIDI over its
# own connection (BluetoothMidiOutput). Tracktion has no other way in: every send, by the engine's
# playback or by fireMessage, ends in MidiOutputDevice::sendMessageNow, which writes to a juce::MidiOutput.
#
# Made with CMake rather than a patch file (see the crill change in CMakeLists.txt), once, and the
# configure stops if a line has moved after a Tracktion upgrade.

set(midi_out_dir ${tracktion_engine_SOURCE_DIR}/modules/tracktion_engine/playback/devices)

function(purrlude_edit_source file)
    file(READ ${file} source)

    if(source MATCHES "Purrlude:")
        return()
    endif()

    set(edited "${source}")
    math(EXPR last "${ARGC} - 1")

    foreach(i RANGE 1 ${last} 2)
        math(EXPR j "${i} + 1")
        set(from "${ARGV${i}}")
        set(to "${ARGV${j}}")
        string(FIND "${edited}" "${from}" at)

        if(at EQUAL -1)
            message(FATAL_ERROR "Couldn't find this in ${file} (check it after a Tracktion upgrade):\n${from}")
        endif()

        string(REPLACE "${from}" "${to}" edited "${edited}")
    endforeach()

    file(WRITE ${file} "${edited}")
endfunction()

purrlude_edit_source(${midi_out_dir}/tracktion_MidiOutputDevice.h
[=[    bool isMidi() const override                        { return true; }
]=]
[=[    bool isMidi() const override                        { return true; }

    /** Purrlude: lets the app send some devices' messages itself instead of through their MIDI
        ports, which then aren't opened. Set before such devices are opened; never deleted.
    */
    struct Router
    {
        virtual ~Router() = default;
        virtual bool handles (const juce::String& deviceName) = 0;
        virtual void send (const juce::String& deviceName, const juce::MidiMessage&) = 0;   // any thread
    };

    static inline std::atomic<Router*> router { nullptr };

    /** Purrlude: open, through its port or the router. */
    bool isOpenForSending() const                       { return outputDevice != nullptr || routed; }
]=]
[=[    bool softDevice = false;
]=]
[=[    bool softDevice = false;
    std::atomic<bool> routed { false };   // Purrlude: sent through the router
]=]
)

purrlude_edit_source(${midi_out_dir}/tracktion_MidiOutputDevice.cpp
[=[            else if (deviceInfo.identifier.isNotEmpty())
            {
                outputDevice = juce::MidiOutput::openDevice (deviceInfo.identifier);
]=]
[=[            else if (auto r = router.load(); r != nullptr && r->handles (getName()))
            {
                routed = true;   // Purrlude: no port
            }
            else if (deviceInfo.identifier.isNotEmpty())
            {
                outputDevice = juce::MidiOutput::openDevice (deviceInfo.identifier);
]=]
[=[    saveProps();

    if (outputDevice != nullptr)
]=]
[=[    saveProps();
    routed = false;   // Purrlude

    if (outputDevice != nullptr)
]=]
[=[    if (outputDevice == nullptr)
        return TRANS("Couldn't open the MIDI port");

    stop();
]=]
[=[    if (! isOpenForSending())
        return TRANS("Couldn't open the MIDI port");

    stop();
]=]
[=[    if (outputDevice != nullptr)
    {
        audioAdjustmentDelay = juce::roundToInt (2.0 * engine.getDeviceManager().getBlockSizeMs());
]=]
[=[    if (isOpenForSending())
    {
        audioAdjustmentDelay = juce::roundToInt (2.0 * engine.getDeviceManager().getBlockSizeMs());
]=]
[=[    if (outputDevice != nullptr)
    {
        if (playing)
]=]
[=[    if (isOpenForSending())
    {
        if (playing)
]=]
[=[    if (outputDevice != nullptr)
    {
        const juce::ScopedLock sl (noteOnLock);
]=]
[=[    if (isOpenForSending())
    {
        const juce::ScopedLock sl (noteOnLock);
]=]
[=[    if (outputDevice != nullptr)
        outputDevice->sendMessageNow (message);
]=]
[=[    if (outputDevice != nullptr)
        outputDevice->sendMessageNow (message);
    else if (auto r = router.load(); r != nullptr && routed)   // Purrlude
        r->send (getName(), message);
]=]
[=[getMidiOutput().outputDevice == nullptr]=]
[=[! getMidiOutput().isOpenForSending()]=]
[=[getMidiOutput().outputDevice != nullptr]=]
[=[getMidiOutput().isOpenForSending()]=]
)
