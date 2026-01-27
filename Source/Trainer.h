#pragma once
#include <memory>
#include <vector>

class Memory;

class Trainer final {
public:
    static std::shared_ptr<Trainer> Create(const std::shared_ptr<Memory>& memory);
    ~Trainer();

    bool GetNoclip();
    std::vector<float> GetPlayerPos();
    int GetHealth();
    int GetMaxHealth();
    int GetCharge();
    int GetMaxCharge();
    bool GetGodMode();

    void SetNoclip(bool enable);
    void SetPlayerPos(const std::vector<float>& pos);
    void SetHealth(int health);
    void SetMaxHealth(int maxHealth);
    void SetCharge(int charge);
    void SetMaxCharge(int maxCharge);
    void SetGodMode(bool enable);

private:
    std::shared_ptr<Memory> _memory;
};
