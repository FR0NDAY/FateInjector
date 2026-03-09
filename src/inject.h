#pragma once

#include <windows.h>

#include <string>

enum class InjectError
{
    None = 0,
    InvalidArgument,
    ProcessNotFound,
    ProcessOpenFailed,
    ProcessArchitectureMismatch,
    DllNotFound,
    DllOpenFailed,
    DllInvalidImage,
    DllArchitectureMismatch,
    RemoteAllocationFailed,
    RemoteWriteFailed,
    RemoteThreadFailed,
    RemoteThreadTimedOut,
    RemoteLoadLibraryFailed,
};

struct PreflightResult
{
    bool ok = false;
    InjectError error = InjectError::InvalidArgument;
    DWORD win32Error = 0;
    std::string message;
};

struct InjectionResult
{
    bool ok = false;
    InjectError error = InjectError::InvalidArgument;
    DWORD win32Error = 0;
    std::string message;
};

DWORD GetProcId(const char* procName);
PreflightResult RunInjectionPreflight(DWORD procId, const wchar_t* dllPath);
InjectionResult performInjection(DWORD procId, const wchar_t* dllPath);
std::string FormatWindowsError(DWORD errorCode);
