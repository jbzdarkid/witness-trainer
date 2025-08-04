#pragma once

#define W(x)    W_(x)
#define W_(x)   L ## x
#undef assert
#define assert(expr, message) \
    if (!(expr)) { \
        void ShowAssertDialogue(const wchar_t*); \
        ShowAssertDialogue(W_(message)); \
    }

#define CLAMP(value, min, max) \
    ((value) < min ? (value) : ((value) > max ? max : (value)))

#define _HAS_EXCEPTIONS 0

// We include the debug headers early, so we can override the assert macros.
#include <crtdbg.h>
#undef _RPT_BASE
#define _RPT_BASE(...) \
    void ShowAssertDialogue(const wchar_t*); \
    ShowAssertDialogue(nullptr);

#undef _RPT_BASE_W
#define _RPT_BASE_W(...) \
    void ShowAssertDialogue(const wchar_t*); \
    ShowAssertDialogue(nullptr);

#define WIN32_LEAN_AND_MEAN 1
#define VC_EXTRALEAN 1
#undef NDEBUG // Enable asserts (even in release mode)
#include <windows.h>
#undef min
#undef max

#include <map>
#include <functional>
#include <cmath>
#include <ctime>
#include <csignal>
#include <iomanip>
#include <sstream>
#include <thread>

#pragma warning (disable: 26451) // Potential arithmetic overflow
#pragma warning (disable: 26812) // Unscoped enum type

#include "Memory.h"
#include "DebugUtils.h"
