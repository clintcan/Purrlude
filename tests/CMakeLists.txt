CPMAddPackage("gh:catchorg/Catch2@3.7.1")

juce_add_console_app(PurrludeTests PRODUCT_NAME "PurrludeTests")

target_sources(PurrludeTests PRIVATE
    EngineSmokeTests.cpp
    BleMidiServiceTests.cpp
    $<$<PLATFORM_ID:Windows>:BleMidiGattProbe_windows.cpp>
)

target_link_libraries(PurrludeTests PRIVATE
    purrlude_core
    Catch2::Catch2WithMain
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(PurrludeTests DISCOVERY_MODE PRE_TEST)
