// Prints a stack trace if the test runner crashes, so failures that only happen on a CI
// machine (e.g. a crash during shutdown on Windows) say where they happened.

#include <juce_core/juce_core.h>

#include <cstdio>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace
{
    void printStackTrace (const char* what)
    {
        std::fprintf (stderr, "\n*** PurrludeTests crashed (%s). Stack trace:\n", what);
        std::fputs (juce::SystemStats::getStackBacktrace().toRawUTF8(), stderr);
        std::fflush (stderr);
    }

   #if JUCE_WINDOWS
    // Runs before any other handler (Catch2 installs its own unhandled-exception filter, which
    // replaces JUCE's), and before the process can die without flushing anything.
    LONG CALLBACK onException (EXCEPTION_POINTERS* info)
    {
        auto code = info->ExceptionRecord->ExceptionCode;

        if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_STACK_OVERFLOW || code == EXCEPTION_ILLEGAL_INSTRUCTION)
        {
            std::fprintf (stderr, "\n*** Exception 0x%08lx at %p", (unsigned long) code, info->ExceptionRecord->ExceptionAddress);

            if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2)
                std::fprintf (stderr, " (%s address %p)", info->ExceptionRecord->ExceptionInformation[0] == 0 ? "reading" : "writing",
                              (void*) info->ExceptionRecord->ExceptionInformation[1]);

            std::fprintf (stderr, " on thread %lu", (unsigned long) GetCurrentThreadId());
            printStackTrace ("access violation");
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }
   #endif

    struct InstallCrashHandler
    {
        InstallCrashHandler()
        {
            // Unbuffered, so what was printed before a crash isn't lost with it.
            std::setvbuf (stdout, nullptr, _IONBF, 0);
            std::setvbuf (stderr, nullptr, _IONBF, 0);

            juce::SystemStats::setApplicationCrashHandler ([] (void*) { printStackTrace ("signal"); });

           #if JUCE_WINDOWS
            AddVectoredExceptionHandler (1, onException);
           #endif
        }
    };

    const InstallCrashHandler installCrashHandler;
}
