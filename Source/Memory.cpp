#include "pch.h"
#include "Memory.h"
#include <psapi.h>
#include <tlhelp32.h>

#undef PROCESSENTRY32
#undef Process32Next

bool Memory::__canThrow = false;

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

void Memory::StartHeartbeat(HWND window, WPARAM wParam, std::chrono::milliseconds beat) {
    if (_threadActive) return;
    _threadActive = true;
    _thread = std::thread([sharedThis = shared_from_this(), window, wParam, beat]{
        while (sharedThis->_threadActive) {
            sharedThis->Heartbeat(window, wParam);
            std::this_thread::sleep_for(beat);
        }
    });
    _thread.detach();
}

void Memory::BringToFront() {
    EnumWindows([](HWND hwnd, LPARAM targetPid){
        DWORD pid;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == (DWORD)targetPid) {
            SetForegroundWindow(hwnd);
            return FALSE; // Stop enumerating
        }
        return TRUE; // Continue enumerating
    }, (LPARAM)_pid);
}

void Memory::Heartbeat(HWND window, WPARAM wParam) {
    if (!_handle && !Initialize()) {
        // Couldn't initialize, definitely not running
        PostMessage(window, WM_COMMAND, wParam, (LPARAM)ProcStatus::NotRunning);
        return;
    }
    assert(_handle);

    DWORD exitCode = 0;
    GetExitCodeProcess(_handle, &exitCode);
    if (exitCode != STILL_ACTIVE) {
        // Process has exited, clean up.
        _computedAddresses.clear();
        _handle = NULL;
        PostMessage(window, WM_COMMAND, wParam, (LPARAM)ProcStatus::NotRunning);
        // Wait for the process to fully close; otherwise we might accidentally re-attach to it.
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        return;
    }

    MEMORY_TRY
        int64_t fullscreenEffectsManager = ReadData<int64_t>({_campaignState - 0x08}, 1)[0];
        if (fullscreenEffectsManager == 0) {
            // Game hasn't loaded yet, we're still sitting on the launcher
            PostMessage(window, WM_COMMAND, wParam, (LPARAM)ProcStatus::NotRunning);
            return;
        }

        int64_t timeOfSave = ReadData<int64_t>({_campaignState, 0x40}, 1)[0];
        if (timeOfSave != _lastTimeOfSave) {
            // Started a new game, loaded an save, or autosaved
            _lastTimeOfSave = timeOfSave;
            _computedAddresses.clear();
            PostMessage(window, WM_COMMAND, wParam, (LPARAM)ProcStatus::Reload);
            return;
        }
    MEMORY_CATCH((void)0)

    PostMessage(window, WM_COMMAND, wParam, (LPARAM)ProcStatus::Running);
}

[[nodiscard]]
bool Memory::Initialize() {
    // First, get the handle of the process
    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    while (Process32NextW(snapshot, &entry)) {
        if (_processName == entry.szExeFile) {
            _pid = entry.th32ProcessID;
            _handle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, _pid);
            break;
        }
    }
    if (!_handle) {
        std::cerr << "Couldn't find " << _processName.c_str() << ", is it open?" << std::endl;
        return false;
    }

    // Next, get the process base address
    std::vector<HMODULE> moduleList(1024);
    DWORD numModules = static_cast<DWORD>(moduleList.size());
    bool suceeded = EnumProcessModulesEx(_handle, &moduleList[0], numModules, &numModules, 3);
    if (!suceeded) return false;
    moduleList.resize(numModules);

    std::wstring name(1024, '\0');
    for (const auto& module : moduleList) {
        int length = GetModuleBaseNameW(_handle, module, &name[0], static_cast<DWORD>(name.size()));
        name.resize(length);
        if (_processName == name) {
            _baseAddress = (uintptr_t)module;
            break;
        }
    }
    if (_baseAddress == 0) {
        std::cerr << "Couldn't locate base address" << std::endl;
        return false;
    }

    AddSigScan({0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x10, 0x48, 0x89, 0x78, 0x18, 0x48, 0x8B, 0x3D}, [&](__int64 offset, int index, const std::vector<byte>& data) {
        _campaignState = ReadStaticInt(offset, index + 0x27, data);
    });
    size_t numFailures = ExecuteSigScans();
    if (numFailures > 0) return false;

    return true;
}

__int64 Memory::ReadStaticInt(__int64 offset, int index, const std::vector<byte>& data) {
    return offset + index + 0x4 + *(int*)&data[index]; // (address of next line) + (index interpreted as 4byte int)
}

void Memory::AddSigScan(const std::vector<byte>& scanBytes, const ScanFunc& scanFunc) {
    _sigScans[scanBytes] = {scanFunc, false};
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

size_t Memory::ExecuteSigScans() {
    size_t notFound = _sigScans.size();
    std::vector<byte> buff;
    buff.resize(0x10100);
    SIZE_T numBytesWritten;
    for (uintptr_t i = _baseAddress; i < _baseAddress + 0x300000; i += 0x10000) {
        if (!ReadProcessMemory(_handle, reinterpret_cast<void*>(i), &buff[0], buff.size(), &numBytesWritten)) continue;
        buff.resize(numBytesWritten);
        for (auto& [scanBytes, sigScan] : _sigScans) {
            if (sigScan.found) continue;
            int index = find(buff, scanBytes);
            if (index == -1) continue;
            sigScan.scanFunc(i, index, buff);
            sigScan.found = true;
            notFound--;
        }
        if (notFound == 0) break;
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

        const auto search = _computedAddresses.find(cumulativeAddress);
        if (search == std::end(_computedAddresses)) {
            // If the address is not yet computed, then compute it.
            uintptr_t computedAddress = 0;
            if (!ReadProcessMemory(_handle, reinterpret_cast<LPVOID>(cumulativeAddress), &computedAddress, sizeof(computedAddress), NULL)) {
                MEMORY_THROW("Couldn't compute offset.", offsets);
            }
            if (computedAddress == 0) {
                MEMORY_THROW("Attempted to derefence NULL while computing offsets.", offsets);
            }
            _computedAddresses[cumulativeAddress] = computedAddress;
        }

        cumulativeAddress = _computedAddresses[cumulativeAddress];
    }
    return reinterpret_cast<void*>(cumulativeAddress + final_offset);
}
