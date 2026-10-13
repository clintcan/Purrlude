cmake_minimum_required(VERSION 3.22)

project(Purrlude VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS YES)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
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
add_subdirectory(${tracktion_engine_SOURCE_DIR}/modules ${CMAKE_BINARY_DIR}/tracktion_engine)

include(cmake/Verovio.cmake)

# ------------------------------------------------------------------------------
# Purrlude core: everything except the app shell, so tests can link it.
# An INTERFACE library because JUCE modules are compiled into each final target;
# a STATIC library linking them would produce duplicate symbols.
# ------------------------------------------------------------------------------
add_library(purrlude_core INTERFACE)

target_sources(purrlude_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/devices/BleMidiService.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/devices/BleMidiService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/devices/BluetoothMidiManager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/devices/BluetoothMidiManager.cpp
    $<$<PLATFORM_ID:Windows>:${CMAKE_CURRENT_SOURCE_DIR}/src/devices/BleMidiService_windows.cpp>
    $<$<PLATFORM_ID:Darwin>:${CMAKE_CURRENT_SOURCE_DIR}/src/devices/BleMidiService_mac.mm>
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/LiveInstrument.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/LiveInstrument.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/Project.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/Project.cpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notation/PdfDocument.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notation/PdfDocument.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notation/ScorePdf.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notation/ScorePdf.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/MidiDevicesPanel.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/MidiDevicesPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ScoreView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ScoreView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/TransportBar.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/TransportBar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ArrangementView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ArrangementView.cpp
)

target_include_directories(purrlude_core INTERFACE src)

target_compile_definitions(purrlude_core INTERFACE
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1
    # Development fallback for Verovio's fonts; release builds use the copy next to the app.
    PURRLUDE_VEROVIO_DATA="${PURRLUDE_VEROVIO_DATA}"
)

target_link_libraries(purrlude_core INTERFACE
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    purrlude_verovio
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
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Purrlude needs microphone access to record audio."
    BLUETOOTH_PERMISSION_ENABLED TRUE
    BLUETOOTH_PERMISSION_TEXT "Purrlude uses Bluetooth to connect to wireless MIDI devices."
    PLUGINHOST_AU TRUE
)

target_sources(Purrlude PRIVATE
    src/app/Main.cpp
    src/app/MainComponent.h
    src/app/MainComponent.cpp
)

target_compile_definitions(Purrlude PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Purrlude,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Purrlude,JUCE_VERSION>"
)

target_link_libraries(Purrlude PRIVATE
    purrlude_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

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

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------
if(PURRLUDE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
