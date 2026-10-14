// Prints a stack trace if the test runner crashes, so failures that only happen on a CI
// machine (e.g. a crash during shutdown on Windows) say where they happened.

#include <juce_core/juce_core.h>

#include <cstdio>
#include <cstring>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace
{
    // Writes straight to the OS: a crash during shutdown can come after the C runtime has torn
    // down stdio, and printing through it would then fault again inside the handler.
    void writeError (const char* text)
    {
       #if JUCE_WINDOWS
        DWORD written = 0;
        WriteFile (GetStdHandle (STD_ERROR_HANDLE), text, (DWORD) std::strlen (text), &written, nullptr);
       #else
        std::fputs (text, stderr);
        std::fflush (stderr);
       #endif
    }

    void printStackTrace (const char* what)
    {
        char header[128];
        std::snprintf (header, sizeof (header), "\n*** PurrludeTests crashed (%s). Stack trace:\n", what);
        writeError (header);
        writeError (juce::SystemStats::getStackBacktrace().toRawUTF8());
    }

   #if JUCE_WINDOWS
    // Runs before any other handler (Catch2 installs its own unhandled-exception filter, which
    // replaces JUCE's), and before the process can die without flushing anything.
    LONG CALLBACK onException (EXCEPTION_POINTERS* info)
    {
        auto code = info->ExceptionRecord->ExceptionCode;

        if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_STACK_OVERFLOW || code == EXCEPTION_ILLEGAL_INSTRUCTION)
        {
            // Report only the first crash: one inside this handler would otherwise recurse.
            static volatile LONG reported = 0;

            if (InterlockedExchange (&reported, 1) != 0)
                return EXCEPTION_CONTINUE_SEARCH;

            char text[256];
            auto length = std::snprintf (text, sizeof (text), "\n*** Exception 0x%08lx at %p",
                                         (unsigned long) code, info->ExceptionRecord->ExceptionAddress);

            if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2)
                length += std::snprintf (text + length, sizeof (text) - (size_t) length, " (%s address %p)",
                                         info->ExceptionRecord->ExceptionInformation[0] == 0 ? "reading" : "writing",
                                         (void*) info->ExceptionRecord->ExceptionInformation[1]);

            std::snprintf (text + length, sizeof (text) - (size_t) length, " on thread %lu", (unsigned long) GetCurrentThreadId());
            writeError (text);
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
