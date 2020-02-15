#pragma once

#define WIN32_LEAN_AND_MEAN 1
#define VC_EXTRALEAN 1
#include <windows.h>
#undef min
#undef max

// @Cleanup.
#include <algorithm>
#include <cassert>
#include <chrono>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma warning (disable: 26451) // Potential arithmetic overflow
#pragma warning (disable: 26812) // Unscoped enum type

#include "MemoryException.h"
#include "Memory.h"

void DebugPrint(std::string text);