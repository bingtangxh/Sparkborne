#include "Sparkborne.hpp"

namespace btxh
{
    HWND hWnd;
    HFONT g_uiFont=nullptr;
    bool g_ownsUiFont=false;

    void InitializeUiFont()
    {
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize=sizeof(metrics);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0))
        {
            g_uiFont=CreateFontIndirectW(&metrics.lfMessageFont);
            g_ownsUiFont=(g_uiFont!=nullptr);
        }

        if (!g_uiFont)
        {
            g_uiFont=static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            g_ownsUiFont=false;
        }
    }

    void ApplyUiFont(HWND hwnd)
    {
        if (hwnd&&g_uiFont)
        {
            SendMessageW(hwnd,WM_SETFONT,reinterpret_cast<WPARAM>(g_uiFont),TRUE);
        }
    }

    void CleanupUiFont()
    {
        if (g_ownsUiFont&&g_uiFont)
        {
            DeleteObject(g_uiFont);
        }
        g_uiFont=nullptr;
        g_ownsUiFont=false;
    }
}

LRESULT CALLBACK btxh::MainWndProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
    switch (msg)
    {
        case WM_CREATE:
        {
            InitializeUiFont();

            g_startupList=CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",nullptr,WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,0,0,0,0,hwnd,reinterpret_cast<HMENU>(IDC_STARTUP_LIST),g_instance,nullptr);
            g_itemDetails=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",nullptr,WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY|WS_VSCROLL,0,0,0,0,hwnd,reinterpret_cast<HMENU>(IDC_ITEM_DETAILS),g_instance,nullptr);
            g_enableButton=CreateWindowW(L"BUTTON",LoadResString(IDS_BUTTON_ENABLE).c_str(),WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,0,0,0,0,hwnd,reinterpret_cast<HMENU>(IDC_BTN_ENABLE),g_instance,nullptr);
            g_disableButton=CreateWindowW(L"BUTTON",LoadResString(IDS_BUTTON_DISABLE).c_str(),WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,0,0,0,0,hwnd,reinterpret_cast<HMENU>(IDC_BTN_DISABLE),g_instance,nullptr);
            g_deleteButton=CreateWindowW(L"BUTTON",LoadResString(IDS_BUTTON_DELETE).c_str(),WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,0,0,0,0,hwnd,reinterpret_cast<HMENU>(IDC_BTN_DELETE),g_instance,nullptr);
            g_launchNowButton=CreateWindowW(L"BUTTON",LoadResString(IDS_BUTTON_LAUNCH_NOW).c_str(),WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,0,0,0,0,hwnd,reinterpret_cast<HMENU>(IDC_BTN_LAUNCH_NOW),g_instance,nullptr);

            ApplyUiFont(g_startupList);
            ApplyUiFont(g_itemDetails);
            ApplyUiFont(g_enableButton);
            ApplyUiFont(g_disableButton);
            ApplyUiFont(g_deleteButton);
            ApplyUiFont(g_launchNowButton);

            RebuildStartupListBox();
            UpdateLayout(hwnd);
            UpdateDetailsPanel();
            return 0;
        }
        case WM_COMMAND:
#if 0
        {
            const int commandId=LOWORD(wParam);
            const int notifyCode=HIWORD(wParam);

            switch (commandId)
            {
                case IDM_FILE_EXIT:
                    if (MessageBoxW(hwnd,L"Do you really want to exit?",LoadResString(IDS_APP_TITLE).c_str(),MB_ICONQUESTION|MB_OKCANCEL)==IDOK)
                    {
                        DestroyWindow(hwnd);
                    }
                    return 0;
                case IDM_STARTUP_REFRESH:
                    ReloadStartupItems();
                    RebuildStartupListBox();
                    UpdateDetailsPanel();
                    return 0;
                case IDM_HELP_MORE_WORKS:
                    ShowWebWindow();
                    return 0;
                case IDC_STARTUP_LIST:
                    if (notifyCode==LBN_SELCHANGE)
                    {
                        UpdateDetailsPanel();
                        return 0;
                    }
                    break;
                case IDC_BTN_ENABLE:
                    if (notifyCode==BN_CLICKED)
                    {
                        ToggleSelectedItem(hwnd,true);
                        return 0;
                    }
                    break;
                case IDC_BTN_DISABLE:
                    if (notifyCode==BN_CLICKED)
                    {
                        ToggleSelectedItem(hwnd,false);
                        return 0;
                    }
                    break;
                case IDC_BTN_DELETE:
                    if (notifyCode==BN_CLICKED)
                    {
                        DeleteSelectedItem(hwnd);
                        return 0;
                    }
                    break;
                case IDC_BTN_LAUNCH_NOW:
                    if (notifyCode==BN_CLICKED)
                    {
                        LaunchSelectedItem();
                        return 0;
                    }
                    break;
                default:
                    break;
            }
            break;
        }
#else
        {
            const UINT id=LOWORD(wParam);
            const UINT code=HIWORD(wParam);
            if (id==IDM_FILE_EXIT)
            {
                PostQuitMessage(0);
                return 0;
            }
            if (id==IDM_STARTUP_REFRESH)
            {
                ReloadStartupItems();
                RebuildStartupListBox();
                UpdateDetailsPanel();
                return 0;
            }
            if (id==IDM_HELP_MORE_WORKS)
            {
                ShowWebWindow();
                return 0;
            }
            if (id==IDC_STARTUP_LIST&&code==LBN_SELCHANGE)
            {
                UpdateDetailsPanel();
                return 0;
            }
            if (id==IDC_BTN_ENABLE)
            {
                ToggleSelectedItem(hwnd,true);
                return 0;
            }
            if (id==IDC_BTN_DISABLE)
            {
                ToggleSelectedItem(hwnd,false);
                return 0;
            }
            if (id==IDC_BTN_DELETE)
            {
                DeleteSelectedItem(hwnd);
                return 0;
            }
            if (id==IDC_BTN_LAUNCH_NOW)
            {
                LaunchSelectedItem();
                return 0;
            }
            if (id==IDM_HELP_ABOUT)
            {
                MessageBoxW(hwnd,LoadResString(IDS_ABOUT).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONINFORMATION|MB_OK);
                return 0;
            }
            if (id==IDM_STARTUP_ADDMYSELF){
                btxh::hWnd=hwnd;
                std::wstring argv0(MAX_PATH, L'\0');
                GetModuleFileNameW(nullptr,argv0.data(),argv0.size());
                argv0.resize(wcslen(argv0.c_str()));
                AddtoSchduledTasks(L"Sparkborne AutoLaunch",argv0,L"-startup");
            }
            break;
        }
#endif

        case WM_SIZE:
            UpdateLayout(hwnd);
            return 0;
        case WM_DESTROY:
            CleanupUiFont();
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd,msg,wParam,lParam);
}

