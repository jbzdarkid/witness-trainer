#pragma once
#include "ThreadSafeAddressMap.h"

using byte = unsigned char;

// Note: Little endian
#define LONG_TO_BYTES(val) \
    static_cast<byte>((val & 0x00000000000000FF) >> 0x00), \
    static_cast<byte>((val & 0x000000000000FF00) >> 0x08), \
    static_cast<byte>((val & 0x0000000000FF0000) >> 0x10), \
    static_cast<byte>((val & 0x00000000FF000000) >> 0x18), \
    static_cast<byte>((val & 0x000000FF00000000) >> 0x20), \
    static_cast<byte>((val & 0x0000FF0000000000) >> 0x28), \
    static_cast<byte>((val & 0x00FF000000000000) >> 0x30), \
    static_cast<byte>((val & 0xFF00000000000000) >> 0x38)

// Note: Little endian
#define INT_TO_BYTES(val) \
    static_cast<byte>((val & 0x000000FF) >> 0x00), \
    static_cast<byte>((val & 0x0000FF00) >> 0x08), \
    static_cast<byte>((val & 0x00FF0000) >> 0x10), \
    static_cast<byte>((val & 0xFF000000) >> 0x18)

#define ARGCOUNT(...) std::tuple_size<decltype(std::make_tuple(__VA_ARGS__))>::value

#define IF_GE(...) __VA_ARGS__, 0x72 // jb
#define IF_LT(...) __VA_ARGS__, 0x73 // jae
#define IF_NE(...) __VA_ARGS__, 0x74 // je
#define IF_EQ(...) __VA_ARGS__, 0x75 // jne
#define IF_GT(...) __VA_ARGS__, 0x76 // jbe
#define IF_LE(...) __VA_ARGS__, 0x77 // ja
#define THEN(...) ARGCOUNT(__VA_ARGS__), __VA_ARGS__
#define DO_WHILE_NOT(...) __VA_ARGS__, static_cast<byte>(-1 - ARGCOUNT(__VA_ARGS__))

class Memory final : public std::enable_shared_from_this<Memory> {
public:
    static std::shared_ptr<Memory> Create(const std::wstring& processName);

    ~Memory();
    void BringToFront();

    // bytesToEOL is the number of bytes from the given index to the end of the opcode. Usually, the target address is last 4 bytes, since it's the destination of the call.
    static int64_t ReadStaticInt(int64_t offset, int index, const std::vector<byte>& data, size_t bytesToEOL = 4);
    using ScanFunc = std::function<void(int64_t offset, int index, const std::vector<byte>& data)>;
    using ScanFunc2 = std::function<bool(int64_t offset, int index, const std::vector<byte>& data)>;
    void AddSigScan(const std::vector<byte>& scanBytes, const ScanFunc& scanFunc);
    void AddSigScan2(const std::vector<byte>& scanBytes, const ScanFunc2& scanFunc);
    [[nodiscard]] size_t ExecuteSigScans();

    uint64_t GetBaseAddress() const { return _baseAddress; }

    template<class T>
    inline std::vector<T> ReadData(const std::vector<int64_t>& offsets, size_t numItems) {
        assert(numItems > 0, "Attempted to read 0 items");
        std::vector<T> data(numItems);
        if (!_handle) return data;
        ReadDataInternal(&data[0], ComputeOffset(offsets), numItems * sizeof(T));
        return data;
    }
    template<class T>
    inline std::vector<T> ReadAbsoluteData(const std::vector<int64_t>& offsets, size_t numItems) {
        assert(numItems > 0, "Attempted to read 0 items");
        std::vector<T> data(numItems);
        if (!_handle) return data;
        ReadDataInternal(&data[0], ComputeOffset(offsets, true), numItems * sizeof(T));
        return data;
    }
    std::string ReadString(const std::vector<int64_t>& offsets);

    template <class T>
    inline void WriteData(const std::vector<int64_t>& offsets, const std::vector<T>& data) {
        WriteDataInternal(&data[0], ComputeOffset(offsets), sizeof(T) * data.size());
    }
    template <class T>
    inline void WriteAbsoluteData(const std::vector<int64_t>& offsets, const std::vector<T>& data) {
        WriteDataInternal(&data[0], ComputeOffset(offsets, true), sizeof(T) * data.size());
    }

    // This is the fully typed function -- you mostly won't need to call this. Use one of the simpler overloads below.
    int CallFunction(int64_t address,
        const int64_t rcx, const int64_t rdx, const int64_t r8, const int64_t r9,
        const float xmm0, const float xmm1, const float xmm2, const float xmm3);

    int CallFunction(int64_t address, int64_t rcx) { return CallFunction(address, rcx, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f); }
    int CallFunction(int64_t address, int64_t rcx, int64_t rdx, int64_t r8, int64_t r9) { return CallFunction(address, rcx, rdx, r8, r9, 0.0f, 0.0f, 0.0f, 0.0f); }
    int CallFunction(int64_t address, int64_t rcx, const float xmm1) { return CallFunction(address, rcx, 0, 0, 0, 0.0f, xmm1, 0.0f, 0.0f); }
    int CallFunction(int64_t address, const std::string& str, int64_t rdx);

    struct Interception {
        int32_t firstLine = 0;
        std::vector<byte> replacedCode;
        uintptr_t addr = 0;
    };
    Interception Intercept(const char* name, int32_t firstLine, int32_t nextLine, const std::vector<byte>& data);
    void Unintercept(const Interception& intercept);
    uintptr_t AllocateArray(int64_t size);

private:
    void ReadDataInternal(void* buffer, const uintptr_t computedOffset, size_t bufferSize);
    void WriteDataInternal(const void* buffer, uintptr_t computedOffset, size_t bufferSize);
    uintptr_t ComputeOffset(std::vector<int64_t> offsets, bool absolute = false);

    // Parts of Initialize / heartbeat
    HANDLE _handle = nullptr;
    DWORD _pid = 0;
    HWND _hwnd = NULL;
    uintptr_t _baseAddress = 0;
    uintptr_t _endOfModule = 0;

    // Parts of Read / Write / Sigscan / etc
    uintptr_t _functionPrimitive = 0;
    ThreadSafeAddressMap _computedAddresses;

    struct SigScan {
        bool found = false;
        ScanFunc2 scanFunc;
    };
    std::map<std::vector<byte>, SigScan> _sigScans;

    std::vector<void*> _allocations;

    std::vector<Interception> _interceptions;
};
