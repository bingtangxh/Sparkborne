#pragma once
#ifndef SPARKBORNE_HPP
#define SPARKBORNE_HPP

#define _WIN32_DCOM

#include <comdef.h>
#include <taskschd.h>
#include <windows.h>
#include <atlbase.h>
#include <atlwin.h>
#include <atlhost.h>
#include <exdisp.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <string>
#include <vector>
#include "resource.h"
#include "framework.h"

namespace btxh
{
    constexpr wchar_t kMainClassName[]=L"SparkborneMainWindow";
    constexpr wchar_t kWebClassName[]=L"SparkborneWebWindow";
    constexpr wchar_t kRunSubkey[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr wchar_t kDisabledRunSubkey[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\-Run";
    constexpr wchar_t kMoreWorksUrl[]=L"https://www.bingtangxh.moe/works";

    enum class ItemType { Registry,Shortcut };

    struct StartupItem
    {
        ItemType type{};
        bool enabled{};
        HKEY root{};
        std::wstring keyPath;
        std::wstring name;
        std::wstring command;
        std::wstring source;
        std::wstring shortcutPath;
    };

    class SparkborneAtlModule: public ATL::CAtlExeModuleT<SparkborneAtlModule>
    {
    };

    extern HWND hWnd;
    extern HINSTANCE g_instance;
    extern HWND g_mainWindow;
    extern HWND g_webWindow;
    extern HWND g_webBrowserHost;
    extern HWND g_startupList;
    extern HWND g_itemDetails;
    extern HWND g_enableButton;
    extern HWND g_disableButton;
    extern HWND g_deleteButton;
    extern HWND g_launchNowButton;
    extern std::vector<StartupItem> g_items;

    std::wstring LoadResString(UINT id);
    std::wstring FormatString(UINT formatId,const std::vector<std::wstring>& args);
    std::wstring GetLastErrorString(DWORD error);
    void ShowError(HWND owner,DWORD error);
    std::wstring ToLower(std::wstring text);
    std::wstring GetKnownFolderPath(REFKNOWNFOLDERID folderId);
    void AppendRegistryItems(HKEY root,const std::wstring& keyPath,bool enabled,UINT sourceStringId);
    void AppendShortcutItems(const std::wstring& folder);
    void ReloadStartupItems();
    bool ToggleRegistryItem(const StartupItem& item);
    bool ToggleShortcutItem(const StartupItem& item);
    bool DeleteRegistryItem(const StartupItem& item);
    bool DeleteShortcutItem(const StartupItem& item);
    bool DeleteStartupItem(const StartupItem& item);
    void BuildMenus();
    void RebuildStartupListBox();
    void UpdateDetailsPanel();
    void UpdateLayout(HWND hwnd);
    int GetSelectedItemIndex();
    bool ToggleSelectedItem(HWND owner,bool enable);
    bool DeleteSelectedItem(HWND owner);
    void LaunchSelectedItem();
    void LaunchRegistryCommand(const std::wstring& rawCommand);
    void LaunchEnabledStartupItems();
    void ShowWebWindow();
    LRESULT CALLBACK WebWndProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam);
    LRESULT CALLBACK MainWndProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam);
    bool IsStartupMode();
    bool RegisterWindowClasses();
    void ApplyChineseUiFallback();
    ATOM MyRegisterClass(HINSTANCE hInstance);
    INT_PTR CALLBACK About(HWND hDlg,UINT message,WPARAM wParam,LPARAM lParam);
    HRESULT AddtoSchduledTasks(const std::wstring& taskName,const std::wstring& executablePath,const std::wstring& arguments,const std::wstring& path,const std::wstring& author,const std::wstring& description);
    bool IsAdmin();

} // namespace


#endif // !SPARKBORNE_HPP
