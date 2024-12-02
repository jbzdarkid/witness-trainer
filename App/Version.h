#pragma once

#define TO_STRING2(s) L#s
#define TO_STRING(s) TO_STRING2(s)

#define MAJOR 0
#define MINOR 0
#define PATCH 1

#define VERSION_STR     TO_STRING(MAJOR) L"." TO_STRING(MINOR) L"." TO_STRING(PATCH)
#define VERSION			MAJOR, MINOR, PATCH

#define PRODUCT_NAME L"BattleBlock Save Editor"
#define WINDOW_CLASS L"BattleBlockSave"
