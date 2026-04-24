#include "Sparkborne.hpp"

HINSTANCE btxh::g_instance=nullptr;
HWND btxh::g_mainWindow=nullptr;
HWND btxh::g_webWindow=nullptr;
HWND btxh::g_webBrowserHost=nullptr;
std::vector<btxh::StartupItem> btxh::g_items;

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int nCmdShow)
{
    btxh::g_instance = instance;
    const HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initResult))
    {
        MessageBoxW(nullptr,btxh::LoadResString(IDS_COM_INIT_FAILED).c_str(),btxh::LoadResString(IDS_ERROR_TITLE).c_str(), MB_ICONERROR | MB_OK);
        return 1;
    }

    if (btxh::IsStartupMode())
    {
        btxh::LaunchEnabledStartupItems();
        CoUninitialize();
        return 0;
    }

    if (!AtlAxWinInit())
    {
        MessageBoxW(nullptr,btxh::LoadResString(IDS_ATL_INIT_FAILED).c_str(),btxh::LoadResString(IDS_ERROR_TITLE).c_str(), MB_ICONERROR | MB_OK);
        CoUninitialize();
        return 1;
    }

    if (!btxh::RegisterWindowClasses())
    {
        CoUninitialize();
        return 1;
    }

    btxh::ReloadStartupItems();

    btxh::g_mainWindow = CreateWindowExW(
        0,
        btxh::kMainClassName,
        btxh::LoadResString(IDS_APP_TITLE).c_str(),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        900,
        600,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!btxh::g_mainWindow)
    {
        CoUninitialize();
        return 1;
    }

    btxh::BuildMenus();
    ShowWindow(btxh::g_mainWindow, nCmdShow);
    UpdateWindow(btxh::g_mainWindow);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return static_cast<int>(msg.wParam);
}

std::wstring btxh::GetLastErrorString(DWORD error)
{
    LPWSTR message=nullptr;
    const DWORD flags=FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD size=FormatMessageW(flags,nullptr,error,0,reinterpret_cast<LPWSTR>(&message),0,nullptr);
    std::wstring result=(size&&message) ? std::wstring(message,size) : L"Unknown error";
    if (message)
    {
        LocalFree(message);
    }
    while (!result.empty()&&(result.back()==L'\n'||result.back()==L'\r'))
    {
        result.pop_back();
    }
    return result;
}

void btxh::ShowError(HWND owner,DWORD error)
{
    const std::wstring message=FormatString(IDS_ACTION_FAILED_FMT,{ GetLastErrorString(error) });
    MessageBoxW(owner,message.c_str(),LoadResString(IDS_ERROR_TITLE).c_str(),MB_ICONERROR|MB_OK);
}

std::wstring btxh::ToLower(std::wstring text)
{
    std::transform(text.begin(),text.end(),text.begin(),[](wchar_t ch)
                   { return static_cast<wchar_t>(towlower(ch)); });
    return text;
}

std::wstring btxh::GetKnownFolderPath(REFKNOWNFOLDERID folderId)
{
    PWSTR path=nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(folderId,KF_FLAG_DEFAULT,nullptr,&path))&&path)
    {
        result=path;
        CoTaskMemFree(path);
    }
    return result;
}

