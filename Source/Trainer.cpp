#include "pch.h"
#include "Trainer.h"
#include "Panels.h"

std::shared_ptr<Trainer> Trainer::Create(const std::shared_ptr<Memory>& memory) {
    auto trainer = std::shared_ptr<Trainer>(new Trainer());

    memory->AddSigScan({0x74, 0x41, 0x48, 0x85, 0xC0, 0x74, 0x04, 0x48, 0x8B, 0x48, 0x10}, [trainer](__int64 offset, int index, const std::vector<byte>& data) {
        trainer->_globals = Memory::ReadStaticInt(offset, index + 0x14, data);
    });

    memory->AddSigScan({0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0xE9, 0xB3}, [trainer](__int64 offset, int index, const std::vector<byte>& data) {
        trainer->_recordPlayerUpdate = offset + index - 0x0C;
    });
    
    // We need to save _memory before we exit, otherwise we can't destroy properly.
    trainer->_memory = memory;

    size_t numFailedScans = memory->ExecuteSigScans();
    if (numFailedScans != 0) return nullptr; // Sigscans failed, we'll try again later.

    return trainer;
}

// Restore default game settings when shutting down the trainer.
Trainer::~Trainer() {
    SetInfiniteChallenge(false);
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

void Trainer::UnRandomize() {

}

void Trainer::ReRandomize() {

}


// mess below














std::vector<int> challengePanels = {
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
//    0x04CB3, // Challenge Left Timer
//    0x04CB5, // Challenge Middle Timer
//    0x04CB6, // Challenge Right Timer
    0x034EC, // Challenge Triangle
    0x034F4, // Challenge Triangle
    0x1C31A, // Challenge Left Pillar
    0x1C319, // Challenge Right Pillar
//    0x0356B, // Challenge Vault Box
};

void Trainer::SetSeed(int seed) {
    s_seed = seed;
}

// Returns a random integer in [min, max]
int Trainer::RandInt(int min, int max) {
    s_seed = (214013 * s_seed + 2531011); // Implicit unsigned integer overflow
    int32_t maskedSeed = ((s_seed >> 16) & 0x7fff); // Only use bits 16-30
    return (maskedSeed % (max - min + 1)) + min;
}

// Modify an instruction to use RNG2 instead of main RNG
void Trainer::AdjustRng(const std::vector<byte>& data, int offset, int index) {
    int64_t currentRngPtr = Memory::ReadStaticInt(offset, index, data);
    _memory->WriteData<int64_t>({offset}, {currentRngPtr + 0x20});
}

void Trainer::Randomize(int32_t seed) {
    seed = Trainer::RandInt(1, 0x7FFFFFFF); // TODO -- seeds? ????
    for (int panel : challengePanels) {
        _memory->WriteData<int>({_globals, 0x18, panel * 8, 0x2C0}, {0});
    }

    int64_t RNG_ADDR = _memory->ReadData<int64_t>({_globals + 0x10}, 1)[0];
    int64_t RNG2_ADDR = _memory->ReadData<int64_t>({_globals + 0x30}, 1)[0];
    bool alreadyInjected = (RNG2_ADDR == RNG_ADDR + 4);

    if (!alreadyInjected) _memory->WriteData<int64_t>({_globals + 0x30}, {RNG_ADDR + 4});
    _memory->WriteData<int>({_globals + 0x30, 0}, {seed});

    // do_success_side_effects
    _memory->AddSigScan({0xFF, 0xC8, 0x99, 0x2B, 0xC2, 0xD1, 0xF8, 0x8B, 0xD0}, [&](int64_t offset, int index, const std::vector<byte>& data) {
        if (_globals == 0x5B28C0) { // Version differences.
            index += 0x3E;
        } else if (_globals == 0x62D0A0) {
            index += 0x42;
        }
        // Overwritten bytes start just after the movsxd rax, dword ptr ds:[rdi + 0x230]
        // aka test eax, eax; jle 2C; imul rcx, rax, 34
        _memory->WriteData<byte>({offset + index}, {
            0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,         // mov ecx, [0x00000000]                ;This is going to be the address of the custom RNG
            0x67, 0xC7, 0x01, 0x00, 0x00, 0x00, 0x00,   // mov dword ptr ds:[ecx], 0x00000000   ;This is going to be the seed value
            0x48, 0x83, 0xF8, 0x02,                     // cmp rax, 0x2                         ;This is the short solve on the record player (which turns it off)
            0x90, 0x90, 0x90                            // nop nop nop
        });
        // ReadStaticInt?
        int target = (_globals + 0x30) - (offset + index + 0x6); // +6 is for the length of the line
        _memory->WriteData<int>({offset + index + 0x2}, {target});
        _memory->WriteData<int>({offset + index + 0x9}, {seed}); // Because we're resetting seed every challenge, we need to run this injection every time.
    });

    if (!alreadyInjected) {
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
        // do_lotus_minutes
        _memory->AddSigScan({0x0F, 0xBE, 0x6C, 0x08, 0xFF, 0x45}, [&](int64_t offset, int index, const std::vector<byte>& data) {
            _memory->WriteData<byte>({index + 0x410}, {0x31, 0xC0, 0x90, 0x90, 0x90});
        });
        // do_lotus_tenths
        _memory->AddSigScan({0x00, 0x04, 0x00, 0x00, 0x41, 0x8D, 0x50, 0x09}, [&](int64_t offset, int index, const std::vector<byte>& data) {
            _memory->WriteData<byte>({index + 0xA2}, {0x31, 0xC0, 0x90, 0x90, 0x90});
        });
        // do_lotus_eighths
        _memory->AddSigScan({0x75, 0xF5, 0x0F, 0xBE, 0x44, 0x08, 0xFF}, [&](int64_t offset, int index, const std::vector<byte>& data) {
            _memory->WriteData<byte>({index + 0x1AE}, {0x31, 0xC0, 0x90, 0x90, 0x90});
        });
    }

    int failed = _memory->ExecuteSigScans();
    if (failed != 0) {
        std::cout << "Failed " << failed << " sigscans";
    }
}
