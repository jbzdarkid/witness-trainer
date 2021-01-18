#pragma once

class Trainer final {
public:
    static std::shared_ptr<Trainer> Create(const std::shared_ptr<Memory>& memory);
    ~Trainer();

    std::vector<float> GetPlayerPos();
    void SetPlayerPos(const std::vector<float>& pos);

    bool GetInfiniteChallenge();
    void SetInfiniteChallenge(bool enable);

    void Randomize(int32_t seed);
    void SetSeed(int32_t seed);

private:
    int RandInt(int min, int max);
    void AdjustRng(const std::vector<byte>& data, int offset, int index);

    int32_t s_seed;

    std::shared_ptr<Memory> _memory;

    // __int64 _noclipEnabled = 0;
    // __int64 _noclipSpeed = 0;
    // __int64 _cameraPos = 0;
    // __int64 _cameraAng = 0;
    // __int64 _fovCurrent = 0;
    __int64 _globals = 0;
    // __int64 _campaignState = 0;
    // __int64 _doorOpen = 0;
    // __int64 _doorClose = 0;
    // int _solvedTargetOffset = 0;
    // __int64 _powerOn = 0;
    // __int64 _walkAcceleration = 0;
    // __int64 _walkDeceleration = 0;
    // __int64 _runSpeed = 0;
    __int64 _recordPlayerUpdate = 0;
    // std::vector<__int64> _activePanelOffsets;
    // __int64 _mainMenuColor = 0;
    // std::vector<__int64> _consoleOpenTarget;
    // std::vector<__int64> _consoleWindowYB;
    // __int64 _wantCampaignSave = 0;
    // int _epNameOffset = 0;
};
