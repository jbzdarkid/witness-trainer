#include "pch.h"
#include "Memory.h"
#include <psapi.h>
#include <tlhelp32.h>

Memory::~Memory() {
    if (_handle != nullptr) {
        CloseHandle(_handle);
    }
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

    EnumWindows([](HWND hwnd, LPARAM data) {
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

ProcStatus Memory::TryAttachToProcess() {
    // First, get the handle of the process. Note that we might attach before the main HWND is opened,
    // in which case we will save the 'attachment' and only retry for the HWND.
    if (_handle == nullptr) {
        HANDLE handle = nullptr;
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
        if (!handle) return ProcStatus::NotRunning;

        std::tie(_baseAddress, _endOfModule) = DebugUtils::GetModuleBounds(handle, _moduleName);
        if (_baseAddress == 0) return ProcStatus::NotRunning;

        BOOL wow64Process = false;
        IsWow64Process(handle, &wow64Process);
        _pointerSize = (wow64Process == TRUE) ? 4 : 8;
        _handle = handle; // Save the handle to indicate that we've correctly attached.
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(_handle, &exitCode);
    if (exitCode != STILL_ACTIVE) {
        // Process has exited, clean up.
        _handle = nullptr;
        _pid = 0;
        _hwnd = nullptr;
        _computedAddresses.Clear();

        // Reset the 'found' state on all sigscans, as they will (often) move when the game reloads.
        for (auto& sigScan : _sigScans) sigScan.found = false;

        return ProcStatus::Stopped;
    }

    if (_hwnd == nullptr) _hwnd = GetProcessHwnd(_pid);
    if (_hwnd == nullptr) return ProcStatus::NotRunning;

    return ProcStatus::Running;
}

__int64 Memory::ReadStaticInt(__int64 offset, int index, const std::vector<byte>& data, size_t bytesToEOL) {
    // (address of next line) + (index interpreted as 4byte int)
    return offset + index + bytesToEOL + *(int*)&data[index];
}

// Small wrapper for non-failing scan functions
void Memory::AddSigScan(const std::string& scanHex, const ScanFunc& scanFunc) {
    _sigScans.emplace_back(SigScan{false, scanHex, SigScan::GetScanBytes(scanHex), [scanFunc](__int64 offset, int index, const std::vector<byte>& data) {
        scanFunc(offset, index, data);
        return true;
    }});
}

void Memory::AddSigScan2(const std::string& scanHex, const ScanFunc2& scanFunc) {
    _sigScans.emplace_back(SigScan{false, scanHex, SigScan::GetScanBytes(scanHex), scanFunc});
}

std::vector<byte> Memory::SigScan::GetScanBytes(const std::string& scanHex) {
    std::vector<byte> bytes;
    byte b = 0x00;
    bool halfByte = false;
    for (char ch : scanHex) {
        if (ch == ' ') continue;

        static std::string HEX_CHARS = "0123456789ABCDEF";
        b *= 16;
        b += (byte)HEX_CHARS.find(ch);
        if (halfByte) bytes.push_back(b);
        halfByte = !halfByte;
    }
    assert(!halfByte, "[INTERNAL ERROR] Could not parse hex bytes");

    return bytes;
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
    for (const auto& sigScan : _sigScans) if (!sigScan.found) notFound++;
    if (notFound == 0) return 0; // Early exit in case we've already found all our scans
    std::vector<byte> buff;
    buff.resize(BUFFER_SIZE + 0x100); // padding in case the sigscan is past the end of the buffer

    for (uintptr_t i = _baseAddress; i < _endOfModule; i += BUFFER_SIZE) {
        SIZE_T numBytesWritten;
        if (!ReadProcessMemory(_handle, reinterpret_cast<void*>(i), &buff[0], buff.size(), &numBytesWritten)) continue;
        buff.resize(numBytesWritten);
        for (auto& sigScan : _sigScans) {
            if (sigScan.found) continue;
            int index = find(buff, sigScan.bytes);
            if (index == -1) continue;
            sigScan.found = sigScan.scanFunc(i, index, buff);
            if (sigScan.found) notFound--;
        }
        if (notFound == 0) break;
    }

    if (notFound > 0) {
        DebugPrint("Failed to find " + std::to_string(notFound) + " sigscans:");
        for (const auto& sigScan : _sigScans) {
            if (sigScan.found) continue;
            DebugPrint(sigScan.hex);
        }
    } else {
        DebugPrint("Found all sigscans!");
    }

    return notFound;
}

// Technically this is ReadChar*, but this name makes more sense with the return type.
std::string Memory::ReadString(const std::vector<__int64>& offsets, size_t pointerSize) {
    if (pointerSize == 0) pointerSize = _pointerSize; // Dynamic default value
    std::vector<byte> charAddrBytes = ReadData<byte>(offsets, pointerSize);
    charAddrBytes.resize(8);
    __int64 charAddr = *(__int64*)charAddrBytes.data();
    if (charAddr == 0) return ""; // Handle nullptr for strings

    std::vector<char> tmp;
    auto nullTerminator = tmp.begin(); // Value is only for type information.
    for (size_t maxLength = (1 << 6); maxLength < (1 << 10); maxLength *= 2) {
        tmp = ReadData<char>({(__int64)charAddr}, maxLength);
        nullTerminator = std::find(tmp.begin(), tmp.end(), '\0');
        // If a null terminator is found, we will strip any trailing data after it.
        if (nullTerminator != tmp.end()) break;
    }
    return std::string(tmp.begin(), nullTerminator);
}

int32_t Memory::CallFunction(int64_t address,
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
    Arguments args = {
        ComputeOffset({address}),
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
    if (!thread) {
        assert(thread, "[Internal error] Failed to allocate a thread in the target process");
        return 0;
    }
    WaitForSingleObject(thread, INFINITE);

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

void Memory::ClearComputedAddress(const std::vector<__int64>& offsets) {
    uintptr_t address = ComputeOffset(offsets);
    _computedAddresses.Remove(address);
}

void Memory::ClearAllComputedAddresses() {
    _computedAddresses.Clear();
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

uintptr_t Memory::ComputeOffset(const std::vector<__int64>& offsets) {
    assert(offsets.size() > 0, "[Internal error] Attempting to compute 0 offsets");
    assert(offsets.front() != 0, "[Internal error] First offset to compute was 0");

    // Leave off the last offset since it's the address of the actual data (and may not be of size _pointerSize).
    uintptr_t cumulativeAddress = 0;
    for (int i = 0; i < offsets.size() - 1; i++) {
        cumulativeAddress += offsets[i];

        // If the address was already computed, continue to the next offset.
        uintptr_t foundAddress = _computedAddresses.Find(cumulativeAddress);
        if (foundAddress != 0) {
            cumulativeAddress = foundAddress;
            continue;
        }

        // If the address was not yet computed, read it from memory.
        uintptr_t computedAddress = 0;
        if (!_handle) return 0;
        if (ReadProcessMemory(_handle, reinterpret_cast<LPCVOID>(cumulativeAddress), &computedAddress, _pointerSize, NULL) && computedAddress != 0) {
            // Success!
            _computedAddresses.Set(cumulativeAddress, computedAddress);
            cumulativeAddress = computedAddress;
            continue;
        }

        // ReadProcessMemory failed, investigate:
        MEMORY_BASIC_INFORMATION info;
        if (!VirtualQuery(reinterpret_cast<LPVOID>(cumulativeAddress), &info, sizeof(info))) {
            assert(false, "Failed to read process memory, possibly because cumulativeAddress was too large.");
        } else {
            assert(info.State == MEM_COMMIT, "Attempted to read unallocated memory.");
            assert(info.AllocationProtect & 0xC4, "Attempted to read unreadable memory."); // 0xC4 = PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY | PAGE_READWRITE
            assert(false, "Failed to read memory for some as-yet unknown reason."); // Won't fire an assert dialogue if a previous one did, because that would be within 30s.
        }
        return 0;
    }
    return cumulativeAddress + offsets.back();
}

uintptr_t Memory::ResolvePointerPath(const std::vector<__int64>& offsets) {
    uintptr_t cumulativeAddress = 0;
    for (__int64 offset : offsets) {
        cumulativeAddress += offset;

        if (!_handle) return 0;
        uintptr_t computedAddress = 0;
        if (!ReadProcessMemory(_handle, reinterpret_cast<LPCVOID>(cumulativeAddress), &computedAddress, _pointerSize, NULL)) return 0;
        if (cumulativeAddress == 0) return 0;
        cumulativeAddress = computedAddress;
    }

    return cumulativeAddress;
}

void Memory::Intercept(const std::string& name, __int64 firstLine, __int64 nextLine, const std::vector<byte>& data, bool writeOriginalCode) {
    std::vector<byte> jumpBack = {
        0x41, 0x53,                                 // push r11
        0x49, 0xBB, LONG_TO_BYTES(firstLine + 15),  // mov r11, firstLine + 15
        0x41, 0xFF, 0xE3,                           // jmp r11
    };

#pragma warning (push)
#pragma warning (disable: 4530)
    std::vector<byte> injectionBytes = {0x41, 0x5B}; // pop r11 (before executing code that might need it)
    injectionBytes.insert(injectionBytes.end(), data.begin(), data.end());
    injectionBytes.push_back(0x90); // Padding nop
    std::vector<byte> replacedCode = ReadData<byte>({firstLine}, nextLine - firstLine);
    if (writeOriginalCode) {
        injectionBytes.insert(injectionBytes.end(), replacedCode.begin(), replacedCode.end());
        injectionBytes.push_back(0x90); // Padding nop
    }
    injectionBytes.insert(injectionBytes.end(), jumpBack.begin(), jumpBack.end());
#pragma warning (pop)

    uintptr_t addr = AllocateArray(injectionBytes.size());
    WriteData<byte>({(__int64)addr}, injectionBytes);

    std::vector<byte> jumpAway = {
        0x41, 0x53,                         // push r11
        0x49, 0xBB, LONG_TO_BYTES(addr),    // mov r11, addr
        0x41, 0xFF, 0xE3,                   // jmp r11
        0x41, 0x5B,                         // pop r11 (we return to this opcode)
    };
    // We need enough space for the jump in the source code
    assert(static_cast<int>(nextLine - firstLine) >= jumpAway.size(), "[INTERNAL ERROR] Injection did not have enough space for jump away/jump back");

    // Fill any leftover space with nops
    for (size_t i=jumpAway.size(); i<static_cast<size_t>(nextLine - firstLine); i++) jumpAway.push_back(0x90);
    WriteData<byte>({firstLine}, jumpAway);

    _interceptions.push_back({name, firstLine, replacedCode, addr});
}

void Memory::Unintercept(const std::string& name) {
    auto search = std::find_if(_interceptions.begin(), _interceptions.end(), [&name](Interception i) { return i.name == name; });
    if (search != _interceptions.end()) return;
    Interception interception = *search;
    WriteData<byte>({interception.firstLine}, interception.replacedCode);
    VirtualFreeEx(_handle, (void*)interception.addr, 0, MEM_RELEASE);
}

uintptr_t Memory::AllocateArray(__int64 size) {
    return (uintptr_t)VirtualAllocEx(_handle, 0, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}