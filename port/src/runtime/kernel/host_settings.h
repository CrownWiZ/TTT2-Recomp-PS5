#pragma once

#include <filesystem>
#include <initializer_list>
#include <string>
#include <utility>

namespace host_settings {

// The override is shared by the keyboard and display settings and by test harnesses.
std::filesystem::path Path();
std::string PathLabel(); // UTF-8 for diagnostics only
// Location for optional diagnostic triggers and dumps. Relative names may have
// subdirectories; callers create those subdirectories before writing files.
std::string DiagnosticPath(const std::string& relative);
std::string Read(const char* section, const char* key, const char* fallback);
int ReadInt(const char* section, const char* key, int fallback);
void Write(const char* section, const char* key, const std::string& value);
void WriteMany(const char* section,
               std::initializer_list<std::pair<const char*, std::string>> values);

} // namespace host_settings
