#pragma once
#include <map>
#include <memory>
#include <string>
#include <unordered_set>

constexpr uint32_t MASK_SHIFT   = 0x0100;
constexpr uint32_t MASK_CONTROL = 0x0200;
constexpr uint32_t MASK_ALT     = 0x0400;
constexpr uint32_t MASK_WIN     = 0x0800;
constexpr uint32_t MASK_REPEAT  = 0x1000;

class Hotkeys {
	typedef int32_t keycode;

public:
	static std::shared_ptr<Hotkeys> Get();
	Hotkeys();

	int64_t CheckMatchingHotkey(WPARAM wParam, LPARAM lParam);

	void RegisterHotkey(LPCSTR hotkeyName, int64_t message);
	std::wstring GetHoverText(LPCSTR hotkeyName);

private:
	static std::shared_ptr<Hotkeys> _instance;

	std::wstring GetHoverText(keycode keyCode);

	keycode _lastCode;
	std::map<keycode, int64_t> _hotkeyCodes;
	std::unordered_set<keycode> _hotkeys;
	std::map<LPCSTR, keycode> _hotkeyNames;
};