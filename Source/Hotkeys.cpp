#include "pch.h"
#include "shellapi.h"
#include "Shlobj.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

#include "Hotkeys.h"

std::shared_ptr<Hotkeys> Hotkeys::_instance = nullptr;

std::shared_ptr<Hotkeys> Hotkeys::Get() {
    if (_instance == nullptr) _instance = std::make_shared<Hotkeys>();
    return _instance;
}

Hotkeys::Hotkeys() {
    _hotkeyNames = {
        { "god_mode",           ParseKeycode("Control-G", -2) },
        { "save_position",      ParseKeycode("Control-P", -2) },
        { "load_position",      ParseKeycode("Control-Shift-P", -2) },
        { "infinite_health",    ParseKeycode("Control-Shift-H", -2) },
        { "infinite_charge",    ParseKeycode("Control-Shift-C", -2) },
        { "respawn",            ParseKeycode("Control-Shift-R", -2) },
        { "select_pos_1",       ParseKeycode("Control-Shift-1", -2) },
        { "select_pos_2",       ParseKeycode("Control-Shift-2", -2) },
        { "select_pos_3",       ParseKeycode("Control-Shift-3", -2) },
        { "select_pos_4",       ParseKeycode("Control-Shift-4", -2) },
        { "select_pos_5",       ParseKeycode("Control-Shift-5", -2) },
        { "select_pos_6",       ParseKeycode("Control-Shift-6", -2) },
        { "select_pos_7",       ParseKeycode("Control-Shift-7", -2) },
        { "select_pos_8",       ParseKeycode("Control-Shift-8", -2) },
    };
    std::wstring path = GetHotkeyFilePath();

    if (!path.empty() && ParseHotkeyFile(path)) {
        // If the file is readable, rewrite with our fully parsed config.
        // If it's not readable, we'll just fall back to an in-memory config.
        std::string text;
        for (const auto& [key, value] : _hotkeyNames) {
            text += key + ": ";
            for (wchar_t wch : GetHoverText(value)) text += (char)wch;
            text += "\n";
        }

        HANDLE file = CreateFile(path.c_str(), FILE_GENERIC_WRITE, NULL, nullptr, CREATE_ALWAYS, NULL, nullptr);
        WriteFile(file, text.data(), (DWORD)text.size(), nullptr, nullptr);
        CloseHandle(file);
    }

    // Not written to disk, can't be changed, used to signal 'end of held key'
    _hotkeyNames["key_released"] = KEYCODE_RELEASE;
}

