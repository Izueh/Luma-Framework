#include "System/Window.hpp"

namespace Luma {
namespace System {
namespace Window {
void MakeBorderless(HWND window)
{
   MONITORINFO info = {.cbSize = sizeof(info)};
   if (!window || !GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info))
      return;
   const RECT& monitor = info.rcMonitor;
   const int monitor_width = monitor.right - monitor.left;
   const int monitor_height = monitor.bottom - monitor.top;

   LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
   style &= ~(WS_OVERLAPPEDWINDOW | WS_BORDER | WS_THICKFRAME | WS_DLGFRAME);
   style |= WS_POPUP | WS_VISIBLE;
   SetWindowLongPtrW(window, GWL_STYLE, style);
   LONG_PTR extended_style = GetWindowLongPtrW(window, GWL_EXSTYLE);
   extended_style &= ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
   SetWindowLongPtrW(window, GWL_EXSTYLE, extended_style);

   RECT rect;
   GetWindowRect(window, &rect);
   int width = rect.right - rect.left;
   int height = rect.bottom - rect.top;
   // 1.0: a game resizing itself past the screen (Thumper) is brought back to it. Other sizes are kept: some games stretch or
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
} // namespace Window
} // namespace System
} // namespace Luma
