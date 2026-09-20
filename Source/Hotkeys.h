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
    static std::wstring GetHotkeyFilePath();

    int64_t CheckMatchingHotkey(WPARAM wParam, LPARAM lParam);

    void RegisterHotkey(LPCSTR hotkeyName, int64_t message);
    std::wstring GetHoverText(LPCSTR hotkeyName);
    void SanityCheckHotkeys();

private:
    static std::shared_ptr<Hotkeys> _instance;

    bool ParseHotkeyFile(const std::wstring& path);
    bool CompareNoCase(const std::string_view& a, const char* b);

    std::wstring GetHoverText(keycode keyCode);
    keycode ParseKeycode(std::string_view text, int lineNo);

    keycode _lastCode = 0;
    std::map<keycode, int64_t> _hotkeyCodes;
    std::unordered_set<keycode> _hotkeys;
    std::map<std::string, keycode> _hotkeyNames;
    std::unordered_set<std::string> _registeredHotkeys;
};