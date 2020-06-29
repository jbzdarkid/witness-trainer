#include "pch.h"
#include "Memory.h"
#include <psapi.h>
#include <tlhelp32.h>

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
            sharedThis->Heartbeat(window, message);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
            assert(false);
            return;
        }
    }

    // New game causes the entity manager to re-allocate
    if (entityManager != _previousEntityManager) {
        _previousEntityManager = entityManager;
        _computedAddresses.clear();
    }

    // Loading a game causes entities to be shuffled
    int loadCount = ReadData<int>({_globals, 0x0, _loadCountOffset}, 1)[0];
    if (_previousLoadCount != loadCount) {
        _previousLoadCount = loadCount;
        _computedAddresses.clear();
    }

    int numEntities = ReadData<int>({_globals, 0x10}, 1)[0];
    if (numEntities != 400'000) {
        // New game is starting, do not take any actions.
        _nextStatus = ProcStatus::NewGame;
        return;
    }

    byte isLoading = ReadData<byte>({_globals, 0x0, _loadCountOffset - 0x4}, 1)[0];
    if (isLoading == 0x01) {
        // Saved game is currently loading, do not take any actions.
        _nextStatus = ProcStatus::Reload;
        return;
    }

    SendMessage(window, message, _nextStatus, NULL);
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

    _handle = handle;
    size_t failedScans = ExecuteSigScans(); // Will DebugPrint the failed scans.
    if (failedScans > 0) {
        // This little song-and-dance is because we need _handle in order to execute sigscans.
        // But, we use _handle to indicate success, so we need to reset it.
        _handle = nullptr;
        return;
    }
}

// These functions are much more generic than this witness-specific implementation. As such, I'm keeping them somewhat separated.

__int64 Memory::ReadStaticInt(__int64 offset, int index, const std::vector<byte>& data, size_t lineLength) {
    // (address of next line) + (index interpreted as 4byte int)
    return offset + index + lineLength + *(int*)&data[index];
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

#define MAX_STRING 100
// Technically this is ReadChar*, but this name makes more sense with the return type.
std::string Memory::ReadString(std::vector<__int64> offsets) {
    offsets.push_back(0L); // Assume we were passed a char*, this is the actual char[]
    std::vector<char> tmp = ReadData<char>(offsets, MAX_STRING);
    std::string name(tmp.begin(), tmp.end());
    // Remove garbage past the null terminator (we read 100 chars, but the string was probably shorter)
    name.resize(strnlen_s(tmp.data(), tmp.size()));
    if (name.size() < tmp.size()) {
        DebugPrint("Buffer did not contain a null terminator, ergo this string is longer than 100 chars. Please change MAX_STRING.");
        assert(false);
    }
    return name;
}

void Memory::ReadDataInternal(void* buffer, const std::vector<__int64>& offsets, size_t bufferSize) {
    assert(bufferSize > 0);
    if (!_handle) return;
    if (!ReadProcessMemory(_handle, ComputeOffset(offsets), buffer, bufferSize, nullptr)) {
        DebugPrint("Failed to read process memory.");
        assert(false);
    }
}

void Memory::WriteDataInternal(const void* buffer, const std::vector<__int64>& offsets, size_t bufferSize) {
    assert(bufferSize > 0);
    if (!_handle) return;
    if (!WriteProcessMemory(_handle, ComputeOffset(offsets), buffer, bufferSize, nullptr)) {
        DebugPrint("Failed to write process memory.");
        assert(false);
    }
}

void* Memory::ComputeOffset(std::vector<__int64> offsets) {
    assert(offsets.size() > 0);
    assert(offsets.front() != 0);

    // Leave off the last offset, since it will be either read/write, and may not be of type uintptr_t.
    const __int64 final_offset = offsets.back();
    offsets.pop_back();

    uintptr_t cumulativeAddress = _baseAddress;
    for (const __int64 offset : offsets) {
        cumulativeAddress += offset;

        // If the address was already computed, continue to the next offset.
        const auto search = _computedAddresses.find(cumulativeAddress);
        if (search != std::end(_computedAddresses)) {
            cumulativeAddress = search->second;
            continue;
        }

        // If the address was not yet computed, read it from memory.
        uintptr_t computedAddress = 0;
        if (!_handle) return 0;
        if (!ReadProcessMemory(_handle, reinterpret_cast<LPCVOID>(cumulativeAddress), &computedAddress, sizeof(computedAddress), NULL)) {
            DebugPrint("Failed to read process memory.");
            assert(false);
            return 0;
        } else if (computedAddress == 0) {
            DebugPrint("Attempted to dereference NULL!");
            assert(false);
            return 0;
        }

        _computedAddresses[cumulativeAddress] = computedAddress;
        cumulativeAddress = computedAddress;
    }
    return reinterpret_cast<void*>(cumulativeAddress + final_offset);
}
