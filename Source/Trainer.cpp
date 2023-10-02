#include "pch.h"
#include "Trainer.h"
#include "Panels.h"

std::shared_ptr<Trainer> Trainer::Create(const std::shared_ptr<Memory>& memory) {
    auto trainer = std::make_shared<Trainer>();

    memory->AddSigScan({0x74, 0x41, 0x48, 0x85, 0xC0, 0x74, 0x04}, [trainer](int64_t offset, int index, const std::vector<byte>& data) {
        trainer->_globals = Memory::ReadStaticInt(offset, index + 0x14, data);
    });

    // Entity_Record_Player::update
    memory->AddSigScan({0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0xE9, 0xB3}, [trainer](int64_t offset, int index, const std::vector<byte>& data) {
        trainer->_infiniteChallenge = offset + index - 0x0C;
    });

    // do_success_side_effects
    memory->AddSigScan({0x49, 0x8B, 0xC8, 0x75}, [trainer](int64_t offset, int index, const std::vector<byte>& data) {
        trainer->_challengeSeed = offset + index - 0x14;
    });

    // This scan intentionally fails if the injection has been applied. It makes this a bit unstable for development, but adds safety in case another trainer is attached.
    // draw_menu_general
    memory->AddSigScan2({0x41, 0xB8, 0x61, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xD3}, [trainer](__int64 offset, int index, const std::vector<byte>& data) {
        for (; index > 0; index--) {
            if (data[index] == 0x44 && data[index + 8] == 0x74 && data[index + 9] == 0x10) {
                trainer->_mainMenuColor = offset + index;
                return true;
            }
        }
        return false;
    });

    // Entity_Record_Player::update
    memory->AddSigScan({0x0F, 0x2E, 0xC6, 0x74, 0x3B}, [trainer](__int64 offset, int index, const std::vector<byte>& data) {
        trainer->_durationTotal = *(int32_t*)&data[index - 0x4];
        trainer->_mkChallenge = offset + index - 8;
    });

    // update_main_menu
    memory->AddSigScan({0x74, 0x0B, 0x0F, 0x28, 0xD0}, [trainer](__int64 offset, int index, const std::vector<byte>& data) {
        trainer->_menuOpenTarget = Memory::ReadStaticInt(offset, index + 0x19, data);
    });

    // get_panel_color_cycle_factors
    memory->AddSigScan({0x83, 0xFA, 0x02, 0x7F, 0x3B, 0xF2, 0x0F, 0x10, 0x05}, [trainer](__int64 offset, int index, const std::vector<byte>& data) {
        trainer->_gameTime = Memory::ReadStaticInt(offset, index + 9, data);
    });

    // Entity_Multipanel::update
    memory->AddSigScan({0xE8, 0x68, 0x00, 0x00, 0x00, 0x83, 0xBB}, [trainer](__int64 offset, int index, const std::vector<byte>& data) {
        trainer->_elapsedTimeOffset = *(int32_t*)&data[index - 0x4];
    });
    
    // Entity_Record_Player::power_on
    memory->AddSigScan({0x48, 0x8B, 0x4B, 0x18, 0xE8, 0x2B, 0x00, 0x00, 0x00}, [trainer](int64_t offset, int index, const std::vector<byte>& data) {
        trainer->_recordPowerOffset = *(int32_t*)&data[index + 0x15];
    });

    // judge_panel
    memory->AddSigScan({0x74, 0x06, 0x80, 0x7D, 0x11, 0x00}, [trainer](int64_t offset, int index, const std::vector<byte>& data) {
        trainer->_powerOffOnFail = *(int32_t*)&data[index - 0xE];
    });

    // Entity_Machine_Panel::update
    memory->AddSigScan({0x44, 0x0F, 0x28, 0x44, 0x24, 0x70, 0x0F, 0x84, 0x9B, 0x00, 0x00, 0x00}, [trainer](int64_t offset, int index, const std::vector<byte>& data) {
        trainer->_solvedOffset = *(int32_t*)&data[index - 0x4];
    });
    
    // We need to save _memory before we exit, otherwise we can't destroy properly.
    trainer->_memory = memory;

    size_t numFailedScans = memory->ExecuteSigScans();
    if (numFailedScans != 0) return nullptr; // Sigscans failed, we'll try again later.

    if (!trainer->Init()) return nullptr; // Initialization failed

    trainer->SetMainMenuColor(true); // Recolor the menu
    return trainer;
}

