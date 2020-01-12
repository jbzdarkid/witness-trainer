#include "pch.h"

void DebugPrint(std::string text) {
#ifdef _DEBUG
    if (text[text.size()-1] != '\n') {
        text += '\n';
    }
    OutputDebugStringA(text.c_str());
    std::cout << text;
#endif
}
