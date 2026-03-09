#include "pch.h"

#include "FixFilePerms.h"
#include "config.h"
#include "inject.h"

#include <direct.h>

#include <algorithm>

namespace
{
    constexpr wchar_t kWindowClassName[] = L"FateInjectorWindow";
    constexpr wchar_t kWindowTitle[] = L"Fate Client Injector";
    constexpr wchar_t kDefaultProcessName[] = L"minecraft.windows.exe";

    constexpr UINT WMAPP_TRAYICON = WM_APP + 1;
    constexpr UINT_PTR ID_TIMER_AUTO_INJECT = 1;

    constexpr int ID_BUTTON_INJECT = 101;
    constexpr int ID_BUTTON_HIDE = 102;
    constexpr int ID_BUTTON_SELECT = 103;
    constexpr int ID_CHECKBOX_CUSTOM = 201;
    constexpr int ID_CHECKBOX_AUTO = 202;

    constexpr int ID_TRAY_INJECT = 301;
    constexpr int ID_TRAY_OPEN = 302;
    constexpr int ID_TRAY_CLOSE = 303;

    constexpr int ID_EDIT_NAME = 401;
    constexpr int ID_EDIT_DELAY = 402;
    constexpr int ID_EDIT_PATH = 403;
    constexpr int ID_STATUS_TEXT = 501;
    constexpr int ID_GROUP_TARGET = 601;
    constexpr int ID_GROUP_INJECT = 602;

    constexpr int IDI_ICON1 = 101;
}

class InjectorWindow
{
public:
    explicit InjectorWindow(HINSTANCE instance)
        : instance_(instance)
    {
    }

    ~InjectorWindow()
    {
        RemoveTrayIcon();
        if (ownsUIFont_ && uiFont_ != nullptr)
        {
            DeleteObject(uiFont_);
            uiFont_ = nullptr;
        }
    }

    bool Create()
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = InjectorWindow::WndProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = instance_;
        wc.hIcon = LoadIconResource(32);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszMenuName = nullptr;
        wc.lpszClassName = kWindowClassName;
        wc.hIconSm = LoadIconResource(16);

        if (!RegisterClassExW(&wc))
        {
            if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            {
                return false;
            }
        }

        hwnd_ = CreateWindowExW(
            WS_EX_APPWINDOW,
            kWindowClassName,
            kWindowTitle,
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            560,
            320,
            nullptr,
            nullptr,
            instance_,
            this);