LRESULT CALLBACK btxh::WebWndProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
    switch (msg)
    {
        case WM_CREATE:
            if (!AtlAxWinInit())
            {
                ShowError(hwnd,GetLastError());
                return -1;
            }

            g_webBrowserHost=CreateWindowW(
                L"AtlAxWin140",
                kMoreWorksUrl,
                WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_HSCROLL,
                0,
                0,
                0,
                0,
                hwnd,
                nullptr,
                g_instance,
                nullptr);
            if (!g_webBrowserHost)
            {
                ShowError(hwnd,GetLastError());
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

void btxh::ApplyChineseUiFallback()
{
    if (PRIMARYLANGID(GetUserDefaultUILanguage())==LANG_CHINESE)
    {
        ULONG numLanguages=0;
        SetThreadPreferredUILanguages(MUI_LANGUAGE_NAME,L"zh-CN;en-US",&numLanguages);
    }
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
    std::wstring details=FormatString(IDS_PANEL_ITEM_DETAILS_FMT,{ item.name, typeText, item.source, item.command, stateText });
    
    // Highlight items from HKLM
    if (g_itemDetails)
    {
        if (item.source.starts_with(L"HKLM")){
            details+=L"\r\n\r\n";
            details+=LoadResString(IDS_TOGGLING_HKLM_NEEDS_PRIVILEGE);
        }
        
            SetWindowTextW(g_itemDetails,details.c_str());
        
    }
    
    if (g_enableButton){ EnableWindow(g_enableButton,!item.enabled); }
    if (g_disableButton){ EnableWindow(g_disableButton,item.enabled); }
    if (g_deleteButton){ EnableWindow(g_deleteButton,TRUE); }
    if (g_launchNowButton){ EnableWindow(g_launchNowButton,TRUE); }
}
