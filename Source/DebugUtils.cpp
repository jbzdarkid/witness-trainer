#include "pch.h"
#include <iostream>
#include <ImageHlp.h>
#include <Psapi.h>
#include "DebugUtils.h"

#pragma push_macro("DebugPrint")
#undef DebugPrint
void DebugUtils::DebugPrint(const std::string& text) {
    OutputDebugStringA(text.c_str());
    std::cout << text;
    if (text[text.size()-1] != '\n') {
        OutputDebugStringA("\n");
        std::cout << '\n';
    }
}

void DebugUtils::DebugPrint(const std::wstring& text) {
    OutputDebugStringW(text.c_str());
    std::wcout << text;
    if (text[text.size()-1] != L'\n') {
        OutputDebugStringW(L"\n");
        std::wcout << L'\n';
    }
}
#pragma pop_macro("DebugPrint")

std::pair<uint64_t, uint64_t> DebugUtils::GetModuleBounds(HANDLE process, const std::wstring& moduleName) {
    DWORD requiredBytes = sizeof(HMODULE);
    std::vector<HMODULE> modules(1, nullptr);

    EnumProcessModules(process, &modules[0], sizeof(HMODULE) * (DWORD)modules.size(), &requiredBytes);
    modules.resize(requiredBytes / sizeof(HMODULE));
    EnumProcessModules(process, &modules[0], sizeof(HMODULE) * (DWORD)modules.size(), &requiredBytes);

    std::wstring baseName(moduleName.size(), '\0');
    for (const auto& module : modules)
    {
        int size = GetModuleBaseNameW(process, module, &baseName[0], sizeof(baseName));
        baseName.resize(size);
        if (baseName != moduleName) continue;

        MODULEINFO moduleInfo;
        GetModuleInformation(process, module, &moduleInfo, sizeof(moduleInfo));

        uint64_t startOfModule = reinterpret_cast<uint64_t>(moduleInfo.lpBaseOfDll);
        uint64_t endOfModule = startOfModule + moduleInfo.SizeOfImage;
        return {startOfModule, endOfModule};
    }

    return {};
}


void SetCurrentThreadName(const wchar_t* name) {
    HMODULE module = GetModuleHandleA("Kernel32.dll");
    if (!module) return;

    typedef HRESULT (WINAPI *TSetThreadDescription)(HANDLE, PCWSTR);
    auto setThreadDescription = (TSetThreadDescription)GetProcAddress(module, "SetThreadDescription");
    if (!setThreadDescription) return;

    setThreadDescription(GetCurrentThread(), name);
}

std::wstring GetStackTrace() {
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymInitialize(process, NULL, TRUE); // For some reason, this is required in order to make StackWalk work.
    CONTEXT context;
    RtlCaptureContext(&context);
    STACKFRAME64 stackFrame;
    stackFrame.AddrPC.Offset = context.Rip;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = context.Rbp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = context.Rsp;
    stackFrame.AddrStack.Mode = AddrModeFlat;

    std::wstringstream ss;
    ss << std::hex << std::showbase << std::nouppercase;

    uint64_t baseAddress = DebugUtils::GetModuleBounds(process, EXE_NAME).first;
    BOOL result = FALSE;
    do {
        ss << (stackFrame.AddrPC.Offset - baseAddress) << L' '; // Normalize offsets relative to the base address
        result = StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &stackFrame, &context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL);
    } while (result == TRUE);
    return ss.str();
}

void ShowAssertDialogue(const wchar_t* message) {
    // Only show an assert every 30 seconds. This prevents assert loops inside the WndProc, as well as adding a grace period after an assert fires.
    static time_t lastShownAssert = ~0ULL; // MAXINT
    if (time(nullptr) - lastShownAssert < 30) return;
    lastShownAssert = time(nullptr);
    std::wstring msg = EXE_NAME L" version " VERSION_STR L" has encountered an error.\n";
    msg += L"Please press Control C to copy this error, and paste it to darkid.\n";
    msg += message;
    msg += L"\n";
    msg += GetStackTrace();
    MessageBox(NULL, msg.c_str(), EXE_NAME L" encountered an error.", MB_TASKMODAL | MB_ICONHAND | MB_OK | MB_SETFOREGROUND);
}

// Note: This function must work properly even in release mode, since we will need to generate callbacks for release exes.
void RegenerateCallstack(const std::wstring& callstack) {
    if (callstack.empty()) return;
    if (callstack[0] != '0') return; // Callstacks should always start with '0x'
    HANDLE process = GetCurrentProcess();
    uint64_t baseAddress = DebugUtils::GetModuleBounds(process, EXE_NAME).first;
    std::vector<uint64_t> addrs;
    std::wstring buffer;
    for (const wchar_t c : callstack) {
        if (c == L'\0') break;
        if (c == L' ') {
            addrs.push_back(baseAddress + std::stoull(buffer, nullptr, 16));
            buffer.clear();
        } else {
            buffer += c;
            continue;
        }
    }
    if (buffer.size() > 0) {
        addrs.push_back(baseAddress + std::stoull(buffer, nullptr, 16));
    }

    // The SYMBOL_INFO struct ends with a buffer of size 1; to allocate a larger buffer, we need to allocate an extra MAX_SYM_NAME characters.
    auto symbolInfo = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(wchar_t));
    if (symbolInfo != nullptr) {
        symbolInfo->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbolInfo->MaxNameLen = MAX_SYM_NAME;
    }
    SymInitialize(process, NULL, TRUE);
    std::wstringstream ss;
    ss << std::hex << std::showbase << std::nouppercase;
    for (auto addr : addrs) {
        ss << addr;
        if (symbolInfo != nullptr && SymFromAddr(process, addr, NULL, symbolInfo)) {
            ss << L' ' << symbolInfo->Name;
            ss << L" +" << (addr - symbolInfo->Address);
        }
        ss << L'\n';
    }

    DebugPrint(ss.str());
    if (IsDebuggerPresent()) __debugbreak();
}