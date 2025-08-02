#include "pch.h"
#include "Hotkeys.h"

#include <sstream>

std::shared_ptr<Hotkeys> Hotkeys::_instance = nullptr;

std::shared_ptr<Hotkeys> Hotkeys::Get() {
    if (_instance == nullptr) _instance = std::make_shared<Hotkeys>();
    return _instance;
}

Hotkeys::Hotkeys() {
    // In theory, this comes from some file. I haven't written the parser yet, though.
    _hotkeyNames["noclip_enabled"] = MASK_CONTROL | 'N';
    _hotkeyNames["can_save_game"] = MASK_SHIFT | MASK_CONTROL | 'S';
    _hotkeyNames["open_console"] = MASK_SHIFT | VK_OEM_3;
    _hotkeyNames["ep_overlay"] = MASK_ALT | '2';
    _hotkeyNames["save_position"] = MASK_CONTROL | 'P';
    _hotkeyNames["load_position"] = MASK_SHIFT | MASK_CONTROL | 'P';
    _hotkeyNames["snap_to_panel"] = MASK_CONTROL | 'L';
    _hotkeyNames["open_doors"] = MASK_CONTROL | 'O';
    _hotkeyNames["dump_callstack"] = MASK_CONTROL | MASK_SHIFT | MASK_ALT | VK_OEM_PLUS;
}

int64_t Hotkeys::CheckMatchingHotkey(WPARAM wParam, LPARAM lParam) {
    if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
        _lastCode = 0; // Cancel key repeat
    } else if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
        auto p = (PKBDLLHOOKSTRUCT)lParam;
        int32_t fullCode = p->vkCode;

        int64_t found = 0;
        // For perf, we look at just the keyboard key first (before consulting GetKeyState).
        if (_hotkeys.find(fullCode) != _hotkeys.end()) {
            if (GetKeyState(VK_SHIFT) & 0x8000)     fullCode |= MASK_SHIFT;
            if (GetKeyState(VK_CONTROL) & 0x8000)   fullCode |= MASK_CONTROL;
            if (GetKeyState(VK_MENU) & 0x8000)      fullCode |= MASK_ALT;
            if (GetKeyState(VK_LWIN) & 0x8000)      fullCode |= MASK_WIN;
            if (GetKeyState(VK_RWIN) & 0x8000)      fullCode |= MASK_WIN;
            if (_lastCode == fullCode)              fullCode |= MASK_REPEAT;

            auto search = _hotkeyCodes.find(fullCode);
            if (search != std::end(_hotkeyCodes)) found = search->second;
        }
        _lastCode = fullCode & ~MASK_REPEAT;
        return found;
    }

    return 0;
}

void Hotkeys::RegisterHotkey(LPCSTR hotkeyName, int64_t message) {
    auto search = _hotkeyNames.find(hotkeyName);
    if (search == std::end(_hotkeyNames)) return; // No keybind for this hotkey, no need to register a callback

    keycode keyCode = search->second;
    _hotkeyCodes[keyCode] = message;
    _hotkeys.insert(keyCode & 0xFF);
}

std::wstring Hotkeys::GetHoverText(LPCSTR hotkeyName) {
    auto search = _hotkeyNames.find(hotkeyName);
    if (search == std::end(_hotkeyNames)) return {}; // No keybind for this hotkey

    Hotkeys::keycode keyCode = search->second;
    return GetHoverText(keyCode);
}

std::wstring Hotkeys::GetHoverText(keycode keyCode) {
    // Special handling because this is a weird key
    if (keyCode == (MASK_SHIFT | VK_OEM_3)) return std::wstring(L"Tilde (~)");

    std::wstringstream ss;
    if (keyCode & MASK_CONTROL) ss << "Control-";
    if (keyCode & MASK_SHIFT)   ss << "Shift-";
    if (keyCode & MASK_ALT)     ss << "Alt-";
    if (keyCode & MASK_WIN)     ss << "Win-";

    bool repeat = keyCode & MASK_REPEAT;
    keyCode &= 0xFF; // Remove masks for comparison to ascii codes

    if      (keyCode >= 'a' && keyCode <= 'z') ss << (char)(keyCode - 'a' + 'A');
    else if (keyCode >= '0' && keyCode <= ']') ss << (char)keyCode; // Includes A-Z and 0-9
    else if (keyCode == VK_OEM_PLUS) ss << '+';


    if (repeat) ss << " (held)";
    return ss.str();
}
