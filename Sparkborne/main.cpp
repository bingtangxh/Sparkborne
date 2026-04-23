#include <windows.h>
#include <atlbase.h>
#include <atlwin.h>
#include <atlhost.h>
#include <exdisp.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <string>
#include <vector>

#include "resource.h"

namespace
{
constexpr wchar_t kMainClassName[] = L"SparkborneMainWindow";
constexpr wchar_t kWebClassName[] = L"SparkborneWebWindow";
constexpr wchar_t kRunSubkey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kDisabledRunSubkey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\-Run";
constexpr wchar_t kMoreWorksUrl[] = L"https://www.bingtangxh.moe/works";

enum class ItemType
{
    Registry,
    Shortcut
};

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

HINSTANCE g_instance = nullptr;
HWND g_mainWindow = nullptr;
HWND g_webWindow = nullptr;
HWND g_webBrowserHost = nullptr;
std::vector<StartupItem> g_items;

std::wstring LoadResString(UINT id)
{
    std::array<wchar_t, 1024> buffer{};
    const int len = LoadStringW(g_instance, id, buffer.data(), static_cast<int>(buffer.size()));
    return len > 0 ? std::wstring(buffer.data(), len) : std::wstring();
}

std::wstring FormatString(UINT formatId, const std::vector<std::wstring>& args)
{
    std::wstring value = LoadResString(formatId);
    for (const auto& arg : args)
    {
        const auto pos = value.find(L"%s");
        if (pos == std::wstring::npos)
        {
            break;
        }
        value.replace(pos, 2, arg);
    }
    return value;
}

std::wstring GetLastErrorString(DWORD error)
{
    LPWSTR message = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD size = FormatMessageW(flags, nullptr, error, 0, reinterpret_cast<LPWSTR>(&message), 0, nullptr);
    std::wstring result = (size && message) ? std::wstring(message, size) : L"Unknown error";
    if (message)
    {
        LocalFree(message);
    }
    while (!result.empty() && (result.back() == L'\n' || result.back() == L'\r'))
    {
        result.pop_back();
    }
    return result;
}

void ShowError(HWND owner, DWORD error)
{
    const std::wstring message = FormatString(IDS_ACTION_FAILED_FMT, {GetLastErrorString(error)});
    MessageBoxW(owner, message.c_str(), LoadResString(IDS_ERROR_TITLE).c_str(), MB_ICONERROR | MB_OK);
}

std::wstring ToLower(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch)
                   { return static_cast<wchar_t>(towlower(ch)); });
    return text;
}

std::wstring GetKnownFolderPath(REFKNOWNFOLDERID folderId)
{
    PWSTR path = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &path)) && path)
    {
        result = path;
        CoTaskMemFree(path);
    }
    return result;
}

void AppendRegistryItems(HKEY root, const std::wstring& keyPath, bool enabled, UINT sourceStringId)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, keyPath.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
    {
        return;
    }

    DWORD valueCount = 0;
    DWORD maxNameLength = 0;
    DWORD maxDataLength = 0;
    if (RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &valueCount, &maxNameLength, &maxDataLength, nullptr, nullptr) != ERROR_SUCCESS)
    {
        RegCloseKey(key);
        return;
    }

    std::vector<wchar_t> nameBuffer(maxNameLength + 1);
    std::vector<BYTE> dataBuffer(maxDataLength + sizeof(wchar_t));

    for (DWORD index = 0; index < valueCount; ++index)
    {
        DWORD nameLength = maxNameLength + 1;
        DWORD dataLength = maxDataLength + sizeof(wchar_t);
        DWORD type = 0;

        const LSTATUS status = RegEnumValueW(key, index, nameBuffer.data(), &nameLength, nullptr, &type, dataBuffer.data(), &dataLength);
        if (status != ERROR_SUCCESS)
        {
            continue;
        }

        if (type != REG_SZ && type != REG_EXPAND_SZ)
        {
            continue;
        }

        std::wstring command(reinterpret_cast<wchar_t*>(dataBuffer.data()), dataLength / sizeof(wchar_t));
        const auto nullPos = command.find(L'\0');
        if (nullPos != std::wstring::npos)
        {
            command.resize(nullPos);
        }

        StartupItem item;
        item.type = ItemType::Registry;
        item.enabled = enabled;
        item.root = root;
        item.keyPath = keyPath;
        item.name.assign(nameBuffer.data(), nameLength);
        item.command = command;
        item.source = LoadResString(sourceStringId);
        g_items.push_back(std::move(item));
    }

    RegCloseKey(key);
}

void AppendShortcutItems(const std::wstring& folder)
{
    if (folder.empty())
    {
        return;
    }

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec))
    {
        if (ec || !entry.is_regular_file())
        {
            continue;
        }

        const std::wstring ext = ToLower(entry.path().extension().wstring());
        if (ext != L".lnk" && ext != L".dis")
        {
            continue;
        }

        StartupItem item;
        item.type = ItemType::Shortcut;
        item.enabled = (ext == L".lnk");
        item.name = entry.path().stem().wstring();
        item.command = entry.path().wstring();
        item.shortcutPath = entry.path().wstring();
        item.source = L"shell:startup";
        g_items.push_back(std::move(item));
    }
}