std::wstring Hotkeys::GetHotkeyFilePath() {
    // Try to open the file, and regenerate it if it doesn't exist.
    std::wstring path;
    {
        PWSTR outPath;
        SHGetKnownFolderPath(FOLDERID_LocalAppData, SHGFP_TYPE_CURRENT, NULL, &outPath);
        path = outPath;
        CoTaskMemFree(outPath);
    }

    if (GetFileAttributes(path.c_str()) == INVALID_FILE_ATTRIBUTES) return L""; // Do not try to create LocalAppData
    path += L"\\HobTrainer";
    if (GetFileAttributes(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (_wmkdir(path.c_str()) != 0) return L"";
    }
    path += L"\\keybinds.txt";

    return path;
}

bool Hotkeys::CompareNoCase(const std::string_view& a, const char* b) {
    for (int i = 0; i < a.size(); i++) {
        if (b[i] == '\0') return false;
        if ((a[i] & 0xDF) != (b[i] & 0xDF)) return false;
    }

    return true;
}

#define SHOW_HOTKEY_FAILURE(message) \
    do { \
        std::wstring __fullMessage = std::wstring(L"Error while parsing hotkey file (") + Hotkeys::GetHotkeyFilePath() + std::wstring(L") on line ") + std::to_wstring(lineNo) + std::wstring(L":\n") + std::wstring(message); \
        ShowAssertDialogue(__fullMessage.c_str()); \
    } while(0)

bool Hotkeys::ParseHotkeyFile(const std::wstring& path) {
    std::ifstream file(path);
    if (file.fail()) return false;

    std::string line;
    int32_t lineNo = 0;
    while (std::getline(file, line)) {
        ++lineNo;

        size_t colonIndex = line.find_first_of(':');
        if (colonIndex == std::string::npos) continue;
        keycode keyCode = ParseKeycode(line.substr(colonIndex + 1), lineNo);

        if (keyCode != 0) {
            std::string key = line.substr(0, colonIndex);
            for (int i = 0; i < key.size(); i++) {
                char ch = key[i];
                if (ch < 0x30 || ch > 0x7F) {
                    SHOW_HOTKEY_FAILURE(L"Unable to parse key: " + std::wstring(key.begin(), key.end()));
                }
                if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A'; // ASCII lowercase
                key[i] = ch;
            }
            _hotkeyNames[key] = keyCode;
        }
    }

    return true;
}

Hotkeys::keycode Hotkeys::ParseKeycode(std::string_view text, int lineNo) {
    keycode keyCode = 0;

    size_t valueIndex = text.find_first_not_of(' ');
    while (valueIndex != std::string::npos) {
        size_t partIndex = text.find_first_of('-', valueIndex);
        std::string_view segment(text.substr(valueIndex, partIndex - valueIndex));
        if (segment.size() == 1) {
            char ch = segment[0];
            if (ch < 0x20 || ch > 0x7F) {
                SHOW_HOTKEY_FAILURE(L"Unable to parse letter: " + std::wstring(1, ch));
                return 0;
            }
            if (ch >= 'a' && ch <= 'z') ch += 'A' - 'a'; // ASCII uppercase, to match the virtual keycodes
            keyCode |= ch;
            if (partIndex != std::string::npos) {
                SHOW_HOTKEY_FAILURE(L"The letter key in a hotkey must go at the end of the line.");
            }
        }
        else if (CompareNoCase(segment, "control"))  keyCode |= MASK_CONTROL;
        else if (CompareNoCase(segment, "shift"))    keyCode |= MASK_SHIFT;
        else if (CompareNoCase(segment, "alt"))      keyCode |= MASK_ALT;
        else if (CompareNoCase(segment, "win"))      keyCode |= MASK_WIN;
        else if (CompareNoCase(segment, "tilde"))    keyCode |= MASK_SHIFT | VK_OEM_3;
        else if (CompareNoCase(segment, "plus"))     keyCode |= VK_OEM_PLUS;
        else if (CompareNoCase(segment, "pageup"))   keyCode |= VK_PRIOR;
        else if (CompareNoCase(segment, "pagedown")) keyCode |= VK_NEXT;
        else if (CompareNoCase(segment, "home"))     keyCode |= VK_HOME;
        else if (CompareNoCase(segment, "end"))      keyCode |= VK_END;
        else if (CompareNoCase(segment, "space"))    keyCode |= VK_SPACE;
        else if (CompareNoCase(segment, "up"))       keyCode |= VK_UP;
        else if (CompareNoCase(segment, "down"))     keyCode |= VK_DOWN;
        else if (CompareNoCase(segment, "left"))     keyCode |= VK_LEFT;
        else if (CompareNoCase(segment, "right"))    keyCode |= VK_RIGHT;
        else if (CompareNoCase(segment, "mouse1"))   keyCode |= VK_LBUTTON;
        else if (CompareNoCase(segment, "mouse2"))   keyCode |= VK_RBUTTON;
        else if (CompareNoCase(segment, "mouse3"))   keyCode |= VK_MBUTTON;
        else if (CompareNoCase(segment, "mouse4"))   keyCode |= VK_XBUTTON1;
        else if (CompareNoCase(segment, "mouse5"))   keyCode |= VK_XBUTTON2;
        else {
            SHOW_HOTKEY_FAILURE(L"Unable to parse segment: '" + std::wstring(segment.begin(), segment.end()) + L"'");
            return 0;
        }

        if (partIndex == std::string::npos) break;
        valueIndex = partIndex + 1;
    }

    return keyCode;
}

int64_t Hotkeys::CheckMatchingHotkey(WPARAM wParam, LPARAM lParam) {
    if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
        if (_lastCode != 0) {
            _lastCode = 0; // Cancel key repeat, and return a signal that a key was let up

            auto search = _hotkeyCodes.find(KEYCODE_RELEASE);
            if (search != std::end(_hotkeyCodes)) return search->second;
        }
        return 0;
    }
    
    int32_t fullCode = 0;
    if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) fullCode = ((PKBDLLHOOKSTRUCT)lParam)->vkCode;
    else if (wParam == WM_XBUTTONDOWN) fullCode = HIWORD(((PMSLLHOOKSTRUCT)lParam)->mouseData) + (VK_XBUTTON1 - XBUTTON1);

    int64_t found = 0;
    // For perf, we look at just the keyboard key first (before consulting GetKeyState).
    if (_hotkeys.find(fullCode) != _hotkeys.end()) {
        if (GetKeyState(VK_SHIFT) & 0x8000)     fullCode |= MASK_SHIFT;
        if (GetKeyState(VK_CONTROL) & 0x8000)   fullCode |= MASK_CONTROL;
        if (GetKeyState(VK_MENU) & 0x8000)      fullCode |= MASK_ALT;
        if (GetKeyState(VK_LWIN) & 0x8000)      fullCode |= MASK_WIN;
        if (GetKeyState(VK_RWIN) & 0x8000)      fullCode |= MASK_WIN;

        auto search = _hotkeyCodes.find(fullCode);
        if (search != std::end(_hotkeyCodes)) found = search->second;
    }
    _lastCode = fullCode;
    return found;
}

