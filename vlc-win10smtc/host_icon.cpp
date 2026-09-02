#include "host_icon.h"

#include <algorithm>
#include <iterator>

#include <windows.h>

namespace {

std::vector<std::byte> ReadLargestImage(HMODULE module, LPCWSTR group_name)
{
    HRSRC group = FindResourceW(module, group_name, RT_GROUP_ICON);
    if (!group)
        return {};

    auto* directory = static_cast<PBYTE>(LockResource(LoadResource(module, group)));
    if (!directory)
        return {};

    const int id = LookupIconIdFromDirectoryEx(directory, TRUE, 1024, 1024, LR_DEFAULTCOLOR);
    HRSRC image = FindResourceW(module, MAKEINTRESOURCEW(id), RT_ICON);
    if (!image)
        return {};

    const auto* bytes = static_cast<const std::byte*>(LockResource(LoadResource(module, image)));
    const DWORD size = SizeofResource(module, image);

    // Only PNG entries are decodable as-is; the others are headerless bitmaps.
    static constexpr std::byte kPngSignature[] = { std::byte{ 0x89 }, std::byte{ 'P' }, std::byte{ 'N' }, std::byte{ 'G' },
                                                   std::byte{ 0x0D }, std::byte{ 0x0A }, std::byte{ 0x1A }, std::byte{ 0x0A } };
    if (!bytes || size < sizeof(kPngSignature) || !std::equal(std::begin(kPngSignature), std::end(kPngSignature), bytes))
        return {};

    return { bytes, bytes + size };
}

// The name is only valid inside the callback.
BOOL CALLBACK ReadFirstGroup(HMODULE module, LPCWSTR, LPWSTR name, LONG_PTR param)
{
    *reinterpret_cast<std::vector<std::byte>*>(param) = ReadLargestImage(module, name);
    return FALSE;
}

} // namespace

std::vector<std::byte> ReadHostIcon()
{
    std::vector<std::byte> icon;
    EnumResourceNamesW(GetModuleHandleW(nullptr), RT_GROUP_ICON, ReadFirstGroup,
                       reinterpret_cast<LONG_PTR>(&icon));
    return icon;
}