// Modify an instruction to use RNG2 instead of main RNG
void Trainer::AdjustRng(const std::vector<byte>& data, int64_t offset, int index) {
    assert(Memory::ReadStaticInt(offset, index, data) == _globals + 0x10, "[Internal error] Attempted to adjust RNG for a non-RNG address");

    // We need to write a relative pointer here, so we just read the current value and add 0x20 (rng2 - rng) to avoid doing math.
    int32_t currentRngPtr = *(int32_t*)&data[index];
    _memory->WriteData<int32_t>({offset + index}, {currentRngPtr + 0x20});
}

static std::vector<int32_t> CHALLENGE_PANELS = {
    0x0088E, // Easy Maze
    0x00BAF, // Hard Maze
    0x00BF3, // Stones Maze
    0x00C09, // Pedestal
    0x0051F, // Column Bottom Left
    0x00524, // Column Top Right
    0x00CDB, // Column Top Left
    0x00CD4, // Column Far Panel
    0x00C80, // Triple 1 Left
    0x00CA1, // Triple 1 Center
    0x00CB9, // Triple 1 Right
    0x00C22, // Triple 2 Left
    0x00C59, // Triple 2 Center
    0x00C68, // Triple 2 Right
    0x034EC, // Triangle 6
    0x034F4, // Triangle 8
    0x1C31A, // Left Pillar
    0x1C319, // Right Pillar
};

