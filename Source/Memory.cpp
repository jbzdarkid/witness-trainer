#include "pch.h"
#include "Memory.h"
#include <psapi.h>
#include <tlhelp32.h>

Memory::Memory(const std::wstring& processName) : _processName(processName) {}

Memory::~Memory() {
    StopHeartbeat();
    if (_thread.joinable()) _thread.join();

    if (_handle != nullptr) {
        CloseHandle(_handle);
    }
}

void Memory::StartHeartbeat(HWND window, UINT message) {
    if (_threadActive) return;
    _threadActive = true;
    _thread = std::thread([sharedThis = shared_from_this(), window, message]{
        SetCurrentThreadName(L"Heartbeat");

        // Run the first heartbeat before setting trainerHasStarted, to detect if we are attaching to a game already in progress.
        sharedThis->Heartbeat(window, message);
        sharedThis->_trainerHasStarted = true;

        while (sharedThis->_threadActive) {
            std::this_thread::sleep_for(s_heartbeat);
            sharedThis->Heartbeat(window, message);
        }
    });
    _thread.detach();
}

void Memory::StopHeartbeat() {
    _threadActive = false;
}

void Memory::BringToFront() {
    ShowWindow(_hwnd, SW_RESTORE); // This handles fullscreen mode
    SetForegroundWindow(_hwnd); // This handles windowed mode
}

bool Memory::IsForeground() {
    return GetForegroundWindow() == _hwnd;
}

HWND Memory::GetProcessHwnd(DWORD pid) {
    struct Data {
        DWORD pid;
        HWND hwnd;
    };
    Data data = Data{pid, NULL};

    BOOL result = EnumWindows([](HWND hwnd, LPARAM data) {
        DWORD pid;
        GetWindowThreadProcessId(hwnd, &pid);
        DWORD targetPid = reinterpret_cast<Data*>(data)->pid;
        if (pid == targetPid) {
            reinterpret_cast<Data*>(data)->hwnd = hwnd;
            return FALSE; // Stop enumerating
        }
        return TRUE; // Continue enumerating
    }, (LPARAM)&data);

    return data.hwnd;
}

void Memory::Heartbeat(HWND window, UINT message) {
    if (!_handle) {
        Initialize(); // Initialize promises to set _handle only on success
        if (!_handle) {
            // Couldn't initialize, definitely not running
            PostMessage(window, message, ProcStatus::NotRunning, NULL);
            return;
        }
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(_handle, &exitCode);
    if (exitCode != STILL_ACTIVE) {
        // Process has exited, clean up. We only need to reset _handle here -- its validity is linked to all other class members.
        _computedAddresses.Clear();
        _handle = nullptr;

        _nextStatus = ProcStatus::Started;
        PostMessage(window, message, ProcStatus::Stopped, NULL);
        // Wait for the process to fully close; otherwise we might accidentally re-attach to it.
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        return;
    }

    __int64 entityManager = ReadData<__int64>({_globals}, 1)[0];
    if (entityManager == 0) {
        // Game hasn't loaded yet, we're still sitting on the launcher
        PostMessage(window, message, ProcStatus::NotRunning, NULL);
        return;
    }

    // To avoid obtaining the HWND for the launcher, we wait to determine HWND until after the entity manager is allocated (the main game has started).
    if (_hwnd == NULL) {
        _hwnd = GetProcessHwnd(_pid);
    } else {
        // Under some circumstances the window can expire? Or the game re-allocates it? I have no idea.
        // Anyways, we check to see if the title is wrong, and if so, search for the window again.
        constexpr int TITLE_SIZE = sizeof(L"The Witness") / sizeof(wchar_t);
        wchar_t title[TITLE_SIZE] = {L'\0'};
        GetWindowTextW(_hwnd, title, TITLE_SIZE);
        if (wcsncmp(title, L"The Witness", TITLE_SIZE) != 0) _hwnd = GetProcessHwnd(_pid);
    }

    if (_hwnd == NULL) {
        assert(false, "Couldn't find the HWND for the game");
        return;
    }

    // New game causes the entity manager to re-allocate
    if (entityManager != _previousEntityManager) {
        _previousEntityManager = entityManager;
        _computedAddresses.Clear();
    }

    // Loading a game causes entities to be shuffled
    int loadCount = ReadAbsoluteData<int>({entityManager, _loadCountOffset}, 1)[0];
    if (_previousLoadCount != loadCount) {
        _previousLoadCount = loadCount;
        _computedAddresses.Clear();
    }

    int numEntities = ReadData<int>({_globals, 0x10}, 1)[0];
    if (numEntities != 400'000) {
        // New game is starting, do not take any actions.
        _nextStatus = ProcStatus::NewGame;
        return;
    }

    byte isLoading = ReadAbsoluteData<byte>({entityManager, _loadCountOffset - 0x4}, 1)[0];
    if (isLoading == 0x01) {
        // Saved game is currently loading, do not take any actions.
        _nextStatus = ProcStatus::Reload;
        return;
    }

    if (_trainerHasStarted == false) {
        // If it's the first time we started, and the game appears to be running, return "Running" instead of "Started".
        PostMessage(window, message, ProcStatus::Running, NULL);
    } else {
        // Else, report whatever status we last encountered.
        PostMessage(window, message, _nextStatus, NULL);
    }
    _nextStatus = ProcStatus::Running;
}

void Memory::Initialize() {
    HANDLE handle = nullptr;
    // First, get the handle of the process
    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    while (Process32NextW(snapshot, &entry)) {
        if (_processName == entry.szExeFile) {
            _pid = entry.th32ProcessID;
            handle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, _pid);
            break;
        }
    }
    if (!handle || !_pid) {
        // Game likely not opened yet. Don't spam the log.
        _nextStatus = ProcStatus::Started;
        return;
    }
    DebugPrint(L"Found " + _processName + L": PID " + std::to_wstring(_pid));

    _hwnd = NULL; // Will be populated later.

    std::tie(_baseAddress, _endOfModule) = DebugUtils::GetModuleBounds(handle);
    if (_baseAddress == 0) {
        DebugPrint("Couldn't locate base address");
        return;
    }

    // Clear out any leftover sigscans from consumers (e.g. the trainer)
    _sigScans.clear();

    AddSigScan({0x74, 0x41, 0x48, 0x85, 0xC0, 0x74, 0x04, 0x48, 0x8B, 0x48, 0x10}, [&](__int64 offset, int index, const std::vector<byte>& data) {
        _globals = Memory::ReadStaticInt(offset, index + 0x14, data);
    });

    AddSigScan({0x01, 0x00, 0x00, 0x66, 0xC7, 0x87}, [&](__int64 offset, int index, const std::vector<byte>& data) {
        _loadCountOffset = *(int*)&data[index-1];
    });

    // This little song-and-dance is because we need _handle in order to execute sigscans.
    // But, we use _handle to indicate success, so we need to reset it.
    // Note that these sigscans are very lightweight -- they are *only* the scans required to handle loading.
    _handle = handle;
    size_t failedScans = ExecuteSigScans(); // Will DebugPrint the failed scans.
    if (failedScans > 0) _handle = nullptr;
}

