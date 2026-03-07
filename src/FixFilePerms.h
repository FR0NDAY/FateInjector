#pragma once

#include <windows.h>
#include <string>

bool SetAccessControl(const std::wstring& ExecutableName, const wchar_t* AccessString, DWORD* win32Error = nullptr);
