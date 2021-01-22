#include "pch.h"
#include <ImageHlp.h>
#include <Psapi.h>
#include "DebugUtils.h"
#include <iostream>

void DebugPrint(const std::string& text) {
#ifdef _DEBUG
    OutputDebugStringA(text.c_str());
    std::cout << text;
    if (text[text.size()-1] != '\n') {
        OutputDebugStringA("\n");
        std::cout << std::endl;
    }
#endif
}

void DebugPrint(const std::wstring& text) {
#ifdef _DEBUG
    OutputDebugStringW(text.c_str());
    std::wcout << text;
    if (text[text.size()-1] != '\n') {
        OutputDebugStringW(L"\n");
        std::wcout << std::endl;
    }
#endif
}

void SetCurrentThreadName(const wchar_t* name) {
    HMODULE module = GetModuleHandleA("Kernel32.dll");
    if (!module) return;

    typedef HRESULT (WINAPI *TSetThreadDescription)(HANDLE, PCWSTR);
    auto setThreadDescription = (TSetThreadDescription)GetProcAddress(module, "SetThreadDescription");
    if (!setThreadDescription) return;

    setThreadDescription(GetCurrentThread(), name);
}

std::wstring DebugUtils::GetStackTrace() {
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

    uint64_t baseAddress = GetBaseAddress(process);
    BOOL result = FALSE;
    do {
        ss << (stackFrame.AddrPC.Offset - baseAddress) << L' '; // Normalize offsets relative to the base address
        result = StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &stackFrame, &context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL);
    } while (result == TRUE);
    return ss.str();
}

std::wstring DebugUtils::version = L"(unknown)"; // Slight hack. Will be overwritten by main during startup.
time_t lastShownAssert = ~0ULL; // MAXINT
void DebugUtils::ShowAssertDialogue() {
    // Only show an assert every 30 seconds. This prevents assert loops inside the WndProc, as well as adding a grace period after an assert fires.
    if (time(nullptr) - lastShownAssert < 30) return;
    lastShownAssert = time(nullptr);
    std::wstring msg = L"WitnessRandomizer version " + version + L" has encountered an error.\n";
    msg += L"Please press Control C to copy this error, and paste it to darkid.\n";
    msg += GetStackTrace();
    MessageBox(NULL, msg.c_str(), L"WitnessTrainer encountered an error.", MB_TASKMODAL | MB_ICONHAND | MB_OK | MB_SETFOREGROUND);
}

uint64_t DebugUtils::GetBaseAddress(HANDLE process) {
    DWORD unused;
    HMODULE modules[1];
    EnumProcessModules(process, &modules[0], sizeof(HMODULE), &unused);
    MODULEINFO moduleInfo;
    GetModuleInformation(process, modules[0], &moduleInfo, sizeof(moduleInfo));
    return reinterpret_cast<uint64_t>(moduleInfo.lpBaseOfDll);
}

// Note: This function must work properly even in release mode, since we will need to generate callbacks for release exes.
void DebugUtils::RegenerateCallstack(const std::wstring& callstack) {
    if (callstack.empty()) return;
    uint64_t baseAddress = GetBaseAddress(GetCurrentProcess());
    std::vector<uint64_t> addrs;
    std::wstring buffer;
    for (const wchar_t c : callstack) {
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
    HANDLE process = GetCurrentProcess();
    SymInitialize(process, NULL, TRUE);
    std::wstringstream ss;
    ss << std::hex << std::showbase << std::nouppercase;
    for (auto addr : addrs) {
        ss << addr;
        if (symbolInfo != nullptr && SymFromAddr(process, addr, NULL, symbolInfo)) {
            ss << L' ' << symbolInfo->Name;
        }
        ss << L'\n';
    }

    OutputDebugStringW(ss.str().c_str());
    if (IsDebuggerPresent()) __debugbreak();
}
