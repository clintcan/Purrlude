CPMAddPackage("gh:catchorg/Catch2@3.7.1")

juce_add_console_app(PurrludeTests PRODUCT_NAME "PurrludeTests")

target_sources(PurrludeTests PRIVATE
    EngineSmokeTests.cpp
    BleMidiServiceTests.cpp
    BluetoothMidiManagerTests.cpp
    LiveInstrumentTests.cpp
    ProjectTests.cpp
    NotationTests.cpp
    ScoreRenderTests.cpp
    $<$<PLATFORM_ID:Windows>:BleMidiGattProbe_windows.cpp>
)

target_link_libraries(PurrludeTests PRIVATE
    purrlude_core
    Catch2::Catch2WithMain
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

if(APPLE)
    # macOS privacy checks need a usage string before a command-line tool may use Bluetooth.
    target_link_options(PurrludeTests PRIVATE
        "LINKER:-sectcreate,__TEXT,__info_plist,${CMAKE_CURRENT_SOURCE_DIR}/PurrludeTests-Info.plist")
endif()

list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(PurrludeTests DISCOVERY_MODE PRE_TEST)
