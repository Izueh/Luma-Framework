#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Luma {
namespace Utils {
namespace Strings {
// Unlike "path::string()", doesn't throw on characters the ANSI code page can't represent
std::string ToUtf8(const std::filesystem::path& path);
std::string ToUtf8(std::wstring_view text);
// Literals and C strings convert to both of the above
inline std::string ToUtf8(const wchar_t* text) { return ToUtf8(std::wstring_view(text)); }

bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b);
} // namespace Strings
} // namespace Utils
} // namespace Luma
