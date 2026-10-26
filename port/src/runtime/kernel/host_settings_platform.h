#pragma once

#include <filesystem>
#include <initializer_list>
#include <string>
#include <utility>

// Private host boundary for the settings store. Public callers use
// host_settings.h and never need to know the OS configuration file API.
namespace host_settings::platform {
std::filesystem::path SettingsOverride();
std::filesystem::path DefaultSettingsFolder();
std::filesystem::path DefaultDiagnosticFolder(const std::filesystem::path& settingsPath);
std::string Read(const std::filesystem::path& path, const char* section,
                 const char* key, const char* fallback);
void WriteMany(const std::filesystem::path& path, const char* section,
               std::initializer_list<std::pair<const char*, std::string>> values);
}