void ReloadStartupItems()
{
    g_items.clear();
    AppendRegistryItems(HKEY_CURRENT_USER, kRunSubkey, true, IDS_REGISTRY_HKCU_RUN);
    AppendRegistryItems(HKEY_CURRENT_USER, kDisabledRunSubkey, false, IDS_REGISTRY_HKCU_DISABLED);
    AppendRegistryItems(HKEY_LOCAL_MACHINE, kRunSubkey, true, IDS_REGISTRY_HKLM_RUN);
    AppendRegistryItems(HKEY_LOCAL_MACHINE, kDisabledRunSubkey, false, IDS_REGISTRY_HKLM_DISABLED);
    AppendShortcutItems(GetKnownFolderPath(FOLDERID_Startup));
}

bool ToggleRegistryItem(const StartupItem& item)
{
    const bool enable = !item.enabled;
    const std::wstring fromKey = enable ? kDisabledRunSubkey : kRunSubkey;
    const std::wstring toKey = enable ? kRunSubkey : kDisabledRunSubkey;

    HKEY source = nullptr;
    if (RegOpenKeyExW(item.root, fromKey.c_str(), 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &source) != ERROR_SUCCESS)
    {
        return false;
    }

    DWORD type = 0;
    DWORD dataSize = 0;
    if (RegQueryValueExW(source, item.name.c_str(), nullptr, &type, nullptr, &dataSize) != ERROR_SUCCESS)
    {
        RegCloseKey(source);
        return false;
    }

    std::vector<BYTE> data(dataSize);
    if (RegQueryValueExW(source, item.name.c_str(), nullptr, &type, data.data(), &dataSize) != ERROR_SUCCESS)
    {
        RegCloseKey(source);
        return false;
    }

    HKEY target = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(item.root, toKey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &target, &disposition) != ERROR_SUCCESS)
    {
        RegCloseKey(source);
        return false;
    }

    const auto setStatus = RegSetValueExW(target, item.name.c_str(), 0, type, data.data(), dataSize);
    if (setStatus != ERROR_SUCCESS)
    {
        RegCloseKey(target);
        RegCloseKey(source);
        return false;
    }

    const auto deleteStatus = RegDeleteValueW(source, item.name.c_str());
    RegCloseKey(target);
    RegCloseKey(source);
    return deleteStatus == ERROR_SUCCESS;
}

bool ToggleShortcutItem(const StartupItem& item)
{
    const std::filesystem::path sourcePath(item.shortcutPath);
    std::filesystem::path targetPath = sourcePath;
    targetPath.replace_extension(item.enabled ? L".dis" : L".lnk");
    std::error_code ec;
    std::filesystem::rename(sourcePath, targetPath, ec);
    return !ec;
}

void BuildMenus()
{
    HMENU menuBar = CreateMenu();
    HMENU fileMenu = CreatePopupMenu();
    HMENU startupMenu = CreatePopupMenu();
    HMENU helpMenu = CreatePopupMenu();

    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_EXIT, LoadResString(IDS_MENU_EXIT).c_str());
    AppendMenuW(startupMenu, MF_STRING, IDM_STARTUP_REFRESH, LoadResString(IDS_MENU_REFRESH).c_str());
    AppendMenuW(startupMenu, MF_SEPARATOR, 0, nullptr);

    for (size_t i = 0; i < g_items.size(); ++i)
    {
        const auto& item = g_items[i];
        std::wstring title = item.enabled ? LoadResString(IDS_ITEM_ENABLED) : LoadResString(IDS_ITEM_DISABLED);
        title += L": ";
        title += item.name;
        AppendMenuW(startupMenu, MF_STRING, IDM_STARTUP_ITEM_BASE + static_cast<UINT>(i), title.c_str());
    }

    AppendMenuW(helpMenu, MF_STRING, IDM_HELP_MORE_WORKS, LoadResString(IDS_MENU_MORE_WORKS).c_str());

    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), LoadResString(IDS_MENU_FILE).c_str());
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(startupMenu), LoadResString(IDS_MENU_STARTUP).c_str());
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(helpMenu), LoadResString(IDS_MENU_HELP).c_str());

    SetMenu(g_mainWindow, menuBar);
    DrawMenuBar(g_mainWindow);
}

