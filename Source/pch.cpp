#include "pch.h"
#include <csignal>
#include <ImageHlp.h>
#include <Psapi.h>
#include <algorithm>
// #include "Version.h"

void DebugPrint(const std::string& text) {
#ifndef NDEBUG
    OutputDebugStringA(text.c_str());
    std::cout << text;
    if (text[text.size()-1] != '\n') {
        OutputDebugStringA("\n");
        std::cout << std::endl;
    }
#endif
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

    std::wstringstream ss;
    BOOL result = FALSE;
    do {
        ss << std::hex << std::showbase << stackFrame.AddrPC.Offset << L' ';
        result = StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &stackFrame, &context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL);
    } while (result == TRUE);
    return ss.str();
}

bool s_isShowingError = false;
void ShowAssertDialogue() {
    if (s_isShowingError) return;
    Memory::PauseHeartbeat();
    std::wstring msg = L"WitnessTrainer version " VERSION_STR " has encountered an error.\n";
    msg += L"Click 'Yes' to ignore it and continue the program,\n";
    msg += L"or click 'No' to stop the program.\n\n";
    msg += L"In either case, please press Control C to copy this error,\n";
    msg += L"then paste it to darkid.\n";
    msg += GetStackTrace();
    s_isShowingError = true; // If there's an assert firing *within* the WndProc, then we need to make sure not to keep firing...
    const int action = MessageBox(NULL,
        msg.c_str(),
        L"WitnessTrainer encountered an error.",
        MB_TASKMODAL | MB_ICONHAND | MB_YESNO | MB_SETFOREGROUND);
    s_isShowingError = false;
    if (action == IDYES) { // User opts to ignore the exception, and continue execution
        Memory::ResumeHeartbeat();
        return;
    }
    // Else, we are aborting execution.
    if (IsDebuggerPresent()) __debugbreak();
    raise(SIGABRT);
    _exit(3);
}