// These functions are much more generic than this witness-specific implementation. As such, I'm keeping them somewhat separated.

__int64 Memory::ReadStaticInt(__int64 offset, int index, const std::vector<byte>& data, size_t bytesToEOL) {
    // (address of next line) + (index interpreted as 4byte int)
    return offset + index + bytesToEOL + *(int*)&data[index];
}

// Small wrapper for non-failing scan functions
void Memory::AddSigScan(const std::vector<byte>& scanBytes, const ScanFunc& scanFunc) {
    _sigScans[scanBytes] = {false, [scanFunc](__int64 offset, int index, const std::vector<byte>& data) {
        scanFunc(offset, index, data);
        return true;
    }};
}

void Memory::AddSigScan2(const std::vector<byte>& scanBytes, const ScanFunc2& scanFunc) {
    _sigScans[scanBytes] = {false, scanFunc};
}

int find(const std::vector<byte>& data, const std::vector<byte>& search) {
    const byte* dataBegin = &data[0];
    const byte* searchBegin = &search[0];
    size_t maxI = data.size() - search.size();
    size_t maxJ = search.size();

    for (int i=0; i<maxI; i++) {
        bool match = true;
        for (size_t j=0; j<maxJ; j++) {
            if (*(dataBegin + i + j) == *(searchBegin + j)) {
                continue;
            }
            match = false;
            break;
        }
        if (match) return i;
    }
    return -1;
}

#define BUFFER_SIZE 0x10000 // 10 KB
size_t Memory::ExecuteSigScans() {
    size_t notFound = 0;
    for (const auto& [_, sigScan] : _sigScans) if (!sigScan.found) notFound++;
    std::vector<byte> buff;
    buff.resize(BUFFER_SIZE + 0x100); // padding in case the sigscan is past the end of the buffer

    for (uintptr_t i = _baseAddress; i < _endOfModule; i += BUFFER_SIZE) {
        SIZE_T numBytesWritten;
        if (!ReadProcessMemory(_handle, reinterpret_cast<void*>(i), &buff[0], buff.size(), &numBytesWritten)) continue;
        buff.resize(numBytesWritten);
        for (auto& [scanBytes, sigScan] : _sigScans) {
            if (sigScan.found) continue;
            int index = find(buff, scanBytes);
            if (index == -1) continue;
            sigScan.found = sigScan.scanFunc(i - _baseAddress, index, buff); // We're expecting i to be relative to the base address here.
            if (sigScan.found) notFound--;
        }
        if (notFound == 0) break;
    }

    if (notFound > 0) {
        DebugPrint("Failed to find " + std::to_string(notFound) + " sigscans:");
        for (const auto& [scanBytes, sigScan] : _sigScans) {
            if (sigScan.found) continue;
            std::stringstream ss;
            for (const auto b : scanBytes) {
                ss << "0x" << std::setw(2) << std::setfill('0') << std::hex << std::uppercase << static_cast<int16_t>(b) << ", ";
            }
            DebugPrint(ss.str());
        }
    } else {
        DebugPrint("Found all sigscans!");
    }

    _sigScans.clear();
    return notFound;
}

