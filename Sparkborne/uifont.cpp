#include "Sparkborne.hpp"

namespace btxh
{
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

void btxh::ApplyChineseUiFallback()
{
    if (PRIMARYLANGID(GetUserDefaultUILanguage())==LANG_CHINESE)
    {
        ULONG numLanguages=0;
        SetThreadPreferredUILanguages(MUI_LANGUAGE_NAME,L"zh-CN;en-US",&numLanguages);
    }
}