#include "Utils/Debug.hpp"

#include <Windows.h>

#include "Utils/Log.hpp"

namespace Luma {
namespace Utils {
namespace Debug {
void Fail(std::string_view expression, std::string_view message, std::string_view file, int line)
{
   Log::Error("Assert: {} ({}) at {}:{}", message.empty() ? expression : message, expression, file, line);
   // 1.0 asks through a message box, which a fullscreen game can hide
   if (IsDebuggerPresent())
      __debugbreak();
}
} // namespace Debug
} // namespace Utils
} // namespace Luma
