#include "pch.h"
#include "Trainer.h"
#include "Panels.h"

std::shared_ptr<Trainer> Trainer::Create(const std::shared_ptr<Memory>& memory) {
    auto trainer = std::shared_ptr<Trainer>(new Trainer());

    memory->AddSigScan({0x74, 0x41, 0x48, 0x85, 0xC0, 0x74, 0x04, 0x48, 0x8B, 0x48, 0x10}, [trainer](int64_t offset, int index, const std::vector<byte>& data) {
        int64_t globals = Memory::ReadStaticInt(offset, index + 0x14, data);
        assert(globals < std::numeric_limits<int32_t>::max());
        trainer->_globals = static_cast<int32_t>(globals);
    });

    memory->AddSigScan({0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0xE9, 0xB3}, [trainer](int64_t offset, int index, const std::vector<byte>& data) {
        trainer->_recordPlayerUpdate = offset + index - 0x0C;
    });

    // We need to save _memory before we exit, otherwise we can't destroy properly.
    trainer->_memory = memory;

    size_t numFailedScans = memory->ExecuteSigScans();
    if (numFailedScans != 0) return nullptr; // Sigscans failed, we'll try again later.

    // do_success_side_effects
    memory->AddSigScan({0xFF, 0xC8, 0x99, 0x2B, 0xC2, 0xD1, 0xF8, 0x8B, 0xD0}, [trainer](int64_t offset, int index, const std::vector<byte>& data) {
        int64_t doSuccessSideEffects = 0;
        if (trainer->_globals == 0x5B28C0) { // Version differences.
            doSuccessSideEffects = offset + index + 0x3E;
        } else if (trainer->_globals == 0x62D0A0) {
            doSuccessSideEffects = offset + index + 0x42;
        } else {
            assert(false);
        }
        assert(doSuccessSideEffects < std::numeric_limits<int32_t>::max());
        trainer->_doSuccessSideEffects = static_cast<int32_t>(doSuccessSideEffects);
    });

    numFailedScans = memory->ExecuteSigScans();
    if (numFailedScans != 0) return nullptr; // Sigscans failed, we'll try again later.

    if (!trainer->Init()) return nullptr; // Initialization failed


    // TODO: Also change the main menu color to blue!

    return trainer;
}

// Modify an instruction to use RNG2 instead of main RNG
void Trainer::AdjustRng(const std::vector<byte>& data, int64_t offset, int index) {
    int32_t currentRngPtr = *(int32_t*)&data[index]; // Not a ReadStaticInt because it's a relative ptr.
    _memory->WriteData<int32_t>({offset + index}, {currentRngPtr + 0x20});
}

bool Trainer::Init() {
    _rng = _memory->ReadData<int64_t>({_globals + 0x10}, 1)[0];
    _rng2 = _memory->ReadData<int64_t>({_globals + 0x30}, 1)[0];
    // Already injected
    if (_rng2 == _rng + 4) return true;

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

    // These disable the random locations on timer panels, which would otherwise increment the RNG.
    // I'm writing 31 C0 (xor eax, eax), then 3 NOPs, which just acts as if the RNG returned 0.
    // TODO: I could be more clever here, and show a checksum? Not sure...

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

    {
        int32_t relativeRng2 = (_globals + 0x30) - (_doSuccessSideEffects + 0x6); // +6 is for the length of the line
        int32_t seed = static_cast<int32_t>(time(nullptr)); // Seed from the time in milliseconds

        // Note: Little endian
        #define INT_TO_BYTES(val) \
            static_cast<byte>((val & 0x000000FF) >> 0x00), \
            static_cast<byte>((val & 0x0000FF00) >> 0x08), \
            static_cast<byte>((val & 0x00FF0000) >> 0x10), \
            static_cast<byte>((val & 0xFF000000) >> 0x18)

        // Overwritten bytes start just after the movsxd rax, dword ptr ds:[rdi + 0x230]
        // aka test eax, eax; jle 2C; imul rcx, rax, 34
        _memory->WriteData<byte>({_doSuccessSideEffects}, {
            0x8B, 0x0D, INT_TO_BYTES(relativeRng2),     // mov ecx, [_rng2]
            0x67, 0xC7, 0x01, INT_TO_BYTES(seed),       // mov dword ptr ds:[ecx], seed
            0x48, 0x83, 0xF8, 0x02,                     // cmp rax, 0x2         ; Shortened version of replaced code. This checks if the record player was short-solved.
            0x90, 0x90, 0x90                            // nop nop nop
        });
        RandomizeSeed(); // Reroll the seed because time() isn't very random.

        // Prevent challenge panels from turning off on failure. Otherwise, rerolling a panel could cause an RNG change.
        for (int32_t panel : _challengePanels) {
            _memory->WriteData<int32_t>({_globals, 0x18, panel * 8, 0x2C0}, {0});
        }
    }

    // Succeeded, set RNG2 to prevent unnecessary future injections.
    _rng2 = _rng + 4;
    _memory->WriteData<int64_t>({_globals + 0x30}, {_rng2});

    return true;
}

std::vector<float> Trainer::GetPlayerPos() {
    return _memory->ReadData<float>({_globals, 0x18, 0x1E465 * 8, 0x24}, 3);
}

void Trainer::SetPlayerPos(const std::vector<float>& pos) {
    _memory->WriteData<float>({_globals, 0x18, 0x1E465 * 8, 0x24}, pos);
}

bool Trainer::GetInfiniteChallenge() {
    return _memory->ReadData<byte>({_recordPlayerUpdate}, 1)[0] == 0x0F;
}

void Trainer::SetInfiniteChallenge(bool enable) {
    if (enable) {
        // Jump over abort_speed_run, with NOP padding
        _memory->WriteData<byte>({_recordPlayerUpdate}, {0xEB, 0x07, 0x66, 0x90});
    } else {
        // (original code) Load entity_manager into rcx
        _memory->WriteData<byte>({_recordPlayerUpdate}, {0x48, 0x8B, 0x4B, 0x18});
    }
}

bool Trainer::GetMkChallenge() {
    return false;
}

void Trainer::SetMkChallenge(bool enable) {
    return;
}

bool Trainer::GetChallengeReroll() {
    return false;
}

void Trainer::SetChallengeReroll(bool enable) {
    return;
}

void Trainer::SetSeed(uint32_t seed) {
    _memory->WriteData<uint32_t>({_doSuccessSideEffects + 9}, {seed});
}

uint32_t Trainer::GetSeed() {
    return _memory->ReadData<uint32_t>({_doSuccessSideEffects + 9}, 1)[0];
}

// Generate a new random number using an LCG. Constants from https://arxiv.org/pdf/2001.05304.pdf
void Trainer::RandomizeSeed() {
    uint32_t seed = GetSeed();
    seed = 0x8664f205 * seed + 5;
    SetSeed(seed);
}
