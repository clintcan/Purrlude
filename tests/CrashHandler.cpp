// Prints a stack trace if the test runner crashes, so failures that only happen on a CI
// machine (e.g. a crash during shutdown on Windows) say where they happened.

#include <juce_core/juce_core.h>

#include <cstdio>

namespace
{
    struct InstallCrashHandler
    {
        InstallCrashHandler()
        {
            juce::SystemStats::setApplicationCrashHandler ([] (void*)
            {
                std::fputs ("\n*** PurrludeTests crashed. Stack trace:\n", stderr);
                std::fputs (juce::SystemStats::getStackBacktrace().toRawUTF8(), stderr);
                std::fflush (stderr);
            });
        }
    };

    const InstallCrashHandler installCrashHandler;
}
