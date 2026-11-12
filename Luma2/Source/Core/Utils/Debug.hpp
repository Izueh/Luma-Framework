#pragma once

#include <string_view>

namespace Luma {
namespace Utils {
namespace Debug {
// Logs the failure and breaks into an attached debugger. Development builds only.
void Fail(std::string_view expression, std::string_view message, std::string_view file, int line);
} // namespace Debug
} // namespace Utils
} // namespace Luma

#if DEVELOPMENT
#define ASSERT_ONCE_MSG(expression, message)                                   \
   do                                                                          \
   {                                                                           \
      static bool asserted_once = false;                                       \
      if (!asserted_once && !(expression))                                     \
      {                                                                        \
         asserted_once = true;                                                 \
         ::Luma::Utils::Debug::Fail(#expression, message, __FILE__, __LINE__); \
      }                                                                        \
   } while (false)
#else
#define ASSERT_ONCE_MSG(expression, message) ((void)0)
#endif
#define ASSERT_ONCE(expression) ASSERT_ONCE_MSG(expression, "")
// A feature this backend doesn't have yet: loud once in Development, a no-op otherwise. `what` is a string literal ("Resources::Scaling").
#define LUMA_UNIMPLEMENTED(what) ASSERT_ONCE_MSG(false, "unimplemented: " what)
