#pragma once
enum ProcStatus : WPARAM {
    NotRunning,
    Started,
    Running,
    Reload,
    NewGame,
    Stopped
};

using byte = unsigned char;

// https://github.com/erayarslan/WriteProcessMemory-Example
// http://stackoverflow.com/q/32798185
// http://stackoverflow.com/q/36018838
// http://stackoverflow.com/q/1387064
// https://github.com/fkloiber/witness-trainer/blob/master/source/foreign_process_memory.cpp
class Memory final : public std::enable_shared_from_this<Memory> {
public:
    Memory(const std::wstring& processName);
    ~Memory();
    void StartHeartbeat(HWND window, UINT message, std::chrono::milliseconds beat = std::chrono::milliseconds(100));
    static void PauseHeartbeat();
    static void ResumeHeartbeat();
    void BringToFront();
    bool IsForeground();

    Memory(const Memory& memory) = delete;
    Memory& operator=(const Memory& other) = delete;

    static __int64 ReadStaticInt(__int64 offset, int index, const std::vector<byte>& data);
    using ScanFunc = std::function<void(__int64 offset, int index, const std::vector<byte>& data)>;
    void AddSigScan(const std::vector<byte>& scanBytes, const ScanFunc& scanFunc);
    [[nodiscard]] size_t ExecuteSigScans();

    template<class T>
    std::vector<T> ReadData(const std::vector<__int64>& offsets, size_t numItems) {
        assert(numItems);
        if (!_handle) MEMORY_THROW("Game has been shut down", offsets, numItems);
        std::vector<T> data;
        data.resize(numItems);
        if (!ReadProcessMemory(_handle, ComputeOffset(offsets), &data[0], sizeof(T) * numItems, nullptr)) {
            MEMORY_THROW("Failed to read data.", offsets, numItems);
        }
        return data;
    }

    // Technically this is ReadChar*, but this name makes more sense with the return type.
    std::string ReadString(std::vector<__int64> offsets) {
        offsets.push_back(0L); // Assume we were passed a char*, this is the actual char[]
        std::vector<char> tmp = ReadData<char>(offsets, 100);
        std::string name(tmp.begin(), tmp.end());
        // Remove garbage past the null terminator (we read 100 chars, but the string was probably shorter)
        name.resize(strnlen_s(tmp.data(), tmp.size()));
        assert(name.size() < tmp.size()); // Assert that there was a null terminator read.
        // Otherwise, this is a truncated string, and we will need to increase the '100' above.
        return name;
    }

    template <class T>
    void WriteData(const std::vector<__int64>& offsets, const std::vector<T>& data) {
        assert(data.size());
        if (!_handle) MEMORY_THROW("Game has been shut down", offsets);
        if (!WriteProcessMemory(_handle, ComputeOffset(offsets), &data[0], sizeof(T) * data.size(), nullptr)) {
            MEMORY_THROW("Failed to write data.", offsets, data.size());
        }
    }

    // Should only be modified by macros
    static bool __canThrow;
    static bool __isPaused;

private:
    void Heartbeat(HWND window, UINT message);
    [[nodiscard]] bool Initialize();
    void* ComputeOffset(std::vector<__int64> offsets);

    // Parts of the constructor / StartHeartbeat
    std::wstring _processName;
    bool _threadActive = false;
    std::thread _thread;

    // Parts of Initialize / heartbeat
    HANDLE _handle = nullptr;
    DWORD _pid = 0;
    HWND _hwnd = NULL;
    uintptr_t _baseAddress = 0;
    __int64 _globals = 0;
    int _loadCountOffset = 0;
    int _previousLoadCount = 0;
    bool _processWasStopped = false;

    // Parts of Read / Write / Sigscan
    std::map<uintptr_t, uintptr_t> _computedAddresses;
    struct SigScan {
        bool found = false;
        ScanFunc scanFunc;
    };
    std::map<std::vector<byte>, SigScan> _sigScans;
};
