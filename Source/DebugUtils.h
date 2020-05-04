#pragma once
#include <string>

class DebugUtils final
{
public:
    static void RegenerateCallstack(const std::wstring& callstack);
    static void ShowAssertDialogue();
    static std::wstring version;

private:
    static uint64_t GetBaseAddress();
    static std::wstring GetStackTrace();
    static bool s_isShowingError;
};

void DebugPrint(const std::string& text);
inline void ShowAssertDialogue() {
    DebugUtils::ShowAssertDialogue();
}