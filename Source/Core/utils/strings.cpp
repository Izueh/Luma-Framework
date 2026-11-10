#include "utils/strings.hpp"

#include <Windows.h>

namespace Luma {
namespace Utils {
namespace Strings {
std::string ToUtf8(const std::filesystem::path& path)
{
   const std::u8string text = path.u8string();
   return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::string ToUtf8(std::wstring_view text)
{
   if (text.empty())
      return {};
   const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
   std::string result(size_t(size), '\0');
   WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), result.data(), size, nullptr, nullptr);
   return result;
}

bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b)
{
   return CompareStringOrdinal(a.data(), int(a.size()), b.data(), int(b.size()), TRUE) == CSTR_EQUAL;
}
} // namespace Strings
} // namespace Utils
} // namespace Luma