void btxh::AppendRegistryItems(HKEY root,const std::wstring& keyPath,bool enabled,UINT sourceStringId)
{
    HKEY key=nullptr;
    if (RegOpenKeyExW(root,keyPath.c_str(),0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS)
    {
        return;
    }

    DWORD valueCount=0;
    DWORD maxNameLength=0;
    DWORD maxDataLength=0;
    if (RegQueryInfoKeyW(key,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,&valueCount,&maxNameLength,&maxDataLength,nullptr,nullptr)!=ERROR_SUCCESS)
    {
        RegCloseKey(key);
        return;
    }

    std::vector<wchar_t> nameBuffer(maxNameLength+1);
    std::vector<BYTE> dataBuffer(maxDataLength+sizeof(wchar_t));

    for (DWORD index=0; index<valueCount; ++index)
    {
        DWORD nameLength=maxNameLength+1;
        DWORD dataLength=maxDataLength+sizeof(wchar_t);
        DWORD type=0;

        const LSTATUS status=RegEnumValueW(key,index,nameBuffer.data(),&nameLength,nullptr,&type,dataBuffer.data(),&dataLength);
        if (status!=ERROR_SUCCESS)
        {
            continue;
        }

        if (type!=REG_SZ&&type!=REG_EXPAND_SZ)
        {
            continue;
        }

        if (dataLength<sizeof(wchar_t)||(dataLength%sizeof(wchar_t))!=0)
        {
            continue;
        }

        const size_t wcharCount=dataLength/sizeof(wchar_t);
        auto* chars=reinterpret_cast<wchar_t*>(dataBuffer.data());
        if (chars[wcharCount-1]!=L'\0')
        {
            if (dataLength+sizeof(wchar_t)>dataBuffer.size())
            {
                dataBuffer.resize(dataLength+sizeof(wchar_t));
                chars=reinterpret_cast<wchar_t*>(dataBuffer.data());
            }
            chars[wcharCount]=L'\0';
            dataLength+=sizeof(wchar_t);
        }

        std::wstring command(chars,dataLength/sizeof(wchar_t));
        const auto nullPos=command.find(L'\0');
        if (nullPos!=std::wstring::npos)
        {
            command.resize(nullPos);
        }

        StartupItem item;
        item.type=ItemType::Registry;
        item.enabled=enabled;
        item.root=root;
        item.keyPath=keyPath;
        item.name.assign(nameBuffer.data(),nameLength);
        item.command=command;
        item.source=LoadResString(sourceStringId);
        g_items.push_back(std::move(item));
    }

    RegCloseKey(key);
}

void btxh::AppendShortcutItems(const std::wstring& folder)
{
    if (folder.empty())
    {
        return;
    }

    std::error_code ec;
    for (const auto& entry:std::filesystem::directory_iterator(folder,ec))
    {
        if (ec||!entry.is_regular_file())
        {
            continue;
        }

        const std::wstring ext=ToLower(entry.path().extension().wstring());
        if (ext!=L".lnk"&&ext!=L".dis")
        {
            continue;
        }

        StartupItem item;
        item.type=ItemType::Shortcut;
        item.enabled=(ext==L".lnk");
        item.name=entry.path().stem().wstring();
        item.command=entry.path().wstring();
        item.shortcutPath=entry.path().wstring();
        item.source=L"shell:startup";
        g_items.push_back(std::move(item));
    }
}

void btxh::ReloadStartupItems()
{
    g_items.clear();
    AppendRegistryItems(HKEY_CURRENT_USER,kRunSubkey,true,IDS_REGISTRY_HKCU_RUN);
    AppendRegistryItems(HKEY_CURRENT_USER,kDisabledRunSubkey,false,IDS_REGISTRY_HKCU_DISABLED);
    AppendRegistryItems(HKEY_LOCAL_MACHINE,kRunSubkey,true,IDS_REGISTRY_HKLM_RUN);
    AppendRegistryItems(HKEY_LOCAL_MACHINE,kDisabledRunSubkey,false,IDS_REGISTRY_HKLM_DISABLED);
    AppendShortcutItems(GetKnownFolderPath(FOLDERID_Startup));
}

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

void btxh::BuildMenus()
{
    HMENU menuBar=CreateMenu();
    HMENU fileMenu=CreatePopupMenu();
    HMENU startupMenu=CreatePopupMenu();
    HMENU helpMenu=CreatePopupMenu();

    AppendMenuW(fileMenu,MF_STRING,IDM_FILE_EXIT,LoadResString(IDS_MENU_EXIT).c_str());
    AppendMenuW(startupMenu,MF_STRING,IDM_STARTUP_REFRESH,LoadResString(IDS_MENU_REFRESH).c_str());
    AppendMenuW(startupMenu,MF_SEPARATOR,0,nullptr);

    const size_t maxItems=static_cast<size_t>(UINT_MAX)-static_cast<size_t>(IDM_STARTUP_ITEM_BASE)+1;
    const size_t itemsToShow=(std::min) (g_items.size(),maxItems);
    for (size_t i=0; i<itemsToShow; ++i)
    {
        const auto& item=g_items[i];
        std::wstring title=item.enabled ? LoadResString(IDS_ITEM_ENABLED) : LoadResString(IDS_ITEM_DISABLED);
        title+=L": ";
        title+=item.name;
        AppendMenuW(startupMenu,MF_STRING,IDM_STARTUP_ITEM_BASE+static_cast<UINT>(i),title.c_str());
    }

    AppendMenuW(helpMenu,MF_STRING,IDM_HELP_MORE_WORKS,LoadResString(IDS_MENU_MORE_WORKS).c_str());

    AppendMenuW(menuBar,MF_POPUP,reinterpret_cast<UINT_PTR>(fileMenu),LoadResString(IDS_MENU_FILE).c_str());
    AppendMenuW(menuBar,MF_POPUP,reinterpret_cast<UINT_PTR>(startupMenu),LoadResString(IDS_MENU_STARTUP).c_str());
    AppendMenuW(menuBar,MF_POPUP,reinterpret_cast<UINT_PTR>(helpMenu),LoadResString(IDS_MENU_HELP).c_str());

    SetMenu(g_mainWindow,menuBar);
    DrawMenuBar(g_mainWindow);
}

void btxh::ShowItemDetails(HWND owner,const StartupItem& item)
{
    const std::wstring typeText=item.type==ItemType::Registry ? LoadResString(IDS_ITEM_TYPE_REGISTRY) : LoadResString(IDS_ITEM_TYPE_SHORTCUT);
    const std::wstring stateText=item.enabled ? LoadResString(IDS_ITEM_ENABLED) : LoadResString(IDS_ITEM_DISABLED);
    const std::wstring prompt=item.enabled ? LoadResString(IDS_DISABLE_ACTION) : LoadResString(IDS_ENABLE_ACTION);
    const std::wstring details=FormatString(IDS_ITEM_DETAILS_FMT,{ item.name, typeText, item.source, item.command, stateText, prompt });

    if (MessageBoxW(owner,details.c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONINFORMATION|MB_YESNO)!=IDYES)
    {
        return;
    }

    const bool ok=item.type==ItemType::Registry ? ToggleRegistryItem(item) : ToggleShortcutItem(item);
    if (!ok)
    {
        ShowError(owner,GetLastError());
    }

    ReloadStartupItems();
    BuildMenus();
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

void btxh::ShowWebWindow()
{    
    if (g_webWindow&&IsWindow(g_webWindow))
    {
        ShowWindow(g_webWindow,SW_SHOWNORMAL);
        SetForegroundWindow(g_webWindow);
        return;
    }
    g_webWindow=CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kWebClassName,
        LoadResString(IDS_WEB_WINDOW_TITLE).c_str(),
        WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1024,
        768,
        g_mainWindow,
        nullptr,
        g_instance,
        nullptr);
}

LRESULT CALLBACK btxh::WebWndProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
    switch (msg)
    {
        case WM_CREATE:
            g_webBrowserHost=CreateWindowW(L"AtlAxWin",kMoreWorksUrl,WS_CHILD|WS_VISIBLE,0,0,0,0,hwnd,nullptr,g_instance,nullptr);
            if (!g_webBrowserHost)
            {
                return -1;
            }
            return 0;
        case WM_SIZE:
            if (g_webBrowserHost)
            {
                MoveWindow(g_webBrowserHost,0,0,LOWORD(lParam),HIWORD(lParam),TRUE);
            }
            return 0;
        case WM_DESTROY:
            g_webBrowserHost=nullptr;
            g_webWindow=nullptr;
            return 0;
        default:
            return DefWindowProcW(hwnd,msg,wParam,lParam);
    }
}