        return hwnd_ != nullptr;
    }

    int Run()
    {
        ShowWindow(hwnd_, SW_SHOWDEFAULT);
        UpdateWindow(hwnd_);

        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        return static_cast<int>(msg.wParam);
    }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        InjectorWindow* self = reinterpret_cast<InjectorWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (msg == WM_NCCREATE)
        {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = reinterpret_cast<InjectorWindow*>(create->lpCreateParams);
            if (self != nullptr)
            {
                self->hwnd_ = hwnd;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            }
        }

        if (self != nullptr)
        {
            return self->HandleMessage(msg, wParam, lParam);
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_CREATE:
            CreateUIFont();
            CreateControls();
            ApplyInitialState();
            AddTrayIcon();
            SetStatusText(L"Ready. Select a DLL and click Inject.");
            return 0;

        case WM_SIZE:
            LayoutControls();
            return 0;

        case WM_COMMAND:
            return HandleCommand(LOWORD(wParam), HIWORD(wParam));

        case WM_TIMER:
            if (wParam == ID_TIMER_AUTO_INJECT)
            {
                OnAutoInjectTimer();
                return 0;
            }
            break;

        case WMAPP_TRAYICON:
            HandleTrayEvent(static_cast<UINT>(lParam));
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd_, ID_TIMER_AUTO_INJECT);
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    }

    LRESULT HandleCommand(int id, int code)
    {
        switch (id)
        {
        case ID_BUTTON_INJECT:
            if (code == BN_CLICKED)
            {
                InjectCurrentTarget(false);
            }
            return 0;

        case ID_BUTTON_HIDE:
            if (code == BN_CLICKED)
            {
                HideToTray();
            }
            return 0;

        case ID_BUTTON_SELECT:
            if (code == BN_CLICKED)
            {
                OnSelectDll();
            }
            return 0;

        case ID_CHECKBOX_CUSTOM:
            if (code == BN_CLICKED)
            {
                HandleCustomCheckBox();
                SaveCurrentConfig();
            }
            return 0;

        case ID_CHECKBOX_AUTO:
            if (code == BN_CLICKED)
            {
                HandleAutoCheckBox();
                SaveCurrentConfig();
            }
            return 0;

        case ID_TRAY_INJECT:
            InjectCurrentTarget(false);
            return 0;

        case ID_TRAY_OPEN:
            ShowFromTray();
            return 0;

        case ID_TRAY_CLOSE:
            DestroyWindow(hwnd_);
            return 0;
        }

        return 0;
    }

    void CreateUIFont()
    {
        HDC hdc = GetDC(hwnd_);
        const int fontHeight = -MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72);
        ReleaseDC(hwnd_, hdc);

        uiFont_ = CreateFontW(
            fontHeight,
            0,
            0,
            0,
            FW_NORMAL,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");

        if (uiFont_ != nullptr)
        {
            ownsUIFont_ = true;
        }
        else
        {
            uiFont_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            ownsUIFont_ = false;
        }
    }

    void CreateControls()
    {
        const DWORD commonControlStyle = WS_CHILD | WS_VISIBLE;

        groupTarget_ = CreateWindowExW(0, L"BUTTON", L"Target Process", commonControlStyle | BS_GROUPBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(ID_GROUP_TARGET), instance_, nullptr);
        labelProcess_ = CreateWindowExW(0, L"STATIC", L"Process:", commonControlStyle, 0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);
        txtName_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", kDefaultProcessName, commonControlStyle | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(ID_EDIT_NAME), instance_, nullptr);
        checkCustom_ = CreateWindowExW(0, L"BUTTON", L"Custom target", commonControlStyle | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(ID_CHECKBOX_CUSTOM), instance_, nullptr);

        groupInject_ = CreateWindowExW(0, L"BUTTON", L"Injection", commonControlStyle | BS_GROUPBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(ID_GROUP_INJECT), instance_, nullptr);
        labelDll_ = CreateWindowExW(0, L"STATIC", L"DLL:", commonControlStyle, 0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);
        txtPath_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", commonControlStyle | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(ID_EDIT_PATH), instance_, nullptr);
        btnSelect_ = CreateWindowExW(0, L"BUTTON", L"Browse", commonControlStyle | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(ID_BUTTON_SELECT), instance_, nullptr);
        checkAuto_ = CreateWindowExW(0, L"BUTTON", L"Auto inject", commonControlStyle | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(ID_CHECKBOX_AUTO), instance_, nullptr);
        labelDelay_ = CreateWindowExW(0, L"STATIC", L"Delay (s):", commonControlStyle, 0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);
        txtDelay_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"5", commonControlStyle | WS_TABSTOP | ES_AUTOHSCROLL | ES_CENTER | ES_NUMBER, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(ID_EDIT_DELAY), instance_, nullptr);

        btnInject_ = CreateWindowExW(0, L"BUTTON", L"Inject", commonControlStyle | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(ID_BUTTON_INJECT), instance_, nullptr);
        btnHide_ = CreateWindowExW(0, L"BUTTON", L"Hide to Tray", commonControlStyle | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(ID_BUTTON_HIDE), instance_, nullptr);
        txtStatus_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"", commonControlStyle | SS_LEFTNOWORDWRAP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(ID_STATUS_TEXT), instance_, nullptr);

        SendMessageW(txtDelay_, EM_SETLIMITTEXT, 4, 0);

        ApplyFont(groupTarget_);
        ApplyFont(labelProcess_);
        ApplyFont(txtName_);
        ApplyFont(checkCustom_);
        ApplyFont(groupInject_);
        ApplyFont(labelDll_);
        ApplyFont(txtPath_);
        ApplyFont(btnSelect_);
        ApplyFont(checkAuto_);
        ApplyFont(labelDelay_);
        ApplyFont(txtDelay_);
        ApplyFont(btnInject_);
        ApplyFont(btnHide_);
        ApplyFont(txtStatus_);

        LayoutControls();
    }

    void LayoutControls()
    {
        RECT rc{};
        GetClientRect(hwnd_, &rc);

        const int width = rc.right - rc.left;
        const int height = rc.bottom - rc.top;

        const int margin = 12;
        const int groupWidth = width - (margin * 2);

        const int targetY = margin;
        const int targetHeight = 78;

        const int injectY = targetY + targetHeight + 10;
        const int injectHeight = 104;

        const int statusHeight = 22;
        const int statusY = height - margin - statusHeight;

        int buttonY = injectY + injectHeight + 10;
        buttonY = std::min(buttonY, statusY - 36);

        MoveWindow(groupTarget_, margin, targetY, groupWidth, targetHeight, TRUE);
        MoveWindow(labelProcess_, margin + 16, targetY + 30, 58, 20, TRUE);
        MoveWindow(checkCustom_, width - margin - 130, targetY + 30, 112, 20, TRUE);
        MoveWindow(txtName_, margin + 78, targetY + 27, width - (margin * 2) - 214, 24, TRUE);

        MoveWindow(groupInject_, margin, injectY, groupWidth, injectHeight, TRUE);
        MoveWindow(labelDll_, margin + 16, injectY + 30, 40, 20, TRUE);
        MoveWindow(txtPath_, margin + 60, injectY + 27, width - (margin * 2) - 156, 24, TRUE);
        MoveWindow(btnSelect_, width - margin - 84, injectY + 27, 70, 24, TRUE);
        MoveWindow(checkAuto_, margin + 60, injectY + 61, 100, 20, TRUE);
        MoveWindow(labelDelay_, margin + 172, injectY + 61, 62, 20, TRUE);
        MoveWindow(txtDelay_, margin + 236, injectY + 58, 54, 24, TRUE);

        MoveWindow(btnInject_, width - margin - 196, buttonY, 92, 30, TRUE);
        MoveWindow(btnHide_, width - margin - 98, buttonY, 92, 30, TRUE);

        MoveWindow(txtStatus_, margin, statusY, groupWidth, statusHeight, TRUE);
    }

    void ApplyInitialState()
    {
        SetText(txtName_, procName);
        SetText(txtDelay_, delaystr);
        SetText(txtPath_, dllPath);

        if (GetText(txtPath_).empty())
        {
            SetText(txtPath_, L"Click Browse to select the DLL file");
        }

        SendMessageW(checkCustom_, BM_SETCHECK, customProcName ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(checkAuto_, BM_SETCHECK, BST_UNCHECKED, 0);

        HandleCustomCheckBox();
    }

    void HandleCustomCheckBox()
    {
        if (IsChecked(checkCustom_))
        {
            EnableWindow(txtName_, TRUE);
        }
        else
        {
            SetText(txtName_, kDefaultProcessName);
            EnableWindow(txtName_, FALSE);
        }
    }

    void HandleAutoCheckBox()
    {
        if (IsChecked(checkAuto_))
        {
            EnableWindow(txtName_, FALSE);
            EnableWindow(txtPath_, FALSE);
            EnableWindow(txtDelay_, FALSE);
            EnableWindow(btnSelect_, FALSE);
            EnableWindow(checkCustom_, FALSE);

            lastAutoInjectedProcId_ = 0;
            const int delay = GetValidatedDelaySeconds();
            SetTimer(hwnd_, ID_TIMER_AUTO_INJECT, static_cast<UINT>(delay * 1000), nullptr);
            SetStatusText(L"AutoInject: Enabled | trying every " + std::to_wstring(delay) + L" seconds");
        }
        else
        {
            KillTimer(hwnd_, ID_TIMER_AUTO_INJECT);
            DisableAutoInject();
            SetStatusText(L"AutoInject: Disabled");
        }
    }

    void DisableAutoInject()
    {
        EnableWindow(checkCustom_, TRUE);
        if (IsChecked(checkCustom_))
        {
            EnableWindow(txtName_, TRUE);
        }
        else
        {
            EnableWindow(txtName_, FALSE);
        }

        EnableWindow(txtPath_, TRUE);
        EnableWindow(txtDelay_, TRUE);
        EnableWindow(btnSelect_, TRUE);
    }

    void OnAutoInjectTimer()
    {
        if (!IsChecked(checkAuto_))
        {
            KillTimer(hwnd_, ID_TIMER_AUTO_INJECT);
            DisableAutoInject();
            return;
        }

        const std::string processName = ToUtf8(GetText(txtName_));
        DWORD procId = GetProcId(processName.c_str());
        if (procId == 0)
        {
            SetStatusText(L"AutoInject: Can't find process! | 0");
            return;
        }

        if (procId == lastAutoInjectedProcId_)
        {
            SetStatusText(L"AutoInject: Already injected! | " + std::to_wstring(procId));
            return;
        }

        DWORD injectedProcId = 0;
        if (InjectCurrentTarget(true, &injectedProcId))
        {
            lastAutoInjectedProcId_ = injectedProcId;
        }
        else if (GetProcId(processName.c_str()) != 0)
        {
            SendMessageW(checkAuto_, BM_SETCHECK, BST_UNCHECKED, 0);
            KillTimer(hwnd_, ID_TIMER_AUTO_INJECT);
            DisableAutoInject();
        }
    }

    void OnSelectDll()
    {
        wchar_t filePath[MAX_PATH] = {0};
        std::wstring initialPath = GetText(txtPath_);
        if (!initialPath.empty() && initialPath.size() < MAX_PATH)
        {
            wcsncpy_s(filePath, initialPath.c_str(), _TRUNCATE);
        }

        const std::wstring initialDir = ToWide(working_dir);
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = hwnd_;
        dialog.lpstrFilter = L"Dynamic Link Library (*.dll)\0*.dll\0All Files (*.*)\0*.*\0";
        dialog.lpstrFile = filePath;
        dialog.nMaxFile = MAX_PATH;
        dialog.lpstrInitialDir = initialDir.c_str();
        dialog.lpstrDefExt = L"dll";
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

        if (GetOpenFileNameW(&dialog))
        {
            SetText(txtPath_, filePath);
            SaveCurrentConfig();
        }
    }

    int GetValidatedDelaySeconds()
    {
        int delay = _wtoi(GetText(txtDelay_).c_str());
        if (delay < 1)
        {
            delay = 1;
        }
        else if (delay > 3600)
        {
            delay = 3600;
        }

        SetText(txtDelay_, std::to_wstring(delay));
        return delay;
    }

    void SaveCurrentConfig()
    {
        customProcName = IsChecked(checkCustom_);
        delaystr = GetText(txtDelay_);
        dllPath = GetText(txtPath_);
        procName = GetText(txtName_);

        config cfg;
        cfg.saveConfig();
    }

    bool InjectCurrentTarget(bool autoInjectMode, DWORD* injectedProcId = nullptr)
    {
        std::string prefix;
        if (autoInjectMode)
        {
            prefix = "AutoInject: ";
        }

        const std::string processName = ToUtf8(GetText(txtName_));
        DWORD procId = GetProcId(processName.c_str());
        if (procId == 0)
        {
            SetStatusText(ToWide(prefix + "Can't find process! | 0"));
            return false;
        }

        std::wstring dllPathValue = GetText(txtPath_);
        PreflightResult preflight = RunInjectionPreflight(procId, dllPathValue.c_str());
        if (!preflight.ok)
        {
            std::string status = prefix + preflight.message;
            if (preflight.win32Error != 0)
            {
                status += " (error " + std::to_string(preflight.win32Error) + ": " + FormatWindowsError(preflight.win32Error) + ")";
            }
            SetStatusText(ToWide(status));
            return false;
        }

        DWORD accessError = 0;
        if (!SetAccessControl(dllPathValue, L"S-1-15-2-1", &accessError))
        {
            std::string status = prefix + "Failed to set DLL read/execute permissions.";
            if (accessError != 0)
            {
                status += " (error " + std::to_string(accessError) + ": " + FormatWindowsError(accessError) + ")";
            }
            SetStatusText(ToWide(status));
            return false;
        }

        InjectionResult result = performInjection(procId, dllPathValue.c_str());
        if (!result.ok)
        {
            std::string status = prefix + result.message;
            if (result.win32Error != 0)
            {
                status += " (error " + std::to_string(result.win32Error) + ": " + FormatWindowsError(result.win32Error) + ")";
            }
            SetStatusText(ToWide(status));
            return false;
        }

        SetStatusText(ToWide(prefix + "Injected successfully into process " + std::to_string(procId)));
        if (injectedProcId != nullptr)
        {
            *injectedProcId = procId;
        }

        SaveCurrentConfig();
        return true;
    }

    void AddTrayIcon()
    {
        if (trayIconAdded_)
        {
            return;
        }

        ZeroMemory(&trayIconData_, sizeof(trayIconData_));
        trayIconData_.cbSize = sizeof(trayIconData_);
        trayIconData_.hWnd = hwnd_;
        trayIconData_.uID = 1;
        trayIconData_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        trayIconData_.uCallbackMessage = WMAPP_TRAYICON;

        trayIconData_.hIcon = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
        if (trayIconData_.hIcon != nullptr)
        {
            ownsTrayIcon_ = true;
        }
        else
        {
            trayIconData_.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
            ownsTrayIcon_ = false;
        }

        wcsncpy_s(trayIconData_.szTip, L"Fate Client Injector", _TRUNCATE);

        trayIconAdded_ = Shell_NotifyIconW(NIM_ADD, &trayIconData_) == TRUE;
        if (trayIconAdded_)
        {
            trayIconData_.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &trayIconData_);
        }

        SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(trayIconData_.hIcon));
        SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(trayIconData_.hIcon));
    }

    void RemoveTrayIcon()
    {
        if (trayIconAdded_)
        {
            Shell_NotifyIconW(NIM_DELETE, &trayIconData_);
            trayIconAdded_ = false;
        }

        if (ownsTrayIcon_ && trayIconData_.hIcon != nullptr)
        {
            DestroyIcon(trayIconData_.hIcon);
            trayIconData_.hIcon = nullptr;
            ownsTrayIcon_ = false;
        }
    }

    void HideToTray()
    {
        ShowWindow(hwnd_, SW_HIDE);
        ShowTrayBalloon(L"Fate Client Injector is now hidden in your system tray.");
    }

    void ShowFromTray()
    {
        ShowWindow(hwnd_, SW_SHOW);
        ShowWindow(hwnd_, SW_RESTORE);
        SetForegroundWindow(hwnd_);
    }

    void ShowTrayBalloon(const wchar_t* message)
    {
        if (!trayIconAdded_)
        {
            return;
        }

        NOTIFYICONDATAW notifyData = trayIconData_;
        notifyData.uFlags = NIF_INFO;
        notifyData.dwInfoFlags = NIIF_INFO;
        wcsncpy_s(notifyData.szInfoTitle, L"Fate Client Injector", _TRUNCATE);
        wcsncpy_s(notifyData.szInfo, message, _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &notifyData);
    }

    void HandleTrayEvent(UINT eventId)
    {
        switch (eventId)
        {
        case WM_LBUTTONDBLCLK:
            ShowFromTray();
            break;

        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowTrayContextMenu();
            break;
        }
    }

    void ShowTrayContextMenu()
    {
        HMENU menu = CreatePopupMenu();
        if (menu == nullptr)
        {
            return;
        }

        AppendMenuW(menu, MF_STRING, ID_TRAY_INJECT, L"Inject");
        AppendMenuW(menu, MF_STRING, ID_TRAY_OPEN, L"Open");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, ID_TRAY_CLOSE, L"Close");

        POINT cursor{};
        GetCursorPos(&cursor);

        SetForegroundWindow(hwnd_);
        const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, hwnd_, nullptr);
        DestroyMenu(menu);

        if (command != 0)
        {
            SendMessageW(hwnd_, WM_COMMAND, MAKEWPARAM(command, 0), 0);
        }
    }

    bool IsChecked(HWND control) const
    {
        return SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    void SetStatusText(const std::wstring& text)
    {
        SetWindowTextW(txtStatus_, text.c_str());
    }

    std::wstring GetText(HWND control) const
    {
        const int length = GetWindowTextLengthW(control);
        if (length <= 0)
        {
            return L"";
        }

        std::wstring text(static_cast<size_t>(length + 1), L'\0');
        GetWindowTextW(control, text.data(), length + 1);
        text.resize(static_cast<size_t>(length));
        return text;
    }

    void SetText(HWND control, const std::wstring& value)
    {
        SetWindowTextW(control, value.c_str());
    }

    void ApplyFont(HWND control)
    {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    }

    HICON LoadIconResource(int iconSize)
    {
        HICON icon = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON, iconSize, iconSize, LR_DEFAULTCOLOR));
        if (icon != nullptr)
        {
            return icon;
        }

        return LoadIcon(nullptr, IDI_APPLICATION);
    }

    std::wstring ToWide(const std::string& text) const
    {
        if (text.empty())
        {
            return L"";
        }

        int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        UINT codePage = CP_UTF8;
        if (size <= 0)
        {
            codePage = CP_ACP;
            size = MultiByteToWideChar(codePage, 0, text.c_str(), -1, nullptr, 0);
            if (size <= 0)
            {
                return L"";
            }
        }

        std::wstring wide(static_cast<size_t>(size), L'\0');
        if (MultiByteToWideChar(codePage, 0, text.c_str(), -1, wide.data(), size) <= 0)
        {
            return L"";
        }

        if (!wide.empty() && wide.back() == L'\0')
        {
            wide.pop_back();
        }

        return wide;
    }

    std::string ToUtf8(const std::wstring& text) const
    {
        if (text.empty())
        {
            return "";
        }

        int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
        UINT codePage = CP_UTF8;
        if (size <= 0)
        {
            codePage = CP_ACP;
            size = WideCharToMultiByte(codePage, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (size <= 0)
            {
                return "";
            }
        }

        std::string narrow(static_cast<size_t>(size), '\0');
        if (WideCharToMultiByte(codePage, 0, text.c_str(), -1, narrow.data(), size, nullptr, nullptr) <= 0)
        {
            return "";
        }

        if (!narrow.empty() && narrow.back() == '\0')
        {
            narrow.pop_back();
        }

        return narrow;
    }

private:
    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HFONT uiFont_ = nullptr;
    bool ownsUIFont_ = false;

    HWND groupTarget_ = nullptr;
    HWND labelProcess_ = nullptr;
    HWND txtName_ = nullptr;
    HWND checkCustom_ = nullptr;

    HWND groupInject_ = nullptr;
    HWND labelDll_ = nullptr;
    HWND txtPath_ = nullptr;
    HWND btnSelect_ = nullptr;
    HWND checkAuto_ = nullptr;
    HWND labelDelay_ = nullptr;
    HWND txtDelay_ = nullptr;

    HWND btnInject_ = nullptr;
    HWND btnHide_ = nullptr;
    HWND txtStatus_ = nullptr;

    NOTIFYICONDATAW trayIconData_{};
    bool trayIconAdded_ = false;
    bool ownsTrayIcon_ = false;
    DWORD lastAutoInjectedProcId_ = 0;
};

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    INITCOMMONCONTROLSEX icex{};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    if (_getcwd(working_dir, FILENAME_MAX) == nullptr)
    {
        MessageBoxW(nullptr, L"Could not determine current working directory.", L"Fate Client ERROR", MB_ICONERROR | MB_OK);
        return 1;
    }

    config cfg;
    if (cfg.loadConfig())
    {
        cfg.saveConfig();
    }

    InjectorWindow app(hInstance);
    if (!app.Create())
    {
        MessageBoxW(nullptr, L"Failed to create main window.", L"Fate Client ERROR", MB_ICONERROR | MB_OK);
        return 1;
    }

    return app.Run();
}
