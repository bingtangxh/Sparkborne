#include "Sparkborne.hpp"

bool btxh::ToggleRegistryItem(const StartupItem& item)
{
    const bool enable=!item.enabled;
    const std::wstring fromKey=enable ? kDisabledRunSubkey : kRunSubkey;
    const std::wstring toKey=enable ? kRunSubkey : kDisabledRunSubkey;

    HKEY sourceRead=nullptr;
    if (RegOpenKeyExW(item.root,fromKey.c_str(),0,KEY_QUERY_VALUE,&sourceRead)!=ERROR_SUCCESS)
    {
        return false;
    }

    DWORD type=0;
    DWORD dataSize=0;
    if (RegQueryValueExW(sourceRead,item.name.c_str(),nullptr,&type,nullptr,&dataSize)!=ERROR_SUCCESS)
    {
        RegCloseKey(sourceRead);
        return false;
    }

    std::vector<BYTE> data(dataSize);
    if (RegQueryValueExW(sourceRead,item.name.c_str(),nullptr,&type,data.data(),&dataSize)!=ERROR_SUCCESS)
    {
        RegCloseKey(sourceRead);
        return false;
    }
    RegCloseKey(sourceRead);

    HKEY target=nullptr;
    DWORD disposition=0;
    if (RegCreateKeyExW(item.root,toKey.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&target,&disposition)!=ERROR_SUCCESS)
    {
        return false;
    }

    const auto setStatus=RegSetValueExW(target,item.name.c_str(),0,type,data.data(),dataSize);
    if (setStatus!=ERROR_SUCCESS)
    {
        RegCloseKey(target);
        return false;
    }

    HKEY sourceWrite=nullptr;
    if (RegOpenKeyExW(item.root,fromKey.c_str(),0,KEY_SET_VALUE,&sourceWrite)!=ERROR_SUCCESS)
    {
        RegCloseKey(target);
        return false;
    }

    const auto deleteStatus=RegDeleteValueW(sourceWrite,item.name.c_str());
    RegCloseKey(sourceWrite);
    RegCloseKey(target);
    return deleteStatus==ERROR_SUCCESS;
}

bool btxh::ToggleShortcutItem(const StartupItem& item)
{
    const std::filesystem::path sourcePath(item.shortcutPath);
    std::filesystem::path targetPath=sourcePath;
    targetPath.replace_extension(item.enabled ? L".dis" : L".lnk");
    std::error_code ec;
    std::filesystem::rename(sourcePath,targetPath,ec);
    return !ec;
}


bool btxh::DeleteRegistryItem(const StartupItem& item)
{
    HKEY key=nullptr;
    if (RegOpenKeyExW(item.root,item.keyPath.c_str(),0,KEY_SET_VALUE,&key)!=ERROR_SUCCESS)
    {
        return false;
    }
    const auto status=RegDeleteValueW(key,item.name.c_str());
    RegCloseKey(key);
    return status==ERROR_SUCCESS;
}

bool btxh::DeleteShortcutItem(const StartupItem& item)
{
    std::error_code ec;
    return std::filesystem::remove(item.shortcutPath,ec)&&!ec;
}

bool btxh::DeleteStartupItem(const StartupItem& item)
{
    return item.type==ItemType::Registry ? DeleteRegistryItem(item) : DeleteShortcutItem(item);
}

bool btxh::DeleteSelectedItem(HWND owner)
{
    const int selected=GetSelectedItemIndex();
    if (selected==LB_ERR||selected<0||selected>=static_cast<int>(g_items.size()))
    {
        return false;
    }
    const auto item=g_items[selected];
    if (MessageBoxW(owner,LoadResString(IDS_CONFIRM_DELETE).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONQUESTION|MB_YESNO)!=IDYES)
    {
        return false;
    }
    if (!DeleteStartupItem(item))
    {
        ShowError(owner,GetLastError());
        return false;
    }

    ReloadStartupItems();
    RebuildStartupListBox();
    if (selected<static_cast<int>(g_items.size()))
    {
        SendMessageW(g_startupList,LB_SETCURSEL,selected,0);
    } else if (selected>0&&selected-1<static_cast<int>(g_items.size()))
    {
        SendMessageW(g_startupList,LB_SETCURSEL,selected-1,0);
    }
    UpdateDetailsPanel();
    return true;
}

void btxh::LaunchSelectedItem()
{
    const int selected=GetSelectedItemIndex();
    if (selected==LB_ERR||selected<0||selected>=static_cast<int>(g_items.size()))
    {
        return;
    }
    const auto& item=g_items[selected];
    if (item.type==ItemType::Registry)
    {
        LaunchRegistryCommand(item.command);
        return;
    }
    const HINSTANCE openResult=ShellExecuteW(nullptr,L"open",item.shortcutPath.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(openResult)<=32)
    {
        const std::wstring message=std::wstring(L"Sparkborne failed to launch shortcut: ")+item.shortcutPath+L"\n";
        OutputDebugStringW(message.c_str());
    }
}

void btxh::LaunchRegistryCommand(const std::wstring& rawCommand)
{
    std::wstring command=rawCommand;
    std::array<wchar_t,4096> expanded{};
    const DWORD expandedLen=ExpandEnvironmentStringsW(rawCommand.c_str(),expanded.data(),static_cast<DWORD>(expanded.size()));
    if (expandedLen>0&&expandedLen<expanded.size())
    {
        command.assign(expanded.data(),expandedLen-1);
    }

    STARTUPINFOW si{};
    si.cb=sizeof(si);
    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> mutableCommand(command.begin(),command.end());
    mutableCommand.push_back(L'\0');
    if (CreateProcessW(nullptr,mutableCommand.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi))
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else
    {
        const std::wstring message=std::wstring(L"Sparkborne failed to start command: ")+command+L"\n";
        OutputDebugStringW(message.c_str());
    }
}
