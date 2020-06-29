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
    void StartHeartbeat(HWND window, UINT message);
    void BringToFront();
    bool IsForeground();

    Memory(const Memory& memory) = delete;
    Memory& operator=(const Memory& other) = delete;

    // lineLength is the number of bytes from the given index to the end of the instruction. Usually, it's 4.
    static __int64 ReadStaticInt(__int64 offset, int index, const std::vector<byte>& data, size_t lineLength = 4);
    using ScanFunc = std::function<void(__int64 offset, int index, const std::vector<byte>& data)>;
    void AddSigScan(const std::vector<byte>& scanBytes, const ScanFunc& scanFunc);
    [[nodiscard]] size_t ExecuteSigScans();

    template<class T>
    inline std::vector<T> ReadData(const std::vector<__int64>& offsets, size_t numItems) {
        std::vector<T> data(numItems, 0);
        ReadDataInternal(&data[0], offsets, numItems * sizeof(T));
        return data;
    }
    std::string ReadString(std::vector<__int64> offsets);

    template <class T>
    inline void WriteData(const std::vector<__int64>& offsets, const std::vector<T>& data) {
        WriteDataInternal(&data[0], offsets, sizeof(T) * data.size());
    }


private:
    void Heartbeat(HWND window, UINT message);
    void Initialize();

    void ReadDataInternal(void* buffer, const std::vector<__int64>& offsets, size_t bufferSize);
    void WriteDataInternal(const void* buffer, const std::vector<__int64>& offsets, size_t bufferSize);
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
    __int64 _previousEntityManager = 0;
    int _previousLoadCount = 0;
    ProcStatus _nextStatus = ProcStatus::Started;
    bool _isSafe = false; // Whether or not it is safe to read memory from the process

    // Parts of Read / Write / Sigscan
    std::map<uintptr_t, uintptr_t> _computedAddresses;
    struct SigScan {
        bool found = false;
        ScanFunc scanFunc;
    };
    std::map<std::vector<byte>, SigScan> _sigScans;
};