bool Trainer::Init() {
    // Prevent challenge panels from turning off on failure. Otherwise, rerolling a panel could cause an RNG change.
    for (int32_t panel : CHALLENGE_PANELS) {
        _memory->WriteData<int32_t>({_globals, 0x18, panel * 8, _powerOffOnFail}, {0});
    }

    int64_t rng = _memory->ReadData<int64_t>({_globals + 0x10}, 1)[0];
    _rng2 = _memory->ReadData<int64_t>({_globals + 0x30}, 1)[0];
    if (_rng2 == rng + 4) return true; // Already injected

    // We have to set this before adjusting RNG, because some of the RNG functions might get called, and if they are called before RNG2 is set, the game will crash.
    _rng2 = rng + 4;
    _memory->WriteData<int64_t>({_globals + 0x30}, {_rng2});

    // shuffle_integers
    _memory->AddSigScan({0x48, 0x89, 0x5C, 0x24, 0x10, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x63, 0xDA, 0x48, 0x8B, 0xF1, 0x83, 0xFB, 0x01}, [&](int64_t offset, int index, const std::vector<byte>& data) {
        AdjustRng(data, offset, index + 0x23);
    });
    // shuffle<int>
    _memory->AddSigScan({0x33, 0xF6, 0x48, 0x8B, 0xD9, 0x39, 0x31, 0x7E, 0x51}, [&](int64_t offset, int index, const std::vector<byte>& data) {
        AdjustRng(data, offset, index - 0x4);
    });
    // cut_random_edges
    _memory->AddSigScan({0x89, 0x44, 0x24, 0x3C, 0x33, 0xC0, 0x85, 0xC0, 0x75, 0xFA}, [&](int64_t offset, int index, const std::vector<byte>& data) {
        AdjustRng(data, offset, index + 0x3B);
    });
    // get_empty_decoration_slot
    _memory->AddSigScan({0x42, 0x83, 0x3C, 0x80, 0x00, 0x75, 0xDF}, [&](int64_t offset, int index, const std::vector<byte>& data) {
        AdjustRng(data, offset, index - 0x17);
    });
    // get_empty_dot_spot
    _memory->AddSigScan({0xF7, 0xF3, 0x85, 0xD2, 0x74, 0xEC}, [&](int64_t offset, int index, const std::vector<byte>& data) {
        AdjustRng(data, offset, index - 0xB);
    });
    // add_exactly_this_many_bisection_dots
    _memory->AddSigScan({0x48, 0x8B, 0xB4, 0x24, 0xB8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xBC, 0x24, 0xB0, 0x00, 0x00, 0x00}, [&](int64_t offset, int index, const std::vector<byte>& data) {
        AdjustRng(data, offset, index - 0x4);
    });
    // make_a_shaper
    _memory->AddSigScan({0xF7, 0xE3, 0xD1, 0xEA, 0x8D, 0x0C, 0x52}, [&](int64_t offset, int index, const std::vector<byte>& data) {
        AdjustRng(data, offset, index - 0x10);
        AdjustRng(data, offset, index + 0x1C);
        AdjustRng(data, offset, index + 0x49);
    });
    // Entity_Machine_Panel::init_pattern_data_lotus
    _memory->AddSigScan({0x40, 0x55, 0x56, 0x48, 0x8D, 0x6C, 0x24, 0xB1}, [&](int64_t offset, int index, const std::vector<byte>& data) {
        AdjustRng(data, offset, index + 0x433);
        AdjustRng(data, offset, index + 0x45B);
        AdjustRng(data, offset, index + 0x5A7);
        AdjustRng(data, offset, index + 0x5D6);
        AdjustRng(data, offset, index + 0x6F6);
        AdjustRng(data, offset, index + 0xD17);
        AdjustRng(data, offset, index + 0xFDA);
    });
    // Entity_Record_Player::reroll_lotus_eater_stuff
    _memory->AddSigScan({0xB8, 0xAB, 0xAA, 0xAA, 0xAA, 0x41, 0xC1, 0xE8}, [&](int64_t offset, int index, const std::vector<byte>& data) {
        AdjustRng(data, offset, index - 0x13);
        AdjustRng(data, offset, index + 0x34);
    });
    // position_decoy
    _memory->AddSigScan({0x41, 0x0F, 0x29, 0x7B, 0xD8, 0x48, 0x8B, 0xD9}, [&](int64_t offset, int index, const std::vector<byte>& data) {
        AdjustRng(data, offset, index + 0x0B);
    });

    // These disable the random locations on timer panels, which would otherwise increment the RNG.
    // I'm writing 31 C0 (xor eax, eax), then 3 NOPs, which just acts as if the RNG returned 0.
    
    // do_lotus_minutes
    _memory->AddSigScan({0x0F, 0xBE, 0x6C, 0x08, 0xFF, 0x45}, [&](int64_t offset, int index, const std::vector<byte>& data) {
        _memory->WriteData<byte>({offset + index + 0x410}, {0x31, 0xC0, 0x90, 0x90, 0x90});
    });
    // do_lotus_tenths
    _memory->AddSigScan({0x00, 0x04, 0x00, 0x00, 0x41, 0x8D, 0x50, 0x09}, [&](int64_t offset, int index, const std::vector<byte>& data) {
        _memory->WriteData<byte>({offset + index + 0xA2}, {0x31, 0xC0, 0x90, 0x90, 0x90});
    });
    // do_lotus_eighths
    _memory->AddSigScan({0x75, 0xF5, 0x0F, 0xBE, 0x44, 0x08, 0xFF}, [&](int64_t offset, int index, const std::vector<byte>& data) {
        _memory->WriteData<byte>({offset + index + 0x1AE}, {0x31, 0xC0, 0x90, 0x90, 0x90});
    });

    size_t numFailedScans = _memory->ExecuteSigScans();
    if (numFailedScans != 0) return false; // Sigscans failed, we'll try again later.

    // Adjust the code so that we reset RNG to the the current set seed whenever the user starts the challenge.
    int32_t relativeRng2 = static_cast<int32_t>((_globals + 0x30) - (_challengeSeed + 0x6)); // +6 is for the length of the line
    uint32_t seed = static_cast<uint32_t>(time(nullptr)); // Seed from the time in milliseconds

    // Overwrites 20 bytes:
    // movsxd rax, dword ptr ds:[rdi + 0x230] ; Not overwritten
    // test eax, eax                          ; Overwitten
    // jle 2C                                 ; Overwritten
    // imul rcx, rax, 34                      ; Overwitten
    // mov rax, [rdi + offset]                ; Overwritten
    // cmp [rcx + rax - 30], 02               ; Overwritten
    // mov rcx, r8                            ; Not overwritten
    _memory->WriteData<byte>({_challengeSeed}, {
        0x8B, 0x0D, INT_TO_BYTES(relativeRng2),     // mov ecx, [_rng2]
        0x67, 0xC7, 0x01, INT_TO_BYTES(seed),       // mov dword ptr ds:[ecx], seed
        0x48, 0x83, 0xF8, 0x02,                     // cmp rax, 0x2 ; Shortened version of the original code. This checks if the record player was short-solved.
        0x90, 0x90, 0x90                            // nop nop nop
    });
    RandomizeSeed(); // Reroll the seed because time() isn't very random.

    return true;
}

