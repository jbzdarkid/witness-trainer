#pragma once
#include "ProcStatus.h"
#include <memory>
#include <vector>

class Memory;

class Trainer final : public std::enable_shared_from_this<Trainer> {
public:
    Trainer(std::shared_ptr<Memory> memory);
    void StartHeartbeat(HWND window, UINT message);
    bool HeartbeatActive() const { return _threadActive; }
    void StopHeartbeat() { _threadActive = false; }
    ~Trainer();

    bool GetNoclip();
    std::vector<float> GetPlayerPos();
    std::vector<float> GetPlayerAngle();
    int GetHealth();
    int GetMaxHealth();
    int GetCharge();
    int GetMaxCharge();
    bool GetGodMode();
    bool GetShowCollision();
    std::string GetLevelName();

    void SetNoclip(bool enable);
    void SetPlayerPos(const std::vector<float>& pos);
    void SetPlayerAngle(const std::vector<float>& angle);
    void SetHealth(int health);
    void SetMaxHealth(int maxHealth);
    void SetCharge(int charge);
    void SetMaxCharge(int maxCharge);
    void SetGodMode(bool enable);
    void SetShowCollision(bool enable);

private:
    ProcStatus Heartbeat();
    void OnGameStart();

    std::shared_ptr<Memory> _memory;
    bool _threadActive = false;
    std::thread _thread;
    bool _firstHeartbeat = true;

#ifdef NDEBUG
    static constexpr std::chrono::milliseconds s_heartbeat = std::chrono::milliseconds(100);
#else // Induce more stress in debug, to catch errors more easily.
    static constexpr std::chrono::milliseconds s_heartbeat = std::chrono::milliseconds(10);
#endif

    uintptr_t _previousCombatStats = 0;

    int _gameWorldPtr = 0;
    int _globalSettingsPtr = 0;
};