LRESULT CALLBACK btxh::MainWndProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
    switch (msg)
    {
        case WM_COMMAND:
        {
            const UINT id=LOWORD(wParam);
            if (id==IDM_FILE_EXIT)
            {
                PostQuitMessage(0);
                return 0;
            }
            if (id==IDM_STARTUP_REFRESH)
            {
                ReloadStartupItems();
                BuildMenus();
                return 0;
            }
            if (id==IDM_HELP_MORE_WORKS)
            {
                ShowWebWindow();
                return 0;
            }
            if (id>=IDM_STARTUP_ITEM_BASE)
            {
                const size_t index=static_cast<size_t>(id-IDM_STARTUP_ITEM_BASE);
                if (index<g_items.size())
                {
                    ShowItemDetails(hwnd,g_items[index]);
                    return 0;
                }
            }
            break;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC hdc=BeginPaint(hwnd,&ps);
            RECT rect{};
            GetClientRect(hwnd,&rect);
            FillRect(hdc,&rect,reinterpret_cast<HBRUSH>(COLOR_WINDOW+1));
            DrawTextW(hdc,LoadResString(IDS_WINDOW_HINT).c_str(),-1,&rect,DT_CENTER|DT_VCENTER|DT_WORDBREAK);
            EndPaint(hwnd,&ps);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd,msg,wParam,lParam);
}

bool btxh::IsStartupMode()
{
    int argc=0;
    LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if (!argv)
    {
        return false;
    }

    bool startupMode=false;
    for (int i=1; i<argc; ++i)
    {
        std::wstring arg=ToLower(argv[i]);
        if (arg==L"-startup"||arg==L"/startup")
        {
            startupMode=true;
            break;
        }
    }

    LocalFree(argv);
    return startupMode;
}

bool btxh::RegisterWindowClasses()
{
    WNDCLASSEXW mainClass{};
    mainClass.cbSize=sizeof(mainClass);
    mainClass.hInstance=g_instance;
    mainClass.lpfnWndProc=MainWndProc;
    mainClass.lpszClassName=kMainClassName;
    mainClass.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    mainClass.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);

    if (!RegisterClassExW(&mainClass))
    {
        return false;
    }

    WNDCLASSEXW webClass=mainClass;
    webClass.lpfnWndProc=WebWndProc;
    webClass.lpszClassName=kWebClassName;
    return RegisterClassExW(&webClass)!=0;
}