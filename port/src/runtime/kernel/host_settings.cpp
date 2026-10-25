#include "host_settings.h"
#include "host_settings_platform.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>

namespace host_settings {
namespace {
std::mutex g_fileMutex;
}

std::filesystem::path Path()
{
    std::filesystem::path path = platform::SettingsOverride();
    std::error_code error;
    if (path.empty())
    {
        const auto folder = platform::DefaultSettingsFolder();
        std::filesystem::create_directories(folder, error);
        path = folder / "settings.ini";
    }
    const auto absolute = std::filesystem::absolute(path, error);
    return error ? path : absolute;
}

std::string PathLabel()
{
    const auto utf8 = Path().u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

std::string DiagnosticPath(const std::string& relative)
{
    static const std::filesystem::path root = [] {
        if (const char* overridePath = std::getenv("TTT2_DEBUG_ROOT"); overridePath && *overridePath)
            return std::filesystem::path(overridePath);
        return platform::DefaultDiagnosticFolder(Path());
    }();
    std::error_code error;
    std::filesystem::create_directories(root, error);
    return (root / relative).string();
}

std::string Read(const char* section, const char* key, const char* fallback)
{
    const auto path = Path();
    std::lock_guard lock(g_fileMutex);
    return platform::Read(path, section, key, fallback);
}

int ReadInt(const char* section, const char* key, int fallback)
{
    const std::string value = Read(section, key, "");
    if (value.empty()) return fallback;
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    return end != value.c_str() && *end == '\0' ? int(parsed) : fallback;
}

void Write(const char* section, const char* key, const std::string& value)
{
    WriteMany(section, {{key, value}});
}

void WriteMany(const char* section,
               std::initializer_list<std::pair<const char*, std::string>> values)
{
    const auto path = Path();
    std::lock_guard lock(g_fileMutex);
    platform::WriteMany(path, section, values);
}
} // namespace host_settings
