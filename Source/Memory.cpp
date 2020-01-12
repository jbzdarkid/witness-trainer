#include "pch.h"
#include "Memory.h"
#include <psapi.h>
#include <tlhelp32.h>

#undef PROCESSENTRY32
#undef Process32Next

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

void Memory::Heartbeat(HWND window, WPARAM wParam) {
    if (!_handle && !Initialize()) {
        // Couldn't initialize, definitely not running
        PostMessage(window, WM_COMMAND, wParam, (LPARAM)ProcStatus::NotRunning);
        return;
    }

    DWORD exitCode = 0;
    assert(_handle);
    GetExitCodeProcess(_handle, &exitCode);
    if (exitCode != STILL_ACTIVE) {
        // Process has exited, clean up.
        _computedAddresses.clear();
        _handle = NULL;
        // Wait for the process to fully close; otherwise we might accidentally re-attach to it.
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        PostMessage(window, WM_COMMAND, wParam, (LPARAM)ProcStatus::NotRunning);
        return;
    }

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
            _handle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, entry.th32ProcessID);
            break;
        }
    }
    if (!_handle) {
        std::cerr << "Couldn't find " << _processName.c_str() << ", is it open?" << std::endl;
        return false;
    }

    // Next, get the process base address
    DWORD numModules;
    std::vector<HMODULE> moduleList(1024);
    EnumProcessModulesEx(_handle, &moduleList[0], static_cast<DWORD>(moduleList.size()), &numModules, 3);

    std::wstring name(1024, '\0');
    for (DWORD i = 0; i < numModules / sizeof(HMODULE); i++) {
        int length = GetModuleBaseNameW(_handle, moduleList[i], &name[0], static_cast<DWORD>(name.size()));
        name.resize(length);
        if (_processName == name) {
            _baseAddress = (uintptr_t)moduleList[i];
            break;
        }
    }
    if (_baseAddress == 0) {
        std::cerr << "Couldn't locate base address" << std::endl;
        return false;
    }

    return true;
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

int Memory::ExecuteSigScans(int blockSize) {
    int notFound = static_cast<int>(_sigScans.size());
    for (int i=0; i<0x300000; i+=0x1000) {
        std::vector<byte> data = ReadData<byte>({i}, 0x1100);

        bool modified = false;
        for (auto& [scanBytes, sigScan] : _sigScans) {
            if (sigScan.found) continue;
            int index = find(data, scanBytes);
            if (index == -1) continue;
            if (sigScan.scanFunc(i, index, data)) modified = true;
            sigScan.found = true;
            notFound--;
        }
        if (modified) WriteData<byte>({i}, data);
    }
    return notFound;
}

void* Memory::ComputeOffset(std::vector<int> offsets) {
    // Leave off the last offset, since it will be either read/write, and may not be of type uintptr_t.
    int final_offset = offsets.back();
    offsets.pop_back();

    uintptr_t cumulativeAddress = _baseAddress;
    for (const int offset : offsets) {
        cumulativeAddress += offset;

        const auto search = _computedAddresses.find(cumulativeAddress);
        // This is an issue with re-randomization. Always. Just disable it in debug mode!
#ifdef NDEBUG
        if (search == std::end(_computedAddresses)) {
#endif
            // If the address is not yet computed, then compute it.
            uintptr_t computedAddress = 0;
            if (!ReadProcessMemory(_handle, reinterpret_cast<LPVOID>(cumulativeAddress), &computedAddress, sizeof(uintptr_t), NULL)) {
                MEMORY_THROW("Couldn't compute offset.", offsets);
            }
            if (computedAddress == 0) {
                MEMORY_THROW("Attempted to derefence NULL while computing offsets.", offsets);
            }
            _computedAddresses[cumulativeAddress] = computedAddress;
#ifdef NDEBUG
        }
#endif

        cumulativeAddress = _computedAddresses[cumulativeAddress];
    }
    return reinterpret_cast<void*>(cumulativeAddress + final_offset);
}
