#include "pch.h"

#include "config.h"

#include <fstream>
#include <algorithm>
#include <direct.h>
#include <cwctype>

char working_dir[1024];
bool customProcName = false;
std::wstring delaystr = L"5";
std::wstring dllPath = L"Click \"Select\" to select the dll file";
std::wstring procName = L"minecraft.windows.exe";

config::config()
{
    path = working_dir;
    path += "\\config.txt";
}

bool config::loadConfig()
{
    std::wifstream cFile(path);
    if (cFile.is_open())
    {
        std::wstring line;
        while (getline(cFile, line))
        {
            if (line.empty() || line[0] == L'#')
                continue;
            size_t delimiterPos = line.find('=');
            if (delimiterPos == std::wstring::npos)
            {
                continue;
            }
            name = line.substr(0, delimiterPos);
            value = line.substr(delimiterPos + 1);
            analyseState();
        }
        return false;
    }
    else
    {
        return true;
    }
}

bool config::saveConfig()
{
    std::wofstream create(path);
    if (create.is_open())
    {
        std::wstring configstr = makeConfig();
        create << configstr;
    }
    else
    {
        wxMessageBox("Can't create config file!", "Fate Client ERROR", wxICON_ERROR);
        // std::cout << "Couldn't create config file on " + path << std::endl;
        return true;
    }
    return false;
}

bool config::analyseBool()
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c)
                   { return static_cast<wchar_t>(std::towlower(c)); });
    if (value == L"true" || value == L"1")
    {
        std::cout << name << " "
                  << "true" << '\n';
        return true;
    }
    else
    {
        std::cout << name << " "
                  << "false" << '\n';
        return false;
    }
}

int config::analyseInt()
{
    if (!value.empty() && std::all_of(value.begin(), value.end(), [](wchar_t c)
                                      { return std::iswdigit(c) != 0; }))
    {
        std::cout << name << " " << value << '\n';
        return std::stoi(value);
    }
    else
    {
        std::cout << name << " Is not parsable \"" << value << "\"\n";
        return 0;
    }
}

std::wstring config::makeConfig()
{
    std::wstring generatedConfig;

    generatedConfig += L"#Fate Client injector config file\n";

    // customProcName
    generatedConfig += customProcName == true ? L"customProcName=true\n" : L"customProcName=false\n";
    // delaystr
    generatedConfig += L"delaystr=" + delaystr + L"\n";
    // dllPath
    generatedConfig += L"dllPath=" + dllPath + L"\n";
    // procName
    generatedConfig += L"procName=" + procName + L"\n";

    return generatedConfig;
}

void config::analyseState()
{
    if (name == L"customProcName")
    {
        customProcName = analyseBool();
    }
    else if (name == L"delaystr")
    {
        delaystr = value;
    }
    else if (name == L"dllPath")
    {
        dllPath = value;
    }
    else if (name == L"procName")
    {
        procName = value;
    }
    else
    {
        wxMessageBox(wxString::Format(L"\"%ls\" Is not a known Entry\nDeleting the config file might help!", name.c_str()),
                     L"Fate Config WARNING",
                     wxICON_INFORMATION);
    }
}
