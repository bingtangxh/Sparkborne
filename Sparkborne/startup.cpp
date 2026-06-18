#include "Sparkborne.hpp"


void btxh::LaunchEnabledStartupItems()
{
    ReloadStartupItems();
    for (const auto& item:g_items)
    {
        if (!item.enabled)
        {
            continue;
        }

        if (item.type==ItemType::Registry)
        {
            LaunchRegistryCommand(item.command);
        } else
        {
            const HINSTANCE openResult=ShellExecuteW(nullptr,L"open",item.shortcutPath.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(openResult)<=32)
            {
                const std::wstring message=std::wstring(L"Sparkborne failed to launch shortcut: ")+item.shortcutPath+L"\n";
                OutputDebugStringW(message.c_str());
            }
        }
    }
}
