#pragma once

// Note: Little endian
#define INT_TO_BYTES(val) \
    static_cast<byte>((val & 0x000000FF) >> 0x00), \
    static_cast<byte>((val & 0x0000FF00) >> 0x08), \
    static_cast<byte>((val & 0x00FF0000) >> 0x10), \
    static_cast<byte>((val & 0xFF000000) >> 0x18)

enum class ChallengeState {
    Stopped,
    Running,
    Solved,
};

class Trainer final {
public:
    static std::shared_ptr<Trainer> Create(const std::shared_ptr<Memory>& memory);
    bool Init();

    void SetPlayerPos(const std::vector<float>& pos);
    void SetMainMenuColor(bool enable);
    void SetMainMenuState(bool open);

    bool GetInfiniteChallenge();
    void SetInfiniteChallenge(bool enable);

    bool GetMkChallenge();
    void SetMkChallenge(bool enable);

    uint32_t GetSeed();
    void SetSeed(uint32_t seed);
    void RandomizeSeed();

    double GetGameTime();
    ChallengeState GetChallengeState();

private:
    void AdjustRng(const std::vector<byte>& data, int64_t offset, int index);

    std::shared_ptr<Memory> _memory;

    int64_t _globals = 0;
    int64_t _infiniteChallenge = 0;
    int64_t _challengeSeed = 0;
    int64_t _mainMenuColor = 0;
    int32_t _durationTotal = 0;
    int64_t _mkChallenge = 0;
    int64_t _menuOpenTarget = 0;
    int64_t _gameTime = 0;
    int32_t _powerOffOnFail = 0;
    int32_t _solvedOffset = 0;
    int32_t _elapsedTimeOffset = 0;
    int32_t _recordPowerOffset = 0;

    int64_t _rng2 = 0;
};
