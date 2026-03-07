#include "pch.h"
#include "inject.h"

#include <TlHelp32.h>

namespace
{
    enum class BinaryArch
    {
        Unknown = 0,
        X86,
        X64,
    };

    std::wstring ToWide(const char* text)
    {
        if (!text || text[0] == '\0')
        {
            return L"";
        }

        int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
        UINT codePage = CP_UTF8;
        DWORD flags = MB_ERR_INVALID_CHARS;

        if (length <= 0)
        {
            codePage = CP_ACP;
            flags = 0;
            length = MultiByteToWideChar(codePage, flags, text, -1, nullptr, 0);
            if (length <= 0)
            {
                return L"";
            }
        }

        std::wstring wide(static_cast<size_t>(length), L'\0');
        if (MultiByteToWideChar(codePage, flags, text, -1, wide.data(), length) <= 0)
        {
            return L"";
        }

        if (!wide.empty() && wide.back() == L'\0')
        {
            wide.pop_back();
        }

        return wide;
    }

    bool TryGetProcessArch(HANDLE process, BinaryArch& arch, DWORD& win32Error)
    {
        arch = BinaryArch::Unknown;
        win32Error = 0;

        using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
        auto* isWow64Process2Fn = reinterpret_cast<IsWow64Process2Fn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2"));

        if (isWow64Process2Fn != nullptr)
        {
            USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
            USHORT nativeMachine = IMAGE_FILE_MACHINE_UNKNOWN;
            if (!isWow64Process2Fn(process, &processMachine, &nativeMachine))
            {
                win32Error = GetLastError();
                return false;
            }

            if (processMachine == IMAGE_FILE_MACHINE_UNKNOWN)
            {
                if (nativeMachine == IMAGE_FILE_MACHINE_AMD64 || nativeMachine == IMAGE_FILE_MACHINE_ARM64)
                {
                    arch = BinaryArch::X64;
                }
                else
                {
                    arch = BinaryArch::X86;
                }
            }
            else if (processMachine == IMAGE_FILE_MACHINE_I386)
            {
                arch = BinaryArch::X86;
            }
            else if (processMachine == IMAGE_FILE_MACHINE_AMD64 || processMachine == IMAGE_FILE_MACHINE_ARM64)
            {
                arch = BinaryArch::X64;
            }

            return arch != BinaryArch::Unknown;
        }

        BOOL isWow64 = FALSE;
        if (!IsWow64Process(process, &isWow64))
        {
            win32Error = GetLastError();
            return false;
        }

        if (isWow64)
        {
            arch = BinaryArch::X86;
            return true;
        }

        SYSTEM_INFO si{};
        GetNativeSystemInfo(&si);
        if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 || si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64)
        {
            arch = BinaryArch::X64;
        }
        else
        {
            arch = BinaryArch::X86;
        }
        return true;
    }

    bool TryGetDllArch(const wchar_t* dllPath, BinaryArch& arch)
    {
        arch = BinaryArch::Unknown;

        HANDLE dllHandle = CreateFileW(
            dllPath,
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (dllHandle == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        DWORD bytesRead = 0;
        IMAGE_DOS_HEADER dosHeader{};
        if (!ReadFile(dllHandle, &dosHeader, sizeof(dosHeader), &bytesRead, nullptr) || bytesRead != sizeof(dosHeader) || dosHeader.e_magic != IMAGE_DOS_SIGNATURE)
        {
            CloseHandle(dllHandle);
            return false;
        }

        LARGE_INTEGER seekPos{};
        seekPos.QuadPart = dosHeader.e_lfanew;
        if (!SetFilePointerEx(dllHandle, seekPos, nullptr, FILE_BEGIN))
        {
            CloseHandle(dllHandle);
            return false;
        }

        DWORD ntSignature = 0;
        if (!ReadFile(dllHandle, &ntSignature, sizeof(ntSignature), &bytesRead, nullptr) || bytesRead != sizeof(ntSignature) || ntSignature != IMAGE_NT_SIGNATURE)
        {
            CloseHandle(dllHandle);
            return false;
        }

        IMAGE_FILE_HEADER fileHeader{};
        if (!ReadFile(dllHandle, &fileHeader, sizeof(fileHeader), &bytesRead, nullptr) || bytesRead != sizeof(fileHeader))
        {
            CloseHandle(dllHandle);
            return false;
        }

        CloseHandle(dllHandle);

        if (fileHeader.Machine == IMAGE_FILE_MACHINE_I386)
        {
            arch = BinaryArch::X86;
            return true;
        }
        if (fileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 || fileHeader.Machine == IMAGE_FILE_MACHINE_ARM64)
        {
            arch = BinaryArch::X64;
            return true;
        }

        return false;
    }
}

DWORD GetProcId(const char *procName)

{
    const std::wstring requestedName = ToWide(procName);
    if (requestedName.empty())
    {
        return 0;
    }

    DWORD procId = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (hSnap != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32W procEntry{};
        procEntry.dwSize = sizeof(procEntry);

        if (Process32FirstW(hSnap, &procEntry))
        {
            do
            {
                if (_wcsicmp(procEntry.szExeFile, requestedName.c_str()) == 0)
                {
                    procId = procEntry.th32ProcessID;
                    break;
                }
            } while (Process32NextW(hSnap, &procEntry));
        }
        CloseHandle(hSnap);
    }

    return procId;
}

std::string FormatWindowsError(DWORD errorCode)
{
    if (errorCode == 0)
    {
        return "Unknown error";
    }

    char* messageBuffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD length = FormatMessageA(
        flags,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&messageBuffer),
        0,
        nullptr);

    if (length == 0 || messageBuffer == nullptr)
    {
        return "Windows error code " + std::to_string(errorCode);
    }

    std::string message(messageBuffer, length);
    LocalFree(messageBuffer);

    while (!message.empty() && (message.back() == '\r' || message.back() == '\n'))
    {
        message.pop_back();
    }

    return message;
}

