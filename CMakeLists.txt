cmake_minimum_required(VERSION 3.22)

project(Purrlude VERSION 1.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS YES)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # Compile each target's files in parallel (Visual Studio otherwise builds them one at a time).
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/MP>)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

option(PURRLUDE_BUILD_TESTS "Build the Purrlude test runner" ON)

# ------------------------------------------------------------------------------
# Dependencies (fetched with CPM; set CPM_SOURCE_CACHE to share downloads)
# ------------------------------------------------------------------------------
include(cmake/CPM.cmake)

# JUCE is pinned to the exact commit Tracktion Engine v3.2.0 was released against.
CPMAddPackage(
    NAME JUCE
    URL https://github.com/juce-framework/JUCE/archive/19edd538429c93d277bf95b55aaa7e3eb545f951.tar.gz
)

CPMAddPackage(
    NAME tracktion_engine
    URL https://github.com/Tracktion/tracktion_engine/archive/refs/tags/v3.2.0.tar.gz
    DOWNLOAD_ONLY YES
)

# Tracktion's bundled crill calls __wfe, which GCC's arm_acle.h doesn't declare (Clang's and
# MSVC's do), so it didn't build with GCC on 64-bit ARM Linux. With GCC it now issues the wfe
# instruction itself. Changed here rather than with a patch file: the patch program on Windows
# CI runners failed on one ("Assertation failed!").
set(crill_wait ${tracktion_engine_SOURCE_DIR}/modules/3rd_party/crill/impl/progressive_backoff_wait_impl.h)
file(READ ${crill_wait} crill_source)

if(NOT crill_source MATCHES "Purrlude:")
    string(REPLACE [=[                __wfe();
]=] [=[                // Purrlude: GCC's arm_acle.h has no __wfe (Clang's and MSVC's do), so it's
                // the instruction itself there.
               #if defined (_MSC_VER) || defined (__clang__)
                __wfe();
               #else
                __asm__ __volatile__ ("wfe");
               #endif
]=] crill_patched "${crill_source}")

    if(crill_patched STREQUAL crill_source)
        message(FATAL_ERROR "Couldn't find crill's __wfe call in ${crill_wait}: check it after a Tracktion upgrade")
    endif()

    file(WRITE ${crill_wait} "${crill_patched}")
endif()

include(cmake/TracktionMidiOutputRouter.cmake)
add_subdirectory(${tracktion_engine_SOURCE_DIR}/modules ${CMAKE_BINARY_DIR}/tracktion_engine)

include(cmake/Verovio.cmake)
include(cmake/SoundFonts.cmake)
include(cmake/OnnxRuntime.cmake)

