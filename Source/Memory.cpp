#include "pch.h"
#include "Memory.h"
#include <psapi.h>
#include <tlhelp32.h>

#undef PROCESSENTRY32
#undef Process32Next

bool Memory::__canThrow = false;
bool Memory::__isPaused = false;

Memory::Memory(const std::wstring& processName) : _processName(processName) {}

Memory::~Memory() {
    if (_threadActive) {
        _threadActive = false;
        _thread.join();
    }

    if (_handle != nullptr) {
        CloseHandle(_handle);
    }
}

void Memory::StartHeartbeat(HWND window, UINT message) {
    if (_threadActive) return;
    _threadActive = true;
    _thread = std::thread([sharedThis = shared_from_this(), window, message]{
        SetThreadDescription(GetCurrentThread(), L"Heartbeat");
        while (sharedThis->_threadActive) {
            if (!__isPaused) sharedThis->Heartbeat(window, message);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    _thread.detach();
}

void Memory::BringToFront() {
    SetForegroundWindow(_hwnd);
}

bool Memory::IsForeground() {
    return GetForegroundWindow() == _hwnd;
}

void Memory::Heartbeat(HWND window, UINT message) {
    if (!_handle) {
        Initialize(); // Initialize promises to set _handle only on success
        if (!_handle) {
            // Couldn't initialize, definitely not running
            SendMessage(window, message, ProcStatus::NotRunning, NULL);
            return;
        }
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(_handle, &exitCode);
    if (exitCode != STILL_ACTIVE) {
        // Process has exited, clean up. We only need to reset _handle here -- its validity is linked to all other class members.
        _computedAddresses.clear();
        _handle = nullptr;

        SendMessage(window, message, ProcStatus::Stopped, NULL);
        // Wait for the process to fully close; otherwise we might accidentally re-attach to it.
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        return;
    }

    MEMORY_TRY
        __int64 entityManager = ReadData<__int64>({_globals}, 1)[0];
        if (entityManager == 0) {
            // Game hasn't loaded yet, we're still sitting on the launcher
            SendMessage(window, message, ProcStatus::NotRunning, NULL);
            return;
        }

        // To avoid obtaining the HWND for the launcher, we wait to determine HWND until the game is loaded.
        if (_hwnd == 0) {
            EnumWindows([](HWND hwnd, LPARAM memory){
                DWORD pid;
                GetWindowThreadProcessId(hwnd, &pid);
                DWORD targetPid = reinterpret_cast<Memory*>(memory)->_pid;
                if (pid == targetPid) {
                    reinterpret_cast<Memory*>(memory)->_hwnd = hwnd;
                    return FALSE; // Stop enumerating
                }
                return TRUE; // Continue enumerating
            }, (LPARAM)this);
            if (_hwnd == 0) {
                DebugPrint("Couldn't find the HWND for the game");
                return;
            }
        }

        // New game causes the entity manager to re-allocate
        if (entityManager != _previousEntityManager) {
            // Only issue NewGame & clear addresses if this actually was a new game, rather than our first startup.
            if (_previousEntityManager != 0) {
                _computedAddresses.clear();
                SendMessage(window, message, ProcStatus::NewGame, NULL);
            }
            _previousEntityManager = entityManager;
            return;
        }

        byte isLoading = ReadData<byte>({_globals, 0x0, _loadCountOffset - 0x4}, 1)[0];
        if (isLoading == 0x01) {
            return; // Game is currently loading, do not take any actions.
        }

        int loadCount = ReadData<int>({_globals, 0x0, _loadCountOffset}, 1)[0];
        if (_previousLoadCount != loadCount) {
            _previousLoadCount = loadCount;
            _computedAddresses.clear();
            SendMessage(window, message, ProcStatus::Reload, NULL);
            return;
        }
    MEMORY_CATCH((void)0)

    if (_processWasStopped) {
        SendMessage(window, message, ProcStatus::Started, NULL);
        _processWasStopped = false;
    } else {
        SendMessage(window, message, ProcStatus::Running, NULL);
    }
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
        DebugPrint(L"Couldn't find " + _processName + L", is it open?");
        _processWasStopped = true;
        return;
    }
    DebugPrint(L"Found " + _processName + L": PID " + std::to_wstring(_pid));

    _hwnd = NULL; // Will be populated later.

    _baseAddress = DebugUtils::GetBaseAddress(handle);
    if (_baseAddress == 0) {
        DebugPrint("Couldn't locate base address");
        return;
    }

    // Clear sigscans to avoid duplication (or leftover sigscans from the trainer)
    _sigScans.clear();

    AddSigScan({0x74, 0x41, 0x48, 0x85, 0xC0, 0x74, 0x04, 0x48, 0x8B, 0x48, 0x10}, [&](__int64 offset, int index, const std::vector<byte>& data) {
        _globals = Memory::ReadStaticInt(offset, index + 0x14, data);
    });

    AddSigScan({0x01, 0x00, 0x00, 0x66, 0xC7, 0x87}, [&](__int64 offset, int index, const std::vector<byte>& data) {
        _loadCountOffset = *(int*)&data[index-1];
    });

    AddSigScan({0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x10, 0x48, 0x89, 0x78, 0x18, 0x48, 0x8B, 0x3D}, [&](__int64 offset, int index, const std::vector<byte>& data) {
        _campaignState = Memory::ReadStaticInt(offset, index + 0x27, data);
    });

    _handle = handle;
    size_t failedScans = ExecuteSigScans();
    assert(failedScans == 0); // ... If this starts failing, I can be more cautious here.
}

__int64 Memory::ReadStaticInt(__int64 offset, int index, const std::vector<byte>& data) {
    return offset + index + 0x4 + *(int*)&data[index]; // (address of next line) + (index interpreted as 4byte int)
}

void Memory::AddSigScan(const std::vector<byte>& scanBytes, const ScanFunc& scanFunc) {
    _sigScans[scanBytes] = {false, scanFunc};
}

int find(const std::vector<byte> &data, const std::vector<byte>& search, size_t startIndex = 0) {
    for (size_t i=startIndex; i<data.size() - search.size(); i++) {
        bool match = true;
        for (size_t j=0; j<search.size(); j++) {
            if (data[i+j] == search[j]) {
                continue;
            }
            match = false;
            break;
        }
        if (match) return static_cast<int>(i);
    }
    return -1;
}

#define BUFFER_SIZE 0x10000 // 10 KB
size_t Memory::ExecuteSigScans() {
    size_t notFound = 0;
    for (const auto& [_, sigScan] : _sigScans) if (!sigScan.found) notFound++;
    std::vector<byte> buff;
    buff.resize(BUFFER_SIZE + 0x100); // padding in case the sigscan is past the end of the buffer

    for (uintptr_t i = 0; i < 0x300000; i += BUFFER_SIZE) {
        SIZE_T numBytesWritten;
        if (!ReadProcessMemory(_handle, reinterpret_cast<void*>(_baseAddress + i), &buff[0], buff.size(), &numBytesWritten)) continue;
        buff.resize(numBytesWritten);
        for (auto& [scanBytes, sigScan] : _sigScans) {
            if (sigScan.found) continue;
            int index = find(buff, scanBytes);
            if (index == -1) continue;
            sigScan.scanFunc(i, index, buff); // We're expecting i to be relative to the base address here.
            sigScan.found = true;
            notFound--;
        }
        if (notFound == 0) return 0;
    }

    DebugPrint("Failed to find " + std::to_string(notFound) + " sigscans:");
    for (const auto& [scanBytes, sigScan] : _sigScans) {
        if (sigScan.found) continue;
        std::stringstream ss;
        for (const auto b : scanBytes) {
            ss << "0x" << std::setw(2) << std::setfill('0') << std::hex << std::uppercase << static_cast<int16_t>(b) << ", ";
        }
        DebugPrint(ss.str());
    }
    return notFound;
}

void* Memory::ComputeOffset(std::vector<__int64> offsets) {
    if (offsets.size() == 0 || offsets.front() == 0) {
        MEMORY_THROW("Attempted to compute using null base offset", offsets);
    }

    // Leave off the last offset, since it will be either read/write, and may not be of type uintptr_t.
    const __int64 final_offset = offsets.back();
    offsets.pop_back();

    uintptr_t cumulativeAddress = _baseAddress;
    for (const __int64 offset : offsets) {
        cumulativeAddress += offset;

        // Performance optimization. In release mode, we cache previous computed offsets.
        const auto search = _computedAddresses.find(cumulativeAddress);
        if (search != std::end(_computedAddresses)) {
            cumulativeAddress = search->second;
            continue;
        }

        // If the address is not yet computed, then compute it.
        uintptr_t computedAddress = 0;
        if (!ReadProcessMemory(_handle, reinterpret_cast<LPCVOID>(cumulativeAddress), &computedAddress, sizeof(computedAddress), NULL)) {
            MEMORY_THROW("Couldn't compute offset.", offsets);
        }
        if (computedAddress == 0) {
            MEMORY_THROW("Attempted to dereference NULL while computing offsets.", offsets);
        }
        _computedAddresses[cumulativeAddress] = computedAddress;
        cumulativeAddress = computedAddress;
    }
    return reinterpret_cast<void*>(cumulativeAddress + final_offset);
}