PreflightResult RunInjectionPreflight(DWORD procId, const wchar_t* dllPath)
{
    PreflightResult result{};
    result.ok = false;

    if (procId == 0 || dllPath == nullptr || dllPath[0] == L'\0')
    {
        result.error = InjectError::InvalidArgument;
        result.message = "Invalid process or DLL path.";
        return result;
    }

    DWORD fileAttributes = GetFileAttributesW(dllPath);
    if (fileAttributes == INVALID_FILE_ATTRIBUTES || (fileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        result.error = InjectError::DllNotFound;
        result.win32Error = GetLastError();
        result.message = "DLL path does not point to a valid file.";
        return result;
    }

    HANDLE dllHandle = CreateFileW(
        dllPath,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (dllHandle == INVALID_HANDLE_VALUE)
    {
        result.error = InjectError::DllOpenFailed;
        result.win32Error = GetLastError();
        result.message = "Cannot open DLL for reading.";
        return result;
    }
    CloseHandle(dllHandle);

    const DWORD desiredAccess =
        PROCESS_CREATE_THREAD |
        PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE |
        PROCESS_VM_READ;

    HANDLE hProc = OpenProcess(desiredAccess, FALSE, procId);
    if (hProc == nullptr || hProc == INVALID_HANDLE_VALUE)
    {
        result.error = InjectError::ProcessOpenFailed;
        result.win32Error = GetLastError();
        result.message = "Cannot open target process with required access.";
        return result;
    }

    BinaryArch selfArch = BinaryArch::Unknown;
    BinaryArch targetArch = BinaryArch::Unknown;
    DWORD archError = 0;
    if (!TryGetProcessArch(GetCurrentProcess(), selfArch, archError))
    {
        CloseHandle(hProc);
        result.error = InjectError::InvalidArgument;
        result.win32Error = archError;
        result.message = "Cannot detect injector architecture.";
        return result;
    }
    if (!TryGetProcessArch(hProc, targetArch, archError))
    {
        CloseHandle(hProc);
        result.error = InjectError::InvalidArgument;
        result.win32Error = archError;
        result.message = "Cannot detect target process architecture.";
        return result;
    }
    if (selfArch != targetArch)
    {
        CloseHandle(hProc);
        result.error = InjectError::ProcessArchitectureMismatch;
        result.message = "Injector and target process architectures do not match.";
        return result;
    }

    BinaryArch dllArch = BinaryArch::Unknown;
    if (!TryGetDllArch(dllPath, dllArch))
    {
        CloseHandle(hProc);
        result.error = InjectError::DllInvalidImage;
        result.message = "DLL is not a valid x86/x64 PE image.";
        return result;
    }

    if (dllArch != targetArch)
    {
        CloseHandle(hProc);
        result.error = InjectError::DllArchitectureMismatch;
        result.message = "DLL architecture does not match target process.";
        return result;
    }

    CloseHandle(hProc);

    result.ok = true;
    result.error = InjectError::None;
    result.message = "Preflight checks passed.";
    return result;
}

InjectionResult performInjection(DWORD procId, const wchar_t *dllPath)
{
    InjectionResult result{};
    result.ok = false;

    if (procId == 0 || dllPath == nullptr || dllPath[0] == L'\0')
    {
        result.error = InjectError::InvalidArgument;
        result.message = "Invalid process or DLL path.";
        return result;
    }

    const DWORD desiredAccess =
        PROCESS_CREATE_THREAD |
        PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE |
        PROCESS_VM_READ;

    HANDLE hProc = OpenProcess(desiredAccess, FALSE, procId);
    if (hProc == nullptr || hProc == INVALID_HANDLE_VALUE)
    {
        result.error = InjectError::ProcessOpenFailed;
        result.win32Error = GetLastError();
        result.message = "OpenProcess failed.";
        return result;
    }

    const size_t bytesToWrite = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    void* remoteMemory = VirtualAllocEx(hProc, nullptr, bytesToWrite, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remoteMemory == nullptr)
    {
        result.error = InjectError::RemoteAllocationFailed;
        result.win32Error = GetLastError();
        result.message = "VirtualAllocEx failed.";
        CloseHandle(hProc);
        return result;
    }

    SIZE_T bytesWritten = 0;
    if (!WriteProcessMemory(hProc, remoteMemory, dllPath, bytesToWrite, &bytesWritten) || bytesWritten != bytesToWrite)
    {
        result.error = InjectError::RemoteWriteFailed;
        result.win32Error = GetLastError();
        result.message = "WriteProcessMemory failed.";
        VirtualFreeEx(hProc, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return result;
    }

    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC loadLibraryWAddress = (hKernel32 != nullptr) ? GetProcAddress(hKernel32, "LoadLibraryW") : nullptr;
    if (loadLibraryWAddress == nullptr)
    {
        result.error = InjectError::RemoteThreadFailed;
        result.win32Error = GetLastError();
        result.message = "GetProcAddress(LoadLibraryW) failed.";
        VirtualFreeEx(hProc, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return result;
    }

    HANDLE hThread = CreateRemoteThread(
        hProc,
        nullptr,
        0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLibraryWAddress),
        remoteMemory,
        0,
        nullptr);

    if (hThread == nullptr)
    {
        result.error = InjectError::RemoteThreadFailed;
        result.win32Error = GetLastError();
        result.message = "CreateRemoteThread failed.";
        VirtualFreeEx(hProc, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return result;
    }

    DWORD waitCode = WaitForSingleObject(hThread, 15000);
    if (waitCode == WAIT_TIMEOUT)
    {
        result.error = InjectError::RemoteThreadTimedOut;
        result.message = "Remote thread timed out.";
        CloseHandle(hThread);
        VirtualFreeEx(hProc, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return result;
    }

    if (waitCode == WAIT_FAILED)
    {
        result.error = InjectError::RemoteThreadFailed;
        result.win32Error = GetLastError();
        result.message = "Failed while waiting for remote thread.";
        CloseHandle(hThread);
        VirtualFreeEx(hProc, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return result;
    }

    DWORD remoteModule = 0;
    if (!GetExitCodeThread(hThread, &remoteModule))
    {
        result.error = InjectError::RemoteThreadFailed;
        result.win32Error = GetLastError();
        result.message = "GetExitCodeThread failed.";
        CloseHandle(hThread);
        VirtualFreeEx(hProc, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return result;
    }

    if (remoteModule == 0)
    {
        result.error = InjectError::RemoteLoadLibraryFailed;
        result.message = "LoadLibraryW failed inside target process.";
        CloseHandle(hThread);
        VirtualFreeEx(hProc, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return result;
    }

    CloseHandle(hThread);
    VirtualFreeEx(hProc, remoteMemory, 0, MEM_RELEASE);
    CloseHandle(hProc);

    result.ok = true;
    result.error = InjectError::None;
    result.message = "Injection successful.";
    return result;
}
