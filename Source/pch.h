#pragma once

#undef assert
#define assert(expr) \
    if (!(expr)) { \
        void ShowAssertDialogue(); \
        ShowAssertDialogue(); \
    }

#define _HAS_EXCEPTIONS 0

// We include the debug headers early, so we can override the assert macros.
#include <crtdbg.h>
#undef _RPT_BASE
#define _RPT_BASE(...) \
    void ShowAssertDialogue(); \
    ShowAssertDialogue();

#undef _RPT_BASE_W
#define _RPT_BASE_W(...) \
    void ShowAssertDialogue(); \
    ShowAssertDialogue();

#define WIN32_LEAN_AND_MEAN 1
#define VC_EXTRALEAN 1
#undef NDEBUG // Enable asserts (even in release mode)
#include <windows.h>
#undef min
#undef max

#include <map>
#include <functional>
#include <ctime>
#include <csignal>
#include <iomanip>
#include <sstream>
#include <thread>

#pragma warning (disable: 26451) // Potential arithmetic overflow
#pragma warning (disable: 26812) // Unscoped enum type

#include "Memory.h"
#include "DebugUtils.h"
