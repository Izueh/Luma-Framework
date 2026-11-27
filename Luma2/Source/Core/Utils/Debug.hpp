#pragma once

#include "Notifications/Notifications.hpp"

// As 1.0: Development and Test builds, compiled out in Publishing. Delivered by Notifications::Assert (a message box asking to break,
// or a toast when asserts are silent), once per call site.
#if DEVELOPMENT || TEST
#define ASSERT_ONCE_MSG(expression, message)                                      \
   do                                                                             \
   {                                                                              \
      static bool asserted_once = false;                                          \
      if (!asserted_once && !(expression))                                        \
      {                                                                           \
         asserted_once = true;                                                    \
         ::Luma::Notifications::Assert(#expression, message, __FILE__, __LINE__); \
      }                                                                           \
   } while (false)
#else
#define ASSERT_ONCE_MSG(expression, message) ((void)0)
#endif
#define ASSERT_ONCE(expression) ASSERT_ONCE_MSG(expression, "")
// A feature this backend doesn't have yet: loud once in Development and Test, a no-op in Publishing. `what` is a string literal ("Resources::Scaling").
#define LUMA_UNIMPLEMENTED(what) ASSERT_ONCE_MSG(false, "unimplemented: " what)