void Hotkeys::RegisterHotkey(LPCSTR hotkeyName, int64_t message) {
    _registeredHotkeys.insert(hotkeyName);
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

    keyCode &= 0xFF; // Remove masks for comparison to ascii codes

    if      (keyCode >= 'a' && keyCode <= 'z')  ss << (char)(keyCode - 'a' + 'A');
    else if (keyCode >= '0' && keyCode <= ']')  ss << (char)keyCode; // Includes A-Z and 0-9
    else if (keyCode == VK_OEM_PLUS)            ss << '+';
    else if (keyCode == VK_PRIOR)               ss << "PageUp";
    else if (keyCode == VK_NEXT)                ss << "PageDown";
    else if (keyCode == VK_HOME)                ss << "Home";
    else if (keyCode == VK_END)                 ss << "End";
    else if (keyCode == VK_SPACE)               ss << "Space";
    else if (keyCode == VK_UP)                  ss << "UpArrow";
    else if (keyCode == VK_DOWN)                ss << "DownArrow";
    else if (keyCode == VK_LEFT)                ss << "LeftArrow";
    else if (keyCode == VK_RIGHT)               ss << "RightArrow";
    else if (keyCode == VK_LBUTTON)             ss << "Mouse1";
    else if (keyCode == VK_RBUTTON)             ss << "Mouse2";
    else if (keyCode == VK_MBUTTON)             ss << "Mouse3";
    else if (keyCode == VK_XBUTTON1)            ss << "Mouse4";
    else if (keyCode == VK_XBUTTON2)            ss << "Mouse5";

    return ss.str();
}

void Hotkeys::SanityCheckHotkeys() {
    for (const auto& it : _hotkeyNames) {
        auto search = _registeredHotkeys.find(it.first);
        if (search == std::end(_registeredHotkeys)) {
            int lineNo = -1;
            SHOW_HOTKEY_FAILURE(L"Found entry for unknown hotkey: " + std::wstring(it.first.begin(), it.first.end()));
        }
    }
}
