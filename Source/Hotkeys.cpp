#include "pch.h"
#include "shellapi.h"
#include "Shlobj.h"

#include <fstream>
#include <sstream>

#include "Hotkeys.h"

std::shared_ptr<Hotkeys> Hotkeys::_instance = nullptr;

std::shared_ptr<Hotkeys> Hotkeys::Get() {
    if (_instance == nullptr) _instance = std::make_shared<Hotkeys>();
    return _instance;
}

Hotkeys::Hotkeys() {
    if (!ParseHotkeyFile()) {
        // Default hotkeys (duplicate of what's defined in DEFAULT_HOTKEYS in the header, just in case the parse fails.
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
}

bool Hotkeys::ParseHotkeyFile() {
    _hotkeyNames.clear();

    // Try to open the file, and regenerate it if it doesn't exist.
    std::wstring path;
    {
        PWSTR outPath;
        SHGetKnownFolderPath(FOLDERID_LocalAppData, SHGFP_TYPE_CURRENT, NULL, &outPath);
        path = outPath;
        CoTaskMemFree(outPath);
    }

    if (GetFileAttributes(path.c_str()) == INVALID_FILE_ATTRIBUTES) return false; // Do not try to create LocalAppData
    path += L"\\WitnessTrainer";
    if (GetFileAttributes(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (_wmkdir(path.c_str()) != 0) return false;
    }
    path += L"\\keybinds.txt";
    if (GetFileAttributes(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        HANDLE file = CreateFile(path.c_str(), FILE_GENERIC_WRITE, NULL, nullptr, CREATE_ALWAYS, NULL, nullptr);
        WriteFile(file, &DEFAULT_KEYBINDS[0], (DWORD)strnlen(DEFAULT_KEYBINDS, 0xFFFF), nullptr, nullptr);
        CloseHandle(file);
    }

    std::ifstream file(path);
    if (file.fail()) return false;

    // TODO: The rest of the parser
    return true;

    /*
    std::ifstream file(filename);
    std::string line;

    bool skip = false;
    while (std::getline(file, line)) {
        if (skip) {
            skip = false;
            buffer.pop_back();
        }
        if (line == "North") buffer.push_back(North);
        if (line == "South") buffer.push_back(South);
        if (line == "East")  buffer.push_back(East);
        if (line == "West")  buffer.push_back(West);
#ifdef _DEBUG // Only skip Undos in Debug mode -- in release mode, we need them for the TAS.
        if (line == "Undo") {
            skip = true;
            continue;
        }
#else
        if (line == "Undo")  buffer.push_back(Undo);
#endif
        if (line == "Reset") buffer.push_back(Reset);
        if (line == "None")  buffer.push_back(None); // Allow "None" in demo files, in case we need to buffer something, at some point.
    }
    buffer.push_back(Stop);
    Wipe();
    WriteData(buffer);
    */
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