// Technically this is ReadChar*, but this name makes more sense with the return type.
std::string Memory::ReadString(std::vector<__int64> offsets) {
    __int64 charAddr = ReadData<__int64>(offsets, 1)[0];
    if (charAddr == 0) return ""; // Handle nullptr for strings
    
    std::vector<char> tmp;
    auto nullTerminator = tmp.begin(); // Value is only for type information.
    for (size_t maxLength = (1 << 6); maxLength < (1 << 10); maxLength *= 2) {
        tmp = ReadAbsoluteData<char>({charAddr}, maxLength);
        nullTerminator = std::find(tmp.begin(), tmp.end(), '\0');
        // If a null terminator is found, we will strip any trailing data after it.
        if (nullTerminator != tmp.end()) break;
    }
    return std::string(tmp.begin(), nullTerminator);
}

int32_t Memory::CallFunction(int64_t relativeAddress,
    const int64_t rcx, const int64_t rdx, const int64_t r8, const int64_t r9,
    const float xmm0, const float xmm1, const float xmm2, const float xmm3) {
    struct Arguments {
        uintptr_t address;
        int64_t rcx;
        int64_t rdx;
        int64_t r8;
        int64_t r9;
        float xmm0;
        float xmm1;
        float xmm2;
        float xmm3;
    };
    assert((uint64_t)relativeAddress < _baseAddress, "[Internal error] CallFunction must be called with an address *relative to* the base pointer.");
    Arguments args = {
        ComputeOffset({relativeAddress}),
        rcx, rdx, r8, r9,
        xmm0, xmm1, xmm2, xmm3,
    };

#define INSTRUCTION_SIZE 57

    if (!_functionPrimitive) {
        // Some C++ macro magic to look up the member offset of a given field.
        // For example, args.rcx is 8 bits from the start of the struct, so this would result in 0x08.
        #define OFFSET_OF(field) \
            static_cast<uint8_t>(((uint64_t)&args.##field - (uint64_t)&args.address) & 0x00000000000000FF)

        // This primitive contains both a series of instructions and a buffer for arguments.
        // This allows us to write the instructions once, and then just write our new arguments
        // whenever we need to make another call.
	    const uint8_t instructions[] = {
            0x48, 0xBB,                                 // mov rbx,  0 ; 0 will be replaced by the address of the arguments struct
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x48, 0x8B, 0x4B, OFFSET_OF(rcx),           // mov rcx,  args.rcx
            0x48, 0x8B, 0x53, OFFSET_OF(rdx),           // mov rdx,  args.rdx
            0x4C, 0x8B, 0x43, OFFSET_OF(r8),            // mov r8,   args.r8
            0x4C, 0x8B, 0x4B, OFFSET_OF(r9),            // mov r9,   args.r9
            0xF3, 0x0F, 0x7E, 0x43, OFFSET_OF(xmm0),    // mov xmm0, args.xmm0
            0xF3, 0x0F, 0x7E, 0x4B, OFFSET_OF(xmm1),    // mov xmm1, args.xmm1
            0xF3, 0x0F, 0x7E, 0x53, OFFSET_OF(xmm2),    // mov xmm2, args.xmm2
            0xF3, 0x0F, 0x7E, 0x5B, OFFSET_OF(xmm3),    // mov xmm3, args.xmm3
            0x48, 0x83, 0xEC, 0x48,                     // sub rsp,48 ; align the stack pointer for movss opcodes
            0xFF, 0x13,                                 // call [rbx]
            0x48, 0x83, 0xC4, 0x48,                     // add rsp,48
            0xC3,                                       // ret
        };
        static_assert(sizeof(instructions) == INSTRUCTION_SIZE, "The instruction size is required to be static for argument writing purposes.");

        // Allocate space for the instructions and arguments buffer,
        _functionPrimitive = AllocateArray(sizeof(instructions) + sizeof(Arguments));
        // Then update the instructions to load from the arguments buffer (assuming LE),
        *(uint64_t*)&instructions[2] = (_functionPrimitive + sizeof(instructions));
        // and finally write the completed instructions into the target process.
        WriteDataInternal(instructions, _functionPrimitive, sizeof(instructions));
    }

    // Then, we can write the arguments into the buffer, to be copied by the instructions.
    WriteDataInternal(&args, _functionPrimitive + INSTRUCTION_SIZE, sizeof(args));

    // Although I don't use it here, argument 5 (lpParameter) can be used to pass a single 8-byte integer to the target process.
    // Note that you cannot transfer data this way; if you pass a pointer it will point to memory in this process, not the target.
    HANDLE thread = CreateRemoteThread(_handle, NULL, 0, (LPTHREAD_START_ROUTINE)_functionPrimitive, 0, 0, 0);
	DWORD result = WaitForSingleObject(thread, INFINITE);

    // This will be the return value of the called function.
    int32_t exitCode = 0;
    static_assert(sizeof(DWORD) == sizeof(exitCode));
    GetExitCodeThread(thread, reinterpret_cast<LPDWORD>(&exitCode));
    return exitCode;
}

int32_t Memory::CallFunction(__int64 address, const std::string& str, __int64 rdx) {
    uintptr_t addr = AllocateArray(str.size());
    WriteDataInternal(&str[0], addr, str.size());
    return CallFunction(address, addr, rdx, 0, 0);
}

void Memory::ReadDataInternal(void* buffer, uintptr_t computedOffset, size_t bufferSize) {
    assert(bufferSize > 0, "[Internal error] Attempting to read 0 bytes");
    if (!_handle) return;
    // Ensure that the buffer size does not cause a read across a page boundary.
    if (bufferSize > 0x1000 - (computedOffset & 0x0000FFF)) {
        bufferSize = 0x1000 - (computedOffset & 0x0000FFF);
    }
    if (!ReadProcessMemory(_handle, (void*)computedOffset, buffer, bufferSize, nullptr)) {
        assert(false, "Failed to read process memory.");
    }
}

void Memory::WriteDataInternal(const void* buffer, uintptr_t computedOffset, size_t bufferSize) {
    assert(bufferSize > 0, "[Internal error] Attempting to write 0 bytes");
    if (!_handle) return;
    if (bufferSize > 0x1000 - (computedOffset & 0x0000FFF)) {
        bufferSize = 0x1000 - (computedOffset & 0x0000FFF);
    }
    if (!WriteProcessMemory(_handle, (void*)computedOffset, buffer, bufferSize, nullptr)) {
        assert(false, "Failed to write process memory.");
    }
}

uintptr_t Memory::ComputeOffset(std::vector<__int64> offsets, bool absolute) {
    assert(offsets.size() > 0, "[Internal error] Attempting to compute 0 offsets");
    assert(offsets.front() != 0, "[Internal error] First offset to compute was 0");

    // Leave off the last offset, since it will be either read/write, and may not be of type uintptr_t.
    const __int64 final_offset = offsets.back();
    offsets.pop_back();

    uintptr_t cumulativeAddress = (absolute ? 0 : _baseAddress);
    for (const __int64 offset : offsets) {
        cumulativeAddress += offset;

        // If the address was already computed, continue to the next offset.
        uintptr_t foundAddress = _computedAddresses.Find(cumulativeAddress);
        if (foundAddress != 0) {
            cumulativeAddress = foundAddress;
            continue;
        }

        // If the address was not yet computed, read it from memory.
        uintptr_t computedAddress = 0;
        if (!_handle) return 0;
        if (ReadProcessMemory(_handle, reinterpret_cast<LPCVOID>(cumulativeAddress), &computedAddress, sizeof(computedAddress), NULL) && computedAddress != 0) {
            // Success!
            _computedAddresses.Set(cumulativeAddress, computedAddress);
            cumulativeAddress = computedAddress;
            continue;
        }

        MEMORY_BASIC_INFORMATION info;
        assert(computedAddress != 0, "Attempted to dereference NULL!");
        if (!VirtualQuery(reinterpret_cast<LPVOID>(cumulativeAddress), &info, sizeof(info))) {
            assert(false, "Failed to read process memory, possibly because cumulativeAddress was too large.");
        } else {
            assert(info.State == MEM_COMMIT, "Attempted to read unallocated memory.");
            assert(info.AllocationProtect & 0xC4, "Attempted to read unreadable memory."); // 0xC4 = PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY | PAGE_READWRITE
            assert(false, "Failed to read memory for some as-yet unknown reason."); // Won't fire an assert dialogue if a previous one did, because that would be within 30s.
        }
        return 0;
    }
    return cumulativeAddress + final_offset;
}

uintptr_t Memory::AllocateArray(__int64 size) {
    return (uintptr_t)VirtualAllocEx(_handle, 0, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}