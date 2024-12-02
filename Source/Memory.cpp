#include "pch.h"
#include "Memory.h"
#include <psapi.h>
#include <tlhelp32.h>

std::shared_ptr<Memory> Memory::Create(const std::wstring& processName) {
    auto memory = std::make_shared<Memory>();

    // First, get the handle of the process
    if (!memory->_handle || !memory->_pid) {
        PROCESSENTRY32W entry;
        entry.dwSize = sizeof(entry);
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        while (Process32NextW(snapshot, &entry)) {
            if (processName == entry.szExeFile) {
                memory->_pid = entry.th32ProcessID;
                memory->_handle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, memory->_pid);
                break;
            }
        }
    }

    if (!memory->_handle || !memory->_pid) return nullptr;

    EnumWindows([](HWND hwnd, LPARAM data) {
        DWORD pid;
        GetWindowThreadProcessId(hwnd, &pid);
        Memory* self = reinterpret_cast<Memory*>(data);
        if (pid == self->_pid) {
            self->_hwnd = hwnd;
            return FALSE; // Stop enumerating
        }
        return TRUE; // Continue enumerating
    }, (LPARAM)memory.get());

    if (!memory->_hwnd) return nullptr;

    DebugPrint(L"Found " + processName + L": PID " + std::to_wstring(memory->_pid));

    std::tie(memory->_baseAddress, memory->_endOfModule) = DebugUtils::GetModuleBounds(memory->_handle);
    if (memory->_baseAddress == 0) {
        DebugPrint("Couldn't locate base address");
        return nullptr;
    }

    // Clear out any leftover sigscans from consumers (e.g. the trainer)
    memory->_sigScans.clear();
    return memory; // If we got here, we've got all the data we need.
}

Memory::~Memory() {
    if (_handle != nullptr) {
        for (const auto& interception : _interceptions) Unintercept(interception);
        for (void* addr : _allocations) {
            if (addr != nullptr) VirtualFreeEx(_handle, addr, 0, MEM_RELEASE);
        }
        CloseHandle(_handle);
    }
}

void Memory::BringToFront() {
    ShowWindow(_hwnd, SW_RESTORE); // This handles fullscreen mode
    SetForegroundWindow(_hwnd); // This handles windowed mode
}

// These functions are much more generic than this witness-specific implementation. As such, I'm keeping them somewhat separated.

int64_t Memory::ReadStaticInt(int64_t offset, int index, const std::vector<byte>& data, size_t bytesToEOL) {
    // (address of next line) + (index interpreted as 4byte int)
    return offset + index + bytesToEOL + *(int*)&data[index];
}

// Small wrapper for non-failing scan functions
void Memory::AddSigScan(const std::vector<byte>& scanBytes, const ScanFunc& scanFunc) {
    _sigScans[scanBytes] = {false, [scanFunc](int64_t offset, int index, const std::vector<byte>& data) {
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
std::string Memory::ReadString(const std::vector<int64_t>& offsets) {
    int64_t charAddr = ReadData<int64_t>(offsets, 1)[0];
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
    if (!thread) {
        assert(false, "Failed to create a thread in the process");
        return 0;
    }
	DWORD result = WaitForSingleObject(thread, INFINITE);

    // This will be the return value of the called function.
    int32_t exitCode = 0;
    static_assert(sizeof(DWORD) == sizeof(exitCode));
    GetExitCodeThread(thread, reinterpret_cast<LPDWORD>(&exitCode));
    return exitCode;
}

int32_t Memory::CallFunction(int64_t address, const std::string& str, int64_t rdx) {
    uintptr_t addr = AllocateArray(str.size());
    WriteDataInternal(&str[0], addr, str.size());
    return CallFunction(address, addr, rdx, 0, 0);
}

Memory::Interception Memory::Intercept(const char* name, int32_t firstLine, int32_t nextLine, const std::vector<byte>& data) {
    std::vector<byte> jumpBack = {
        0x56,                                   // push esi
        0xBE, INT_TO_BYTES(firstLine + 8),      // mov esi, firstLine + 8   ; pop esi in the jumpAway code below
        0xFF, 0xE6,                             // jmp esi
    };

    std::vector<byte> injectionBytes = {0x5E}; // pop esi (before executing code that might need it)
    injectionBytes.insert(injectionBytes.end(), data.begin(), data.end());
    injectionBytes.push_back(0x90); // Padding nop
    std::vector<byte> replacedCode = ReadAbsoluteData<byte>({firstLine}, nextLine - firstLine);
    injectionBytes.insert(injectionBytes.end(), replacedCode.begin(), replacedCode.end());
    injectionBytes.push_back(0x90); // Padding nop
    injectionBytes.insert(injectionBytes.end(), jumpBack.begin(), jumpBack.end());

    uintptr_t addr = AllocateArray(injectionBytes.size());
    assert(addr <= 0xFFFFFFFF, "Injected bytes were allocated outside of the int32 range");
    DebugPrint(name + std::string(" Source address: ") + DebugUtils::ToString(firstLine));
    DebugPrint(name + std::string(" Injection address: ") + DebugUtils::ToString(addr));
    WriteDataInternal(&injectionBytes[0], addr, injectionBytes.size());

    std::vector<byte> jumpAway = {
        0x56,                       // push esi
        0xBE, INT_TO_BYTES(addr),   // mov esi, addr
        0xFF, 0xE6,                 // jmp esi
        0x5E,                       // pop esi (we return to this opcode)
    };
    // We need enough space for the jump in the source code, >= 9 bytes
    assert(static_cast<int>(nextLine - firstLine) >= jumpAway.size(), "Source allocation is not sufficient for the jump instructions");

    // Fill any leftover space with nops
    for (size_t i=jumpAway.size(); i<static_cast<size_t>(nextLine - firstLine); i++) jumpAway.push_back(0x90);
    WriteAbsoluteData<byte>({firstLine}, jumpAway);

    return _interceptions.emplace_back(Interception{firstLine, replacedCode, addr});
}

void Memory::Unintercept(const Interception& intercept) {
    WriteAbsoluteData<byte>({intercept.firstLine}, intercept.replacedCode);
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

uintptr_t Memory::ComputeOffset(std::vector<int64_t> offsets, bool absolute) {
    assert(offsets.size() > 0, "[Internal error] Attempting to compute 0 offsets");
    assert(offsets.front() != 0, "[Internal error] First offset to compute was 0");

    // Leave off the last offset, since it will be either read/write, and may not be of type uintptr_t.
    const int64_t final_offset = offsets.back();
    offsets.pop_back();

    uintptr_t cumulativeAddress = (absolute ? 0 : _baseAddress);
    for (const int64_t offset : offsets) {
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

uintptr_t Memory::AllocateArray(int64_t size) {
    void* addr = VirtualAllocEx(_handle, 0, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    _allocations.push_back(addr);
    return (uintptr_t)addr;
}