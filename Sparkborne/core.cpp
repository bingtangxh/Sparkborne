#include "Sparkborne.hpp"

bool btxh::ToggleRegistryItem(const StartupItem& item)
{
    const bool enable=!item.enabled;
    const std::wstring fromKey=enable ? kDisabledRunSubkey : kRunSubkey;
    const std::wstring toKey=enable ? kRunSubkey : kDisabledRunSubkey;

    HKEY sourceRead=nullptr;
    const LSTATUS openSourceStatus=RegOpenKeyExW(item.root,fromKey.c_str(),0,KEY_QUERY_VALUE,&sourceRead);
    if (openSourceStatus!=ERROR_SUCCESS)
    {
        SetLastError(static_cast<DWORD>(openSourceStatus));
        return false;
    }

    DWORD type=0;
    DWORD dataSize=0;
    const LSTATUS querySizeStatus=RegQueryValueExW(sourceRead,item.name.c_str(),nullptr,&type,nullptr,&dataSize);
    if (querySizeStatus!=ERROR_SUCCESS)
    {
        RegCloseKey(sourceRead);
        SetLastError(static_cast<DWORD>(querySizeStatus));
        return false;
    }

    std::vector<BYTE> data(dataSize);
    const LSTATUS queryDataStatus=RegQueryValueExW(sourceRead,item.name.c_str(),nullptr,&type,data.data(),&dataSize);
    if (queryDataStatus!=ERROR_SUCCESS)
    {
        RegCloseKey(sourceRead);
        SetLastError(static_cast<DWORD>(queryDataStatus));
        return false;
    }
    RegCloseKey(sourceRead);

    HKEY target=nullptr;
    DWORD disposition=0;
    const LSTATUS createStatus=RegCreateKeyExW(item.root,toKey.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&target,&disposition);
    if (createStatus!=ERROR_SUCCESS)
    {
        SetLastError(static_cast<DWORD>(createStatus));
        return false;
    }

    const auto setStatus=RegSetValueExW(target,item.name.c_str(),0,type,data.data(),dataSize);
    if (setStatus!=ERROR_SUCCESS)
    {
        RegCloseKey(target);
        SetLastError(static_cast<DWORD>(setStatus));
        return false;
    }

    HKEY sourceWrite=nullptr;
    const LSTATUS openWriteStatus=RegOpenKeyExW(item.root,fromKey.c_str(),0,KEY_SET_VALUE,&sourceWrite);
    if (openWriteStatus!=ERROR_SUCCESS)
    {
        RegCloseKey(target);
        SetLastError(static_cast<DWORD>(openWriteStatus));
        return false;
    }

    const auto deleteStatus=RegDeleteValueW(sourceWrite,item.name.c_str());
    RegCloseKey(sourceWrite);
    RegCloseKey(target);
    if (deleteStatus!=ERROR_SUCCESS)
    {
        SetLastError(static_cast<DWORD>(deleteStatus));
        return false;
    }
    return true;
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
    const LSTATUS openStatus=RegOpenKeyExW(item.root,item.keyPath.c_str(),0,KEY_SET_VALUE,&key);
    if (openStatus!=ERROR_SUCCESS)
    {
        SetLastError(static_cast<DWORD>(openStatus));
        return false;
    }
    const auto status=RegDeleteValueW(key,item.name.c_str());
    RegCloseKey(key);
    if (status!=ERROR_SUCCESS)
    {
        SetLastError(static_cast<DWORD>(status));
        return false;
    }
    return true;
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

bool btxh::IsAdmin()
{
    BOOL isAdmin=FALSE;
    PSID adminGroup=nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority=SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&adminGroup))
    {
        CheckTokenMembership(nullptr,adminGroup,&isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin==TRUE;
}

HRESULT btxh::AddtoSchduledTasks(const std::wstring& taskName,const std::wstring& executablePath,const std::wstring& arguments)
{
#if 0
    MessageBox(hWnd,L"Placeholder yet",LoadResString(IDS_APP_TITLE).c_str(),MB_ICONINFORMATION|MB_OK);
    // This function is currently unused, but it can be implemented in the future if needed.
    return E_NOTIMPL;
#else
    HRESULT hr=CoInitializeEx(NULL,COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        MessageBoxW(hWnd,FormatString(IDS_COM_INIT_FAILED,{std::to_wstring(hr)}).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONERROR|MB_OK);
        return 1;
    }

    //  Set general COM security levels.
    hr=CoInitializeSecurity(
        NULL,
        -1,
        NULL,
        NULL,
        RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL,
        0,
        NULL);

    if (FAILED(hr))
    {
        MessageBoxW(hWnd,FormatString(IDS_COM_SECURITY_FAILED,{ std::to_wstring(hr) }).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONERROR|MB_OK);

        CoUninitialize();
        return 1;
    }

    //  ------------------------------------------------------
    //  Create a name for the task.
    LPCWSTR wszTaskName=taskName.c_str();

    //  Get the windows directory and set the path to notepad.exe.
    std::wstring wstrExecutablePath=executablePath;


    //  ------------------------------------------------------
    //  Create an instance of the Task Service. 
    ITaskService *pService=NULL;
    hr=CoCreateInstance(CLSID_TaskScheduler,
                        NULL,
                        CLSCTX_INPROC_SERVER,
                        IID_ITaskService,
                        (void**) &pService);
    if (FAILED(hr))
    {
        printf("Failed to create an instance of ITaskService: %x",hr);
        CoUninitialize();
        return 1;
    }

    //  Connect to the task service.
    hr=pService->Connect(_variant_t(),_variant_t(),
                         _variant_t(),_variant_t());
    if (FAILED(hr))
    {
        printf("ITaskService::Connect failed: %x",hr);
        pService->Release();
        CoUninitialize();
        return 1;
    }

    //  ------------------------------------------------------
    //  Get or create target task folder.
    ITaskFolder *pRootFolder=NULL;
    hr=pService->GetFolder(_bstr_t(L"\\"),&pRootFolder);
    if (FAILED(hr))
    {
        MessageBoxW(hWnd,FormatString(IDS_GET_ROOT_FOLDER_FAILED,{ std::to_wstring(hr) }).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONERROR|MB_OK);
        pService->Release();
        CoUninitialize();
        return 1;
    }

    //  If the same task exists, remove it.
    pRootFolder->DeleteTask(_bstr_t(wszTaskName),0);

    //  Create the task builder object to create the task.
    ITaskDefinition *pTask=NULL;
    hr=pService->NewTask(0,&pTask);

    pService->Release();  // COM clean up.  Pointer is no longer used.
    if (FAILED(hr))
    {
        MessageBoxW(hWnd,FormatString(IDS_CREATE_TASK_FAILED,{ std::to_wstring(hr) }).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONERROR|MB_OK);
        pRootFolder->Release();
        CoUninitialize();
        return 1;
    }

    // Create \\BingtangXH if it does not exist.
    hr=pRootFolder->CreateFolder(_bstr_t(L"\\BingtangXH"),_variant_t(L""),nullptr);
    if (FAILED(hr) && hr!=HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS))
    {
        MessageBoxW(hWnd,FormatString(IDS_GET_ROOT_FOLDER_FAILED,{ std::to_wstring(hr) }).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONERROR|MB_OK);
        pRootFolder->Release();
        pService->Release();
        CoUninitialize();
        return 1;
    }

    ITaskFolder *pTargetFolder=NULL;
    hr=pService->GetFolder(_bstr_t(L"\\BingtangXH"),&pTargetFolder);
    if (FAILED(hr))
    {
        MessageBoxW(hWnd,FormatString(IDS_GET_ROOT_FOLDER_FAILED,{ std::to_wstring(hr) }).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONERROR|MB_OK);
        pRootFolder->Release();
        pService->Release();
        CoUninitialize();
        return 1;
    }
    pRootFolder->Release();

    //  ------------------------------------------------------
    //  Get the registration info for setting the identification.
    IRegistrationInfo *pRegInfo=NULL;
    hr=pTask->get_RegistrationInfo(&pRegInfo);
    if (FAILED(hr))
    {
        MessageBoxW(hWnd,FormatString(IDS_CANNOT_GET_IDENT_PTR,{ std::to_wstring(hr) }).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONERROR|MB_OK);
        pTargetFolder->Release();
        pTask->Release();
        CoUninitialize();
        return 1;
    }
    std::wstring author=L"Author Name";
    hr=pRegInfo->put_Author(_bstr_t(author.c_str()));
    pRegInfo->Release();
    if (FAILED(hr))
    {
        MessageBoxW(hWnd,FormatString(IDS_CANNOT_PUT_IDENT_PTR,{ std::to_wstring(hr) }).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONERROR|MB_OK);
        pTargetFolder->Release();
        pTask->Release();
        CoUninitialize();
        return 1;
    }

    //  ------------------------------------------------------
    //  Create the settings for the task
    ITaskSettings *pSettings=NULL;
    hr=pTask->get_Settings(&pSettings);
    if (FAILED(hr))
    {
        MessageBoxW(hWnd,FormatString(IDS_CANNOT_GET_SETTINGS_PTR,{ std::to_wstring(hr) }).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONERROR|MB_OK);
        pTargetFolder->Release();
        pTask->Release();
        CoUninitialize();
        return 1;
    }

    //  Set setting values for the task. 
    hr=pSettings->put_StartWhenAvailable(VARIANT_TRUE);
    if (SUCCEEDED(hr))
    {
        hr=pSettings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
    }
    pSettings->Release();
    if (FAILED(hr))
    {
        MessageBoxW(hWnd,FormatString(IDS_CANNOT_PUT_SETTINGS_PTR,{ std::to_wstring(hr) }).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONERROR|MB_OK);
        pTargetFolder->Release();
        pTask->Release();
        CoUninitialize();
        return 1;
    }

    //  ------------------------------------------------------
    //  Get the trigger collection to insert the logon trigger.
    ITriggerCollection *pTriggerCollection=NULL;
    hr=pTask->get_Triggers(&pTriggerCollection);
    if (FAILED(hr))
    {
        MessageBoxW(hWnd,FormatString(IDS_CANNOT_GET_TRIGGER_COLLECTION,{ std::to_wstring(hr) }).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONERROR|MB_OK);
        pTargetFolder->Release();
        pTask->Release();
        CoUninitialize();
        return 1;
    }

    //  Add the logon trigger to the task.
    ITrigger *pTrigger=NULL;
    hr=pTriggerCollection->Create(TASK_TRIGGER_LOGON,&pTrigger);
    pTriggerCollection->Release();
    if (FAILED(hr))
    {
        MessageBoxW(hWnd,FormatString(IDS_CANNOT_CREATE_TRIGGER,{ std::to_wstring(hr) }).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONERROR|MB_OK);
        pTargetFolder->Release();
        pTask->Release();
        CoUninitialize();
        return 1;
    }

    ILogonTrigger *pLogonTrigger=NULL;
    hr=pTrigger->QueryInterface(
        IID_ILogonTrigger,(void**) &pLogonTrigger);
    pTrigger->Release();
    if (FAILED(hr))
    {
        MessageBoxW(hWnd,FormatString(IDS_CANNOT_QUERY_TRIGGER,{ std::to_wstring(hr) }).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONERROR|MB_OK);
        pTargetFolder->Release();
        pTask->Release();
        CoUninitialize();
        return 1;
    }

    hr=pLogonTrigger->put_Id(_bstr_t(L"Trigger1"));
    if (FAILED(hr))
    MessageBoxW(hWnd,FormatString(IDS_TRIGGER_ID_SET_FAILED,{ std::to_wstring(hr) }).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONINFORMATION|MB_OK);

    //  Set the task to start at a certain time. The time 
    //  format should be YYYY-MM-DDTHH:MM:SS(+-)(timezone).
    //  For example, the start boundary below
    //  is January 1st 2005 at 12:05
    if (0){
        hr=pLogonTrigger->put_StartBoundary(_bstr_t(L"2005-01-01T12:05:00"));
        if (FAILED(hr))
            MessageBoxW(hWnd,FormatString(IDS_TRIGGER_START_SET_FAILED,{ std::to_wstring(hr) }).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONINFORMATION|MB_OK);

        hr=pLogonTrigger->put_EndBoundary(_bstr_t(L"2015-05-02T08:00:00"));
        if (FAILED(hr))
            MessageBoxW(hWnd,FormatString(IDS_TRIGGER_END_SET_FAILED,{ std::to_wstring(hr) }).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONINFORMATION|MB_OK);
    }

    //  Define the user.  The task will execute when the user logs on.
    std::wstring userName(256,L'\0');
    DWORD userNameSize=static_cast<DWORD>(userName.size());
    if (GetUserNameW(userName.data(),&userNameSize))
    {
        userName.resize(userNameSize-1);
        hr=pLogonTrigger->put_UserId(_bstr_t(userName.c_str()));
        if (FAILED(hr))
        {
            MessageBoxW(hWnd,FormatString(IDS_CANNOT_ADD_USER_ID,{ std::to_wstring(hr) }).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONERROR|MB_OK);
            pLogonTrigger->Release();
            pTargetFolder->Release();
            pTask->Release();
            CoUninitialize();
            return 1;
        }
    }
    pLogonTrigger->Release();

    //  ------------------------------------------------------
    //  Add an Action to the task. This task will execute executablePath.  
    IActionCollection *pActionCollection=NULL;

    //  Get the task action collection pointer.
    hr=pTask->get_Actions(&pActionCollection);
    if (FAILED(hr))
    {
        MessageBoxW(hWnd,FormatString(IDS_CANNOT_GET_ACTION_COLLECTION,{ std::to_wstring(hr) }).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONERROR|MB_OK);
        pTargetFolder->Release();
        pTask->Release();
        CoUninitialize();
        return 1;
    }

    //  Create the action, specifying that it is an executable action.
    IAction *pAction=NULL;
    hr=pActionCollection->Create(TASK_ACTION_EXEC,&pAction);
    pActionCollection->Release();
    if (FAILED(hr))
    {
        MessageBoxW(hWnd,FormatString(IDS_CANNOT_CREATE_ACTION,{ std::to_wstring(hr) }).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONERROR|MB_OK);

        pTargetFolder->Release();
        pTask->Release();
        CoUninitialize();
        return 1;
    }

    IExecAction *pExecAction=NULL;
    //  QI for the executable task pointer.
    hr=pAction->QueryInterface(
        IID_IExecAction,(void**) &pExecAction);
    pAction->Release();
    if (FAILED(hr))
    {
        MessageBoxW(hWnd,FormatString(IDS_CANNOT_QUERY_ACTION,{ std::to_wstring(hr) }).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONERROR|MB_OK);
        pTargetFolder->Release();
        pTask->Release();
        CoUninitialize();
        return 1;
    }

    //  Set the path of the executable.
    hr=pExecAction->put_Path(_bstr_t(wstrExecutablePath.c_str()));
    if (FAILED(hr))
    {
        MessageBoxW(hWnd,FormatString(IDS_CANNOT_SET_EXECUTABLE_PATH,{ std::to_wstring(hr) }).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONERROR|MB_OK);
        pExecAction->Release();
        pTargetFolder->Release();
        pTask->Release();
        CoUninitialize();
        return 1;
    }

    if (!arguments.empty())
    {
        hr=pExecAction->put_Arguments(_bstr_t(arguments.c_str()));
        if (FAILED(hr))
        {
            MessageBoxW(hWnd,FormatString(IDS_CANNOT_SET_EXECUTABLE_PATH,{ std::to_wstring(hr) }).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONERROR|MB_OK);
            pExecAction->Release();
            pTargetFolder->Release();
            pTask->Release();
            CoUninitialize();
            return 1;
        }
    }
    pExecAction->Release();

    //  ------------------------------------------------------
    //  Save the task in the root folder.
    IRegisteredTask *pRegisteredTask=NULL;

    hr=pTargetFolder->RegisterTaskDefinition(
        _bstr_t(wszTaskName),
        pTask,
        TASK_CREATE_OR_UPDATE,
        _variant_t(L"S-1-5-32-544"),
        _variant_t(),
        TASK_LOGON_GROUP,
        _variant_t(L""),
        &pRegisteredTask);
    if (FAILED(hr))
    {
        MessageBoxW(hWnd,FormatString(IDS_TASK_REGISTRATION_FAILED,{ std::to_wstring(hr) }).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONERROR|MB_OK);
        pTargetFolder->Release();
        pTask->Release();
        CoUninitialize();
        return 1;
    }

    printf("\n Success! Task successfully registered. ");
    MessageBoxW(hWnd,LoadResString(IDS_TASK_REGISTRATION_SUCCESS).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONINFORMATION|MB_OK);

    // Clean up
    pTargetFolder->Release();
    pTask->Release();
    pRegisteredTask->Release();
    CoUninitialize();
    return 0;
#endif
    //  ------------------------------------------------------
    //  Set principal run level to highest available.
    IPrincipal* pPrincipal=nullptr;
    hr=pTask->get_Principal(&pPrincipal);
    if (FAILED(hr))
    {
        MessageBoxW(hWnd,FormatString(IDS_CANNOT_GET_SETTINGS_PTR,{ std::to_wstring(hr) }).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONERROR|MB_OK);
        pTargetFolder->Release();
        pTask->Release();
        CoUninitialize();
        return 1;
    }

    hr=pPrincipal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
    pPrincipal->Release();
    if (FAILED(hr))
    {
        MessageBoxW(hWnd,FormatString(IDS_CANNOT_PUT_SETTINGS_PTR,{ std::to_wstring(hr) }).c_str(),LoadResString(IDS_APP_TITLE).c_str(),MB_ICONERROR|MB_OK);
        pTargetFolder->Release();
        pTask->Release();
        CoUninitialize();
        return 1;
    }
}