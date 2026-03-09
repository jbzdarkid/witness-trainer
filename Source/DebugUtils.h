#pragma once
#include <string>
#include <utility>

class DebugUtils final
{
public:
    // Returns [start of module, end of module)
    static std::pair<uint64_t, uint64_t> GetModuleBounds(HANDLE process, const std::wstring& moduleName);
    static void DebugPrint(const std::string& text);
    static void DebugPrint(const std::wstring& text);
};

void SetCurrentThreadName(const wchar_t* name);
void ShowAssertDialogue(const wchar_t* message);
void RegenerateCallstack(const std::wstring& callstack);

#ifdef _DEBUG
  #define DebugPrint(text) DebugUtils::DebugPrint(text)
#else
  #define DebugPrint(text)
#endif
