#pragma once
#include <string>
#include <utility>

class DebugUtils final
{
public:
    static void RegenerateCallstack(const std::wstring& callstack);
    static void ShowAssertDialogue(const wchar_t* message);
    static std::wstring version;
    // Returns [start of module, end of module)
    static std::pair<uint64_t, uint64_t> GetModuleBounds(HANDLE process);
    static void DebugPrint(const std::string& text);
    static void DebugPrint(const std::wstring& text);

private:
    static std::wstring GetStackTrace();
};

void SetCurrentThreadName(const wchar_t* name);
#ifdef _DEBUG
  #define DebugPrint(text) DebugUtils::DebugPrint(text)
#else
  #define DebugPrint(text)
#endif

inline void ShowAssertDialogue(const wchar_t* message) {
    DebugUtils::ShowAssertDialogue(message);
}