# ------------------------------------------------------------------------------
# Purrlude core: everything except the app shell, so tests can link it.
# An INTERFACE library because JUCE modules are compiled into each final target;
# a STATIC library linking them would produce duplicate symbols.
# ------------------------------------------------------------------------------
# Device definitions (tone lists etc.), compiled in as binary data.
file(GLOB PURRLUDE_DEVICE_DEFINITIONS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/content/devices/*.json
                                                        ${CMAKE_CURRENT_SOURCE_DIR}/content/loops/*.json
                                                        ${CMAKE_CURRENT_SOURCE_DIR}/content/accompaniment/*.json)
juce_add_binary_data(purrlude_content
    HEADER_NAME PurrludeContent.h
    NAMESPACE PurrludeContent
    SOURCES ${PURRLUDE_DEVICE_DEFINITIONS}
)

add_library(purrlude_core INTERFACE)

target_sources(purrlude_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/devices/BleMidiPacket.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/devices/BleMidiPacket.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/devices/BleMidiService.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/devices/BleMidiService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/devices/BluetoothMidiManager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/devices/BluetoothMidiManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/devices/MidiDeviceSettings.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/devices/MidiDeviceSettings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/devices/ToneListener.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/devices/ToneListener.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/devices/DeviceLibrary.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/devices/DeviceLibrary.cpp
    $<$<PLATFORM_ID:Windows>:${CMAKE_CURRENT_SOURCE_DIR}/src/devices/BleMidiService_windows.cpp>
    $<$<PLATFORM_ID:Darwin>:${CMAKE_CURRENT_SOURCE_DIR}/src/devices/BleMidiService_mac.mm>
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/EngineSetup.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/EngineSetup.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/instruments/GeneralMidi.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/instruments/SoundFontPlugin.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/instruments/SoundFontPlugin.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/TrackInstrument.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/Automation.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/Automation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/DeviceChannels.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/DeviceChannels.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/TrackInstrument.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/TrackMix.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/TrackMix.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/MidiControls.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/MidiControls.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/LoopRange.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/LoopRange.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/SharedReverb.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/SharedReverb.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/Takes.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/Takes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/TrackEffects.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/TrackEffects.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/LiveInstrument.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/LiveInstrument.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/LoopLibrary.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/LoopLibrary.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/ClipEditing.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/ClipEditing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/ClipOrder.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/ClipOrder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/Accompaniment.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/Accompaniment.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/AudioExport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/AudioExport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/MidiImport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/MidiImport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/TimeSignature.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/TimeSignature.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/TempoChanges.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/TempoChanges.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/AudioImport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/AudioImport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/AudioInputs.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/AudioInputs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/analysis/PitchTracker.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/analysis/PitchTracker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/analysis/MelodyTranscriber.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/analysis/MelodyTranscriber.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/analysis/DrumTranscriber.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/analysis/DrumTranscriber.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/analysis/AnalysisAudio.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/analysis/PolyphonicTranscriber.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/analysis/PolyphonicTranscriber.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/analysis/OnnxRuntime.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/analysis/OnnxRuntime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/analysis/SourceSeparator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/analysis/SourceSeparator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/analysis/CentreSplit.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/analysis/CentreSplit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/analysis/LeadVocalSeparator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/analysis/LeadVocalSeparator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/AudioToMidi.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/AudioToMidi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/BluetoothMidiInput.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/BluetoothMidiInput.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/BluetoothMidiOutput.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/BluetoothMidiOutput.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/BluetoothMidiPorts.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/BluetoothMidiPorts.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/InstrumentSeparation.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/InstrumentSeparation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/VocalSplit.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/VocalSplit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/FileDownload.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/FileDownload.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/NoteEditing.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/NoteEditing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/PluginLibrary.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/PluginLibrary.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/Project.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/Project.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/ProjectFormat.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/ProjectFormat.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notation/Score.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notation/NotationBuilder.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notation/NotationBuilder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notation/MusicXmlWriter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notation/MusicXmlWriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notation/TrackNotation.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notation/TrackNotation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notation/ScoreRenderer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notation/ScoreRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notation/ScoreSvg.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notation/ScoreSvg.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notation/ChordDetection.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notation/ChordDetection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notation/ScoreDrawing.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notation/ScoreDrawing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notation/PdfDocument.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notation/PdfDocument.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notation/ScorePdf.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notation/ScorePdf.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/MidiDevicesPanel.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/MidiDevicesPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/LoopsPanel.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/LoopsPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/MidiLearnPanel.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/MidiLearnPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ExportAudioDialog.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ExportAudioDialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/SoundFontChooser.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/SoundFontChooser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/LevelMeter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/LevelMeter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/TimelineView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/TimelineView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ScoreView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ScoreView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/PianoRoll.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/PianoRoll.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/TransportBar.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/TransportBar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ControlChooser.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ControlChooser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/PluginWindows.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/PluginWindows.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/EffectsPanel.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/EffectsPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/TrackHeader.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/TrackHeader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ArrangementView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ArrangementView.cpp
)

target_include_directories(purrlude_core INTERFACE src)

target_compile_definitions(purrlude_core INTERFACE
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=1
    # Downloads (the separation model, when the user asks): curl on Linux (NEEDS_CURL on the
    # targets links it); macOS and Windows use their own networking.
    JUCE_USE_CURL=$<IF:$<PLATFORM_ID:Linux>,1,0>
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    # Reading MP3s for audio import. JUCE's decoder, under JUCE's licence; the MP3 patents
    # expired in 2017. (There's no MP3 encoder: export offers WAV, FLAC and Ogg.)
    JUCE_USE_MP3AUDIOFORMAT=1
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1
    # Development fallback for Verovio's fonts; release builds use the copy next to the app.
    PURRLUDE_VEROVIO_DATA="${PURRLUDE_VEROVIO_DATA}"
    PURRLUDE_SOUNDFONT="${PURRLUDE_SOUNDFONT}"
)

target_link_libraries(purrlude_core INTERFACE
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_cryptography
    purrlude_verovio
    purrlude_tinysoundfont
    purrlude_content
    purrlude_models
    purrlude_onnxruntime
)

if(WIN32)
    # WinRT MIDI makes connected Bluetooth LE MIDI devices show up as JUCE MIDI ports;
    # WindowsApp provides the WinRT APIs used by BleMidiService_windows.cpp.
    target_compile_definitions(purrlude_core INTERFACE JUCE_USE_WINRT_MIDI=1)
    target_link_libraries(purrlude_core INTERFACE WindowsApp)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(purrlude_core INTERFACE atomic)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_compile_options(purrlude_core INTERFACE "-fno-aligned-allocation")
    # CoreBluetooth discovers BLE MIDI devices; CoreMIDI's Bluetooth driver then owns the link.
    target_link_libraries(purrlude_core INTERFACE "-framework CoreBluetooth" "-framework CoreMIDI")
endif()

# ------------------------------------------------------------------------------
# Application
# ------------------------------------------------------------------------------
juce_add_gui_app(Purrlude
    PRODUCT_NAME "Purrlude"
    COMPANY_NAME "Purrlude"
    BUNDLE_ID "com.purrlude.app"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/branding/purrlude-icon.png"   # made by tools/branding/make_icon.py
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Purrlude needs microphone access to record audio."
    BLUETOOTH_PERMISSION_ENABLED TRUE
    BLUETOOTH_PERMISSION_TEXT "Purrlude uses Bluetooth to connect to wireless MIDI devices."
    PLUGINHOST_AU TRUE
    NEEDS_CURL TRUE
)

# The logo (made by tools/branding/make_icon.py) and acknowledgements, for the splash screen and
# About window.
juce_add_binary_data(purrlude_branding
    HEADER_NAME PurrludeBranding.h
    NAMESPACE PurrludeBranding
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/branding/purrlude-logo.jpg
            ${CMAKE_CURRENT_SOURCE_DIR}/content/ACKNOWLEDGEMENTS.txt
)

target_sources(Purrlude PRIVATE
    src/app/Main.cpp
    src/app/Splash.h
    src/app/Splash.cpp
    src/app/AboutWindow.h
    src/app/AboutWindow.cpp
    src/app/DocScreenshots.h
    src/app/DocScreenshots.cpp
    src/app/MainComponent.h
    src/app/MainComponent.cpp
)

target_compile_definitions(Purrlude PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Purrlude,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Purrlude,JUCE_VERSION>"
)

target_link_libraries(Purrlude PRIVATE
    purrlude_core
    purrlude_branding
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

purrlude_bundle_onnxruntime(Purrlude)
purrlude_bundle_model_licenses(Purrlude)

# Purrlude's own license and the acknowledgements join them (About > License Files...).
if(APPLE)
    set(PURRLUDE_LICENSES_DEST "$<TARGET_BUNDLE_CONTENT_DIR:Purrlude>/Resources/licenses")
else()
    set(PURRLUDE_LICENSES_DEST "$<TARGET_FILE_DIR:Purrlude>/licenses")
endif()

add_custom_command(TARGET Purrlude POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PURRLUDE_LICENSES_DEST}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${PURRLUDE_LICENSES_DEST}/LICENSE-Purrlude.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/content/ACKNOWLEDGEMENTS.txt" "${PURRLUDE_LICENSES_DEST}/ACKNOWLEDGEMENTS.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PURRLUDE_SOUNDFONT_LICENSE}" "${PURRLUDE_LICENSES_DEST}/LICENSE-GeneralUser-GS.txt"
    COMMENT "Copying Purrlude's license and acknowledgements"
    VERBATIM
)
purrlude_bundle_demucs(Purrlude)

# Verovio's fonts and glyph files ship with the app (see ScoreRenderer::findResourceDirectory).
if(APPLE)
    set(PURRLUDE_VEROVIO_DEST "$<TARGET_BUNDLE_CONTENT_DIR:Purrlude>/Resources/verovio")
else()
    set(PURRLUDE_VEROVIO_DEST "$<TARGET_FILE_DIR:Purrlude>/verovio")
endif()

add_custom_command(TARGET Purrlude POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PURRLUDE_VEROVIO_DATA}" "${PURRLUDE_VEROVIO_DEST}"
    COMMENT "Copying Verovio resources"
    VERBATIM
)

# The GM SoundFont and its license (see SoundFontPlugin::findSoundFont and content/LICENSES.md).
if(APPLE)
    set(PURRLUDE_SOUNDFONT_DEST "$<TARGET_BUNDLE_CONTENT_DIR:Purrlude>/Resources/soundfonts")
else()
    set(PURRLUDE_SOUNDFONT_DEST "$<TARGET_FILE_DIR:Purrlude>/soundfonts")
endif()

add_custom_command(TARGET Purrlude POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PURRLUDE_SOUNDFONT_DEST}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PURRLUDE_SOUNDFONT}" "${PURRLUDE_SOUNDFONT_DEST}/GeneralUser-GS.sf2"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PURRLUDE_SOUNDFONT_LICENSE}" "${PURRLUDE_SOUNDFONT_DEST}/LICENSE-GeneralUser-GS.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/content/LICENSES.md" "${PURRLUDE_SOUNDFONT_DEST}/LICENSES.md"
    COMMENT "Copying the General MIDI SoundFont"
    VERBATIM
)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------
if(PURRLUDE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
