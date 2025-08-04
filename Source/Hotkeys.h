#pragma once
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>

constexpr uint32_t MASK_SHIFT   = 0x0100;
constexpr uint32_t MASK_CONTROL = 0x0200;
constexpr uint32_t MASK_ALT     = 0x0400;
constexpr uint32_t MASK_WIN     = 0x0800;

class Hotkeys {
    typedef int32_t keycode;
    const keycode KEYCODE_RELEASE = (keycode)-1;

public:
    static std::shared_ptr<Hotkeys> Get();
    Hotkeys();
    std::wstring GetHotkeyFilePath() const { return _hotkeyFilePath; }

    int64_t CheckMatchingHotkey(WPARAM wParam, LPARAM lParam);

    void RegisterHotkey(LPCSTR hotkeyName, int64_t message);
    std::wstring GetHoverText(LPCSTR hotkeyName);
    void SanityCheckHotkeys();

private:
    static std::shared_ptr<Hotkeys> _instance;

    bool ParseHotkeyFile();
    bool CompareNoCase(const std::string_view& a, const char* b);
    std::wstring GetHoverText(keycode keyCode);

    std::wstring _hotkeyFilePath;
    keycode _lastCode = 0;
    std::map<keycode, int64_t> _hotkeyCodes;
    std::unordered_set<keycode> _hotkeys;
    std::map<std::string, keycode> _hotkeyNames;
    std::unordered_set<std::string> _registeredHotkeys;

    const LPCSTR DEFAULT_KEYBINDS =
        "noclip_enabled: Control-N\n"
        "fly_up: PageUp\n"
        "fly_down: PageDown\n"
        "can_save_game: Shift-Control-S\n"
        "open_console: Tilde\n"
        "ep_overlay: Alt-2\n"
        "save_position: Control-P\n"
        "load_position: Control-Shift-P\n"
        "snap_to_panel: Control-L\n"
        "open_doors: Control-O\n"
        "doors_practice:\n"
        "challenge_timer:\n"
        "clamp_aim:\n"
        "launch_game:\n"
        "open_save_folder:\n"
        "show_unsolved:\n"
        "distance_gating:\n"
        "open_keybinds:\n";
};