void Trainer::SetPlayerPos(const std::vector<float>& pos) {
    _memory->WriteData<float>({_globals, 0x18, 0x1E465 * 8, 0x24}, pos);
}

void Trainer::SetMainMenuColor(bool enable) {
    if (enable) { // Set the main menu to red by *not* setting the green or blue component.
        _memory->WriteData<byte>({_mainMenuColor}, {0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00}); // 8-byte NOP
    } else { // Restore the original setting by copy/pasting from the block below.
        std::vector<byte> code = _memory->ReadData<byte>({_mainMenuColor + 0x12}, 8);
        _memory->WriteData<byte>({_mainMenuColor}, code);
    }
}

void Trainer::SetMainMenuState(bool open) {
    _memory->WriteData<float>({_menuOpenTarget}, {open ? 1.0f : 0.0f});
}

bool Trainer::GetInfiniteChallenge() {
    return _memory->ReadData<byte>({_infiniteChallenge}, 1)[0] == 0xEB;
}

void Trainer::SetInfiniteChallenge(bool enable) {
    if (enable) {
        _memory->WriteData<byte>({_infiniteChallenge}, {
            0xEB, 0x07, // Jump past abort_speed_run
            0x90, 0x90  // nop nop
        });
    } else {
        // (original code) Load entity_manager into rcx
        _memory->WriteData<byte>({_infiniteChallenge}, {0x48, 0x8B, 0x4B, 0x18});
    }
}

bool Trainer::GetMkChallenge() {
    return _memory->ReadData<byte>({_mkChallenge}, 1)[0] == 0xB8;
}

void Trainer::SetMkChallenge(bool enable) {
    if (enable) {
        _memory->WriteData<byte>({_mkChallenge}, {
            0xB8, INT_TO_BYTES(277), // mov eax, 277        ; New duration in seconds
            0xF3, 0x0F, 0x2A, 0xC0,  // cvtsi2ss xmm0, eax  ; We cannot directly write to float registers, so we convert from eax
            0x90, 0x90, 0x90, 0x90,  // nop nop nop
        });
    } else {
        // Original code
        _memory->WriteData<byte>({_mkChallenge}, {
            0xF3, 0x0F, 0x10, 0x82, INT_TO_BYTES(_durationTotal), // movss xmm0, [rdx + offset]
            0x0F, 0x2E, 0xC6,                                     // ucomiss xmm0, xmm6
            0x74, 0x3B,                                           // je +0x3B
        });
    }
}

uint32_t Trainer::GetSeed() {
    return _memory->ReadData<uint32_t>({_challengeSeed + 9}, 1)[0];
}

void Trainer::SetSeed(uint32_t seed) {
    _memory->WriteData<uint32_t>({_challengeSeed + 9}, {seed});
}

// Generate a new random number using an LCG. Constants from https://arxiv.org/pdf/2001.05304.pdf
void Trainer::RandomizeSeed() {
    uint32_t seed = GetSeed();
    seed = 0x8664f205 * seed + 5;
    SetSeed(seed);
}

double Trainer::GetGameTime() {
    return _memory->ReadData<double>({_gameTime}, 1)[0];
}

ChallengeState Trainer::GetChallengeState() {
    // Inspect one of the challenge timer panels: If it is solved, the challenge was beaten.
    bool isChallengeSolved =  _memory->ReadData<float>({_globals, 0x18, 0x04CB3 * 8, _solvedOffset}, 1)[0] == 1.0f;
    if (isChallengeSolved) return ChallengeState::Solved;

    // Inspect the record player: If it is powered on, the challenge is live.
    bool isChallengeStarted = _memory->ReadData<float>({_globals, 0x18, 0x00BFF * 8, _recordPowerOffset}, 1)[0] == 1.0f;
    if (isChallengeStarted) return ChallengeState::Running;

    // Inspect the speed clock (internal timer): If the time is set to -1, the challenge has been stopped.
    bool isChallengeStopped = _memory->ReadData<float>({_globals, 0x18, 0x03B33 * 8, _elapsedTimeOffset}, 1)[0] == -1.0f;
    if (isChallengeStopped) return ChallengeState::Stopped;

    assert(false, "Unknown challenge state");
    return ChallengeState::Stopped;
}
