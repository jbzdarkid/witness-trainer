#pragma once
#include <unordered_map>
#include <string>

struct Entities {
    static std::unordered_map<int32_t, const char*> ALL_ENTITIES;
	static std::unordered_map<int32_t, const char*> COUNTED_PANELS;
    static std::unordered_map<int32_t, const char*> UNCOUNTED_PANELS;

    static std::string NameOf(int id);

    // https://stackoverflow.com/a/5804024
    struct Constructor {
        Constructor();
    };

private:
    friend struct Constructor;
    static Constructor _construct;
};
