#pragma once
#include <map>
#include <memory>
#include <unordered_set>

constexpr uint32_t MASK_SHIFT   = 0x0100;
constexpr uint32_t MASK_CONTROL = 0x0200;
constexpr uint32_t MASK_ALT     = 0x0400;
constexpr uint32_t MASK_WIN     = 0x0800;
constexpr uint32_t MASK_REPEAT  = 0x1000;

class Hotkeys {
public:
	static std::shared_ptr<Hotkeys> Get();

	int64_t Foo(WPARAM wParam, LPARAM lParam);
	void SetHotkey(int32_t keyCode, int64_t message);
	void SetHotkey(int32_t keyCode, int32_t message);

private:
	static std::shared_ptr<Hotkeys> _instance;

	int32_t _lastCode;
	std::map<int32_t, int64_t> _hotkeyCodes;
	std::unordered_set<int32_t> _hotkeys;
};