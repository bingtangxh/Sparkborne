#include "Sparkborne.hpp"

HINSTANCE btxh::g_instance=nullptr;
HWND btxh::g_mainWindow=nullptr;
HWND btxh::g_webWindow=nullptr;
HWND btxh::g_webBrowserHost=nullptr;
HWND btxh::g_startupList=nullptr;
HWND btxh::g_itemDetails=nullptr;
HWND btxh::g_enableButton=nullptr;
HWND btxh::g_disableButton=nullptr;
HWND btxh::g_deleteButton=nullptr;
HWND btxh::g_launchNowButton=nullptr;
std::vector<btxh::StartupItem> btxh::g_items;

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int nCmdShow)
{
    btxh::g_instance = instance;
    btxh::ApplyChineseUiFallback();
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

    constexpr DWORD kMainWindowStyle=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_VISIBLE;
    constexpr int kClientWidth=960;
    constexpr int kClientHeight=680;

    RECT windowRect{ 0,0,kClientWidth,kClientHeight };
    AdjustWindowRectEx(&windowRect,kMainWindowStyle,TRUE,0);

    btxh::g_mainWindow = CreateWindowExW(
        0,
        btxh::kMainClassName,
        btxh::LoadResString(IDS_APP_TITLE).c_str(),
        kMainWindowStyle  | WS_VISIBLE,
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

void btxh::BuildMenus()
{
    HMENU menuBar=CreateMenu();
    HMENU fileMenu=CreatePopupMenu();
    HMENU startupMenu=CreatePopupMenu();
    HMENU helpMenu=CreatePopupMenu();

    AppendMenuW(fileMenu,MF_STRING,IDM_FILE_EXIT,LoadResString(IDS_MENU_EXIT).c_str());
    AppendMenuW(startupMenu,MF_STRING,IDM_STARTUP_REFRESH,LoadResString(IDS_MENU_REFRESH).c_str());

    AppendMenuW(helpMenu,MF_STRING,IDM_HELP_MORE_WORKS,LoadResString(IDS_MENU_MORE_WORKS).c_str());

    AppendMenuW(menuBar,MF_POPUP,reinterpret_cast<UINT_PTR>(fileMenu),LoadResString(IDS_MENU_FILE).c_str());
    AppendMenuW(menuBar,MF_POPUP,reinterpret_cast<UINT_PTR>(startupMenu),LoadResString(IDS_MENU_STARTUP).c_str());
    AppendMenuW(menuBar,MF_POPUP,reinterpret_cast<UINT_PTR>(helpMenu),LoadResString(IDS_MENU_HELP).c_str());

    SetMenu(g_mainWindow,menuBar);
    DrawMenuBar(g_mainWindow);
}

void btxh::RebuildStartupListBox()
{
    if (!g_startupList)
    {
        return;
    }

    const int selected=GetSelectedItemIndex();
    SendMessageW(g_startupList,LB_RESETCONTENT,0,0);
    for (const auto& item:g_items)
    {
        std::wstring title=item.enabled ? LoadResString(IDS_ITEM_ENABLED) : LoadResString(IDS_ITEM_DISABLED);
        title+=L": ";
        title+=item.name;
        SendMessageW(g_startupList,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(title.c_str()));
    }
    if (selected>=0&&selected<static_cast<int>(g_items.size()))
    {
        SendMessageW(g_startupList,LB_SETCURSEL,selected,0);
    } else if (!g_items.empty())
    {
        SendMessageW(g_startupList,LB_SETCURSEL,0,0);
    }
}

int btxh::GetSelectedItemIndex()
{
    if (!g_startupList)
    {
        return LB_ERR;
    }
    return static_cast<int>(SendMessageW(g_startupList,LB_GETCURSEL,0,0));
}

void btxh::UpdateDetailsPanel()
{
    const int selected=GetSelectedItemIndex();
    if (selected==LB_ERR||selected<0||selected>=static_cast<int>(g_items.size()))
    {
        if (g_itemDetails)
        {
            SetWindowTextW(g_itemDetails,LoadResString(IDS_PANEL_NO_SELECTION).c_str());
        }
        if (g_enableButton){ EnableWindow(g_enableButton,FALSE); }
        if (g_disableButton){ EnableWindow(g_disableButton,FALSE); }
        if (g_deleteButton){ EnableWindow(g_deleteButton,FALSE); }
        if (g_launchNowButton){ EnableWindow(g_launchNowButton,FALSE); }
        return;
    }

    const auto& item=g_items[selected];
    const std::wstring typeText=item.type==ItemType::Registry ? LoadResString(IDS_ITEM_TYPE_REGISTRY) : LoadResString(IDS_ITEM_TYPE_SHORTCUT);
    const std::wstring stateText=item.enabled ? LoadResString(IDS_ITEM_ENABLED) : LoadResString(IDS_ITEM_DISABLED);
    const std::wstring details=FormatString(IDS_PANEL_ITEM_DETAILS_FMT,{ item.name, typeText, item.source, item.command, stateText });
    if (g_itemDetails)
    {
        SetWindowTextW(g_itemDetails,details.c_str());
    }
    if (g_enableButton){ EnableWindow(g_enableButton,!item.enabled); }
    if (g_disableButton){ EnableWindow(g_disableButton,item.enabled); }
    if (g_deleteButton){ EnableWindow(g_deleteButton,TRUE); }
    if (g_launchNowButton){ EnableWindow(g_launchNowButton,TRUE); }
}

void btxh::UpdateLayout(HWND hwnd)
{
    RECT rc{};
    GetClientRect(hwnd,&rc);
    const int width=rc.right-rc.left;
    const int height=rc.bottom-rc.top;
    const int margin=10;
    const int listWidth=(width*35)/100;
    const int left=margin;
    const int top=margin;
    const int usableHeight=height-margin*2;
    const int rightPanelLeft=left+listWidth+margin;
    const int rightPanelWidth=width-rightPanelLeft-margin;
    const int buttonHeight=30;
    const int buttonSpacing=8;
    const int buttonTop=height-margin-buttonHeight;
    const int buttonWidth=(rightPanelWidth-buttonSpacing*3)/4;
    const int detailsHeight=buttonTop-top-margin;

    if (g_startupList)
    {
        MoveWindow(g_startupList,left,top,listWidth,usableHeight,TRUE);
    }
    if (g_itemDetails)
    {
        MoveWindow(g_itemDetails,rightPanelLeft,top,rightPanelWidth,detailsHeight,TRUE);
    }
    if (g_enableButton)
    {
        MoveWindow(g_enableButton,rightPanelLeft,buttonTop,buttonWidth,buttonHeight,TRUE);
    }
    if (g_disableButton)
    {
        MoveWindow(g_disableButton,rightPanelLeft+buttonWidth+buttonSpacing,buttonTop,buttonWidth,buttonHeight,TRUE);
    }
    if (g_deleteButton)
    {
        MoveWindow(g_deleteButton,rightPanelLeft+(buttonWidth+buttonSpacing)*2,buttonTop,buttonWidth,buttonHeight,TRUE);
    }
    if (g_launchNowButton)
    {
        MoveWindow(g_launchNowButton,rightPanelLeft+(buttonWidth+buttonSpacing)*3,buttonTop,buttonWidth,buttonHeight,TRUE);
    }
}

bool btxh::ToggleSelectedItem(HWND owner,bool enable)
{
    const int selected=GetSelectedItemIndex();
    if (selected==LB_ERR||selected<0||selected>=static_cast<int>(g_items.size()))
    {
        return false;
    }

    const auto& item=g_items[selected];
    if (item.enabled==enable)
    {
        return true;
    }

    const bool ok=item.type==ItemType::Registry ? ToggleRegistryItem(item) : ToggleShortcutItem(item);
    if (!ok)
    {
        ShowError(owner,GetLastError());
        return false;
    }

    ReloadStartupItems();
    RebuildStartupListBox();
    if (selected>=0&&selected<static_cast<int>(g_items.size()))
    {
        SendMessageW(g_startupList,LB_SETCURSEL,selected,0);
    }
    UpdateDetailsPanel();
    return true;
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
    mainClass.lpfnWndProc=btxh::MainWndProc;
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

