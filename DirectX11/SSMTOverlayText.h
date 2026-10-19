#pragma once

#include <Windows.h>
#include <cstdarg>
#include <string>

namespace SSMTOverlayText {
struct Message {
    std::wstring english;
    std::wstring chinese;
};

// 只选择显示语言，不改变游戏进程的全局 CRT locale 或配置值。
bool Configure(const wchar_t* language, LANGID systemLanguage = GetUserDefaultUILanguage());
bool UseChinese();
Message Format(const char* format, va_list args);
Message Format(const wchar_t* format, va_list args);
std::wstring FontCharacters();
const wchar_t* HuntingFormat(bool chinese);
}
