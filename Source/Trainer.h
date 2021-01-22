#pragma once

// Note: Little endian
#define INT_TO_BYTES(val) \
    static_cast<byte>((val & 0x000000FF) >> 0x00), \
    static_cast<byte>((val & 0x0000FF00) >> 0x08), \
    static_cast<byte>((val & 0x00FF0000) >> 0x10), \
    static_cast<byte>((val & 0xFF000000) >> 0x18)

class Trainer final {
public:
    static std::shared_ptr<Trainer> Create(const std::shared_ptr<Memory>& memory);
    bool Init();

    void SetPlayerPos(const std::vector<float>& pos);

    bool GetInfiniteChallenge();
    void SetInfiniteChallenge(bool enable);

    bool GetMkChallenge();
    void SetMkChallenge(bool enable);

    bool IsChallengeSolved();
    float GetChallengeTimer();

    void SetSeed(uint32_t seed);
    uint32_t GetSeed();
    void RandomizeSeed();

    enum Offset {
        PowerOffOnFail,
        ElapsedTime,
        SolvedTarget,
    };
    int32_t GetOffset(Offset offset);

private:
    static inline int32_t LongToInt(int64_t orig) {
        assert(orig < std::numeric_limits<int32_t>::max());
        assert(orig > std::numeric_limits<int32_t>::min());
        return static_cast<int32_t>(orig);
    }

    void AdjustRng(const std::vector<byte>& data, int64_t offset, int index);

    std::shared_ptr<Memory> _memory;

    int32_t _globals = 0;
    int32_t _recordPlayerUpdate = 0;
    int32_t _doSuccessSideEffects = 0;
    int64_t _rng2 = 0;

    std::vector<int32_t> _challengePanels = {
        0x0A332, // Challenge Record Start
        0x0088E, // Challenge Easy Maze
        0x00BAF, // Challenge Hard Maze
        0x00BF3, // Challenge Stones Maze
        0x00C09, // Challenge Pedestal
        0x0051F, // Challenge Column Bottom Left
        0x00524, // Challenge Column Top Right
        0x00CDB, // Challenge Column Top Left
        0x00CD4, // Challenge Column Far Panel
        0x00C80, // Challenge Triple 1 Left
        0x00CA1, // Challenge Triple 1 Center
        0x00CB9, // Challenge Triple 1 Right
        0x00C22, // Challenge Triple 2 Left
        0x00C59, // Challenge Triple 2 Center
        0x00C68, // Challenge Triple 2 Right
        // 0x04CB3, // Challenge Left Timer
        // 0x04CB5, // Challenge Middle Timer
        // 0x04CB6, // Challenge Right Timer
        0x034EC, // Challenge Triangle
        0x034F4, // Challenge Triangle
        0x1C31A, // Challenge Left Pillar
        0x1C319, // Challenge Right Pillar
        // 0x0356B, // Challenge Vault Box
    };
};
