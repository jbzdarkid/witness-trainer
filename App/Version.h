#pragma once

#define TO_STRING2(s) L#s
#define TO_STRING(s) TO_STRING2(s)

#define MAJOR 6
#define MINOR 0
#define PATCH 0

#define VERSION_STR     TO_STRING(MAJOR) L"." TO_STRING(MINOR) L"." TO_STRING(PATCH)
#define VERSION			MAJOR, MINOR, PATCH

#define PRODUCT_NAME L"Challenge Randomizer"
#define WINDOW_CLASS L"ChallengeRandomizer"
