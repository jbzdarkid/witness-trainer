#include "pch.h"
#include "Hotkeys.h"

std::shared_ptr<Hotkeys> Hotkeys::_instance = nullptr;

std::shared_ptr<Hotkeys> Hotkeys::Get() {
    if (_instance == nullptr) _instance = std::make_shared<Hotkeys>();
    return _instance;
}

int64_t Hotkeys::Foo(WPARAM wParam, LPARAM lParam) {
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

void Hotkeys::SetHotkey(int32_t keyCode, int64_t message) {
    assert(((int32_t)message) == message, "Attempted to set a hotkey with message > int32");
    SetHotkey(keyCode, (int32_t)message);
}

void Hotkeys::SetHotkey(int32_t keyCode, int32_t message) {
    _hotkeyCodes[keyCode] = message;
    _hotkeys.insert(keyCode & 0xFF);
}