void ShowItemDetails(HWND owner, const StartupItem& item)
{
    const std::wstring typeText = item.type == ItemType::Registry ? LoadResString(IDS_ITEM_TYPE_REGISTRY) : LoadResString(IDS_ITEM_TYPE_SHORTCUT);
    const std::wstring stateText = item.enabled ? LoadResString(IDS_ITEM_ENABLED) : LoadResString(IDS_ITEM_DISABLED);
    const std::wstring prompt = item.enabled ? LoadResString(IDS_DISABLE_ACTION) : LoadResString(IDS_ENABLE_ACTION);
    const std::wstring details = FormatString(IDS_ITEM_DETAILS_FMT, {item.name, typeText, item.source, item.command, stateText, prompt});

    if (MessageBoxW(owner, details.c_str(), LoadResString(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION | MB_YESNO) != IDYES)
    {
        return;
    }

    const bool ok = item.type == ItemType::Registry ? ToggleRegistryItem(item) : ToggleShortcutItem(item);
    if (!ok)
    {
        ShowError(owner, GetLastError());
    }

    ReloadStartupItems();
    BuildMenus();
}

void LaunchRegistryCommand(const std::wstring& rawCommand)
{
    std::wstring command = rawCommand;
    std::array<wchar_t, 4096> expanded{};
    const DWORD expandedLen = ExpandEnvironmentStringsW(rawCommand.c_str(), expanded.data(), static_cast<DWORD>(expanded.size()));
    if (expandedLen > 0 && expandedLen < expanded.size())
    {
        command.assign(expanded.data(), expandedLen - 1);
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    if (CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

void LaunchEnabledStartupItems()
{
    ReloadStartupItems();
    for (const auto& item : g_items)
    {
        if (!item.enabled)
        {
            continue;
        }

        if (item.type == ItemType::Registry)
        {
            LaunchRegistryCommand(item.command);
        }
        else
        {
            ShellExecuteW(nullptr, L"open", item.shortcutPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    }
}

void ShowWebWindow()
{
    if (g_webWindow && IsWindow(g_webWindow))
    {
        ShowWindow(g_webWindow, SW_SHOWNORMAL);
        SetForegroundWindow(g_webWindow);
        return;
    }

    g_webWindow = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kWebClassName,
        LoadResString(IDS_WEB_WINDOW_TITLE).c_str(),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1024,
        768,
        g_mainWindow,
        nullptr,
        g_instance,
        nullptr);
}

LRESULT CALLBACK WebWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        g_webBrowserHost = CreateWindowW(L"AtlAxWin", kMoreWorksUrl, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, nullptr, g_instance, nullptr);
        return 0;
    case WM_SIZE:
        if (g_webBrowserHost)
        {
            MoveWindow(g_webBrowserHost, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        }
        return 0;
    case WM_DESTROY:
        g_webBrowserHost = nullptr;
        g_webWindow = nullptr;
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
    {
        const UINT id = LOWORD(wParam);
        if (id == IDM_FILE_EXIT)
        {
            PostQuitMessage(0);
            return 0;
        }
        if (id == IDM_STARTUP_REFRESH)
        {
            ReloadStartupItems();
            BuildMenus();
            return 0;
        }
        if (id == IDM_HELP_MORE_WORKS)
        {
            ShowWebWindow();
            return 0;
        }
        if (id >= IDM_STARTUP_ITEM_BASE && id < IDM_STARTUP_ITEM_BASE + g_items.size())
        {
            ShowItemDetails(hwnd, g_items[id - IDM_STARTUP_ITEM_BASE]);
            return 0;
        }
        break;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rect{};
        GetClientRect(hwnd, &rect);
        FillRect(hdc, &rect, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        DrawTextW(hdc, LoadResString(IDS_WINDOW_HINT).c_str(), -1, &rect, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool IsStartupMode()
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
    {
        return false;
    }

    bool startupMode = false;
    for (int i = 1; i < argc; ++i)
    {
        std::wstring arg = ToLower(argv[i]);
        if (arg == L"-startup" || arg == L"/startup")
        {
            startupMode = true;
            break;
        }
    }

    LocalFree(argv);
    return startupMode;
}

bool RegisterWindowClasses()
{
    WNDCLASSEXW mainClass{};
    mainClass.cbSize = sizeof(mainClass);
    mainClass.hInstance = g_instance;
    mainClass.lpfnWndProc = MainWndProc;
    mainClass.lpszClassName = kMainClassName;
    mainClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    mainClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassExW(&mainClass))
    {
        return false;
    }

    WNDCLASSEXW webClass = mainClass;
    webClass.lpfnWndProc = WebWndProc;
    webClass.lpszClassName = kWebClassName;
    return RegisterClassExW(&webClass) != 0;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int nCmdShow)
{
    g_instance = instance;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    AtlAxWinInit();

    if (IsStartupMode())
    {
        LaunchEnabledStartupItems();
        CoUninitialize();
        return 0;
    }

    if (!RegisterWindowClasses())
    {
        CoUninitialize();
        return 1;
    }

    ReloadStartupItems();

    g_mainWindow = CreateWindowExW(
        0,
        kMainClassName,
        LoadResString(IDS_APP_TITLE).c_str(),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        900,
        600,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!g_mainWindow)
    {
        CoUninitialize();
        return 1;
    }

    BuildMenus();
    ShowWindow(g_mainWindow, nCmdShow);
    UpdateWindow(g_mainWindow);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
