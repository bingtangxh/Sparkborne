#include "Sparkborne.hpp"

std::wstring btxh::LoadResString(UINT id)
{
    std::array<wchar_t,1024> buffer{};
    const int len=LoadStringW(g_instance,id,buffer.data(),static_cast<int>(buffer.size()));
    return len>0 ? std::wstring(buffer.data(),len) : std::wstring();
}

std::wstring btxh::FormatString(UINT formatId,const std::vector<std::wstring>& args)
{
    std::wstring value=LoadResString(formatId);
    for (const auto& arg:args)
    {
        const auto pos=value.find(L"%s");
        if (pos==std::wstring::npos){ break; }
        value.replace(pos,2,arg);
    }
    return value;
}
