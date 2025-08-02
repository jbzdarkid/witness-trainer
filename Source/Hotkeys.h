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

	bool ParseHotkeyFile();
	std::wstring GetHoverText(keycode keyCode);

	keycode _lastCode = 0;
	std::map<keycode, int64_t> _hotkeyCodes;
	std::unordered_set<keycode> _hotkeys;
	std::map<LPCSTR, keycode> _hotkeyNames;

	const LPCSTR DEFAULT_KEYBINDS =
		"noclip_enabled: Control-N\n"
		"can_save_game: Shift-Control-S\n";
	/*
	_hotkeyNames["open_console"] = MASK_SHIFT | VK_OEM_3;
	_hotkeyNames["ep_overlay"] = MASK_ALT | '2';
	_hotkeyNames["save_position"] = MASK_CONTROL | 'P';
	_hotkeyNames["load_position"] = MASK_SHIFT | MASK_CONTROL | 'P';
	_hotkeyNames["snap_to_panel"] = MASK_CONTROL | 'L';
	_hotkeyNames["open_doors"] = MASK_CONTROL | 'O';
	_hotkeyNames["dump_callstack"] = MASK_CONTROL | MASK_SHIFT | MASK_ALT | VK_OEM_PLUS;
	*/

};