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
    if (!ParseHotkeyFile()) {
        // Default hotkeys (duplicate of what's defined in DEFAULT_HOTKEYS in the header), just in case the parse fails.
        _hotkeyNames["noclip_enabled"] = MASK_CONTROL | 'N';
        _hotkeyNames["can_save_game"] = MASK_SHIFT | MASK_CONTROL | 'S';
        _hotkeyNames["open_console"] = MASK_SHIFT | VK_OEM_3;
        _hotkeyNames["ep_overlay"] = MASK_ALT | '2';
        _hotkeyNames["save_position"] = MASK_CONTROL | 'P';
        _hotkeyNames["load_position"] = MASK_SHIFT | MASK_CONTROL | 'P';
        _hotkeyNames["snap_to_panel"] = MASK_CONTROL | 'L';
        _hotkeyNames["open_doors"] = MASK_CONTROL | 'O';
    }

    // Can't be changed, this is for internal debugging only.
    _hotkeyNames["dump_callstack"] = MASK_CONTROL | MASK_SHIFT | MASK_ALT | VK_OEM_PLUS;
}

bool Hotkeys::CompareNoCase(const std::string_view& a, const char* b) {
    for (int i = 0; i < a.size(); i++) {
        if (b[i] == '\0') return false;
        if ((a[i] & 0xDF) != (b[i] & 0xDF)) return false;
    }

    return true;
}

#define ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            std::wstring __fullMessage = std::wstring(L"Error while parsing hotkey file (") + path + std::wstring(L") on line ") + std::to_wstring(lineNo) + std::wstring(L":\n") + std::wstring(message); \
            ShowAssertDialogue(__fullMessage.c_str()); \
        } \
    } while(0)

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

    std::string line;
    int32_t lineNo = 0;
    while (std::getline(file, line)) {
        ++lineNo;
        std::string_view lineView(line);

        size_t colonIndex = lineView.find_first_of(':');
        if (colonIndex == std::string::npos) continue;
        std::string_view key = lineView.substr(0, colonIndex); // TODO: Lowercase me!
        // TODO: Run some post-parse sanity check to make sure there's no extra keys.

        keycode keyCode = 0;
        size_t valueIndex = lineView.find_first_not_of(' ', colonIndex + 1);
        while (valueIndex != std::string::npos) {
            size_t partIndex = lineView.find_first_of('-', valueIndex);
            std::string_view segment(lineView.substr(valueIndex, partIndex - valueIndex));
            if (segment.size() == 1) {
                char ch = segment[0];
                if (ch >= 'a' && ch <= 'z') ch += 'A' - 'a';
                keyCode |= ch;
                ASSERT(partIndex == std::string::npos, L"A one-character segment should always be at the end of the keycode.");
            }
            else if (CompareNoCase(segment, "control")) keyCode |= MASK_CONTROL;
            else if (CompareNoCase(segment, "shift"))   keyCode |= MASK_SHIFT;
            else if (CompareNoCase(segment, "alt"))     keyCode |= MASK_ALT;
            else if (CompareNoCase(segment, "win"))     keyCode |= MASK_WIN;
            else if (CompareNoCase(segment, "tilde"))   keyCode |= MASK_SHIFT | VK_OEM_3;
            else if (CompareNoCase(segment, "plus"))    keyCode |= VK_OEM_PLUS;
            else {
                ASSERT(false, L"Unable to parse segment: " + std::wstring(segment.begin(), segment.end()));
            }

            if (partIndex == std::string::npos) break;
            valueIndex = partIndex + 1;
        }

        if (keyCode != 0) _hotkeyNames[std::string(key)] = keyCode;
    }

    return true;
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
