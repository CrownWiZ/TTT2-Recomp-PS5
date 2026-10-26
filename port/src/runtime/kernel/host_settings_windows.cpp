#include "host_settings_platform.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdlib>
#include <string>

namespace host_settings::platform {
namespace {
// Section and key names in this settings schema are fixed ASCII literals.
std::wstring Ascii(const char* text)
{
    std::wstring result;
    for (; *text; ++text) result.push_back(static_cast<unsigned char>(*text));
    return result;
}

bool ToWide(const std::string& text, std::wstring& result)
{
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           text.c_str(), -1, nullptr, 0);
    if (!length) return false;
    result.resize(length);
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1,
                            result.data(), length) != length) return false;
    result.resize(length - 1);
    return true;
}

std::string ToUtf8(const std::wstring& text)
{
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    if (!length) return {};
    std::string result(length, '\0');
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        text.data(), int(text.size()), result.data(), length, nullptr, nullptr) == length
        ? result : std::string{};
}
} // namespace

std::filesystem::path SettingsOverride()
{
    const wchar_t* configured = _wgetenv(L"TTT2_SETTINGS_INI");
    return configured && *configured ? std::filesystem::path(configured) :
                                       std::filesystem::path{};
}

std::filesystem::path DefaultSettingsFolder()
{
    const wchar_t* local = _wgetenv(L"LOCALAPPDATA");
    const std::filesystem::path root = local && *local ?
        std::filesystem::path(local) : std::filesystem::path(L".");
    return root / L"TTT2Recomp";
}

std::filesystem::path DefaultDiagnosticFolder(const std::filesystem::path&)
{
    return L"C:/ttt2/port";
}

std::string Read(const std::filesystem::path& path, const char* section,
                 const char* key, const char* fallback)
{
    if (!path.is_absolute()) return fallback;
    const std::wstring wideSection = Ascii(section), wideKey = Ascii(key);
    std::wstring wideFallback;
    if (!ToWide(fallback, wideFallback)) return fallback;
    for (size_t capacity = 256; capacity <= 1024 * 1024; capacity *= 2)
    {
        std::wstring value(capacity, L'\0');
        const DWORD length = GetPrivateProfileStringW(wideSection.c_str(), wideKey.c_str(),
            wideFallback.c_str(), value.data(), DWORD(value.size()), path.c_str());
        if (length < value.size() - 1)
        {
            value.resize(length);
            return ToUtf8(value);
        }
    }
    return fallback;
}

void WriteMany(const std::filesystem::path& path, const char* section,
               std::initializer_list<std::pair<const char*, std::string>> values)
{
    if (!path.is_absolute()) return;
    const std::wstring wideSection = Ascii(section);
    for (const auto& [key, value] : values)
    {
        std::wstring wideValue;
        if (!ToWide(value, wideValue)) continue;
        const std::wstring wideKey = Ascii(key);
        WritePrivateProfileStringW(wideSection.c_str(), wideKey.c_str(),
                                   wideValue.c_str(), path.c_str());
    }
}
} // namespace host_settings::platform
