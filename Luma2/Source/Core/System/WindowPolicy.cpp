#include "System/WindowPolicy.hpp"

#include <mutex>
#include <unordered_map>

namespace Luma {
namespace System {
namespace Window {
namespace {
struct SavedWindow
{
   LONG_PTR style = 0;
   LONG_PTR ex_style = 0;
   RECT rect = {};
};

std::mutex saved_mutex;
std::unordered_map<HWND, SavedWindow> saved_windows;
} // namespace

void MakeBorderless(HWND window)
{
   MONITORINFO info = {.cbSize = sizeof(info)};
   if (!window || !GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info))
      return;
   const RECT& monitor = info.rcMonitor;
   const int monitor_width = monitor.right - monitor.left;
   const int monitor_height = monitor.bottom - monitor.top;

   LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
   LONG_PTR extended_style = GetWindowLongPtrW(window, GWL_EXSTYLE);
   RECT rect = {};
   GetWindowRect(window, &rect);
   {
      std::scoped_lock lock(saved_mutex);
      saved_windows.try_emplace(window, SavedWindow{.style = style, .ex_style = extended_style, .rect = rect});
   }

   style &= ~(WS_OVERLAPPEDWINDOW | WS_BORDER | WS_THICKFRAME | WS_DLGFRAME);
   style |= WS_POPUP | WS_VISIBLE;
   SetWindowLongPtrW(window, GWL_STYLE, style);
   extended_style &= ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
   SetWindowLongPtrW(window, GWL_EXSTYLE, extended_style);

   int width = rect.right - rect.left;
   int height = rect.bottom - rect.top;
   // A game resizing itself past the screen (Thumper) is brought back to it. Other sizes are kept: some games stretch or
   // refuse a forced resize.
   const bool resize = width > monitor_width || height > monitor_height;
   if (resize)
   {
      width = monitor_width;
      height = monitor_height;
   }
   // Asynchronous: the window's thread may be waiting for this one
   SetWindowPos(window, nullptr, monitor.left + (monitor_width - width) / 2, monitor.top + (monitor_height - height) / 2, resize ? width : 0, resize ? height : 0,
      (resize ? 0 : SWP_NOSIZE) | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_ASYNCWINDOWPOS);
}

void RestoreBorderless(HWND window)
{
   if (!window || !IsWindow(window))
      return;
   SavedWindow saved;
   {
      std::scoped_lock lock(saved_mutex);
      const auto it = saved_windows.find(window);
      if (it == saved_windows.end())
         return;
      saved = it->second;
      saved_windows.erase(it);
   }
   SetWindowLongPtrW(window, GWL_STYLE, saved.style);
   SetWindowLongPtrW(window, GWL_EXSTYLE, saved.ex_style);
   SetWindowPos(window, nullptr, saved.rect.left, saved.rect.top, saved.rect.right - saved.rect.left, saved.rect.bottom - saved.rect.top,
      SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_ASYNCWINDOWPOS);
}
} // namespace Window
} // namespace System
} // namespace Luma
