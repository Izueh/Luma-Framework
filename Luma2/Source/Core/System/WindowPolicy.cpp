#include "System/WindowPolicy.hpp"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

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

struct SubclassedWindow
{
   WNDPROC original_proc = nullptr;
   bool unicode = true;
};

// Never held with focus_mutex
std::mutex saved_mutex;
std::unordered_map<HWND, SavedWindow> saved_windows;

// Never held across CallWindowProc or with saved_mutex
std::mutex focus_mutex;
std::unordered_set<HWND> tracked_windows;
std::unordered_map<HWND, SubclassedWindow> subclassed_windows;
std::atomic<bool> suppress_focus_loss = false;

WNDPROC GetWindowProc(HWND window, bool unicode)
{
   return reinterpret_cast<WNDPROC>(unicode ? GetWindowLongPtrW(window, GWLP_WNDPROC) : GetWindowLongPtrA(window, GWLP_WNDPROC));
}

WNDPROC SetWindowProc(HWND window, bool unicode, WNDPROC proc)
{
   return reinterpret_cast<WNDPROC>(
      unicode ? SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(proc))
              : SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(proc)));
}

LRESULT WINAPI CustomWndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
   SubclassedWindow subclassed;
   {
      std::scoped_lock lock(focus_mutex);
      const auto it = subclassed_windows.find(window);
      if (it == subclassed_windows.end())
         return IsWindowUnicode(window) ? DefWindowProcW(window, message, wparam, lparam) : DefWindowProcA(window, message, wparam, lparam);
      subclassed = it->second;
      if (message == WM_NCDESTROY)
      {
         if (GetWindowProc(window, subclassed.unicode) == &CustomWndProc)
            SetWindowProc(window, subclassed.unicode, subclassed.original_proc);
         subclassed_windows.erase(it);
         tracked_windows.erase(window);
      }
   }
   if (message == WM_NCDESTROY)
   {
      std::scoped_lock lock(saved_mutex);
      saved_windows.erase(window);
   }
   if (suppress_focus_loss.load(std::memory_order_relaxed))
   {
      if (message == WM_KILLFOCUS || (message == WM_ACTIVATE && LOWORD(wparam) == WA_INACTIVE) || (message == WM_ACTIVATEAPP && wparam == FALSE))
         return 0;
   }
   return subclassed.unicode ? CallWindowProcW(subclassed.original_proc, window, message, wparam, lparam)
                             : CallWindowProcA(subclassed.original_proc, window, message, wparam, lparam);
}

// With focus_mutex held
void SubclassWindow(HWND window)
{
   if (!window || !IsWindow(window) || subclassed_windows.contains(window))
      return;
   const bool unicode = IsWindowUnicode(window) != FALSE;
   const WNDPROC current_proc = GetWindowProc(window, unicode);
   if (!current_proc || current_proc == &CustomWndProc)
      return;
   const WNDPROC previous_proc = SetWindowProc(window, unicode, &CustomWndProc);
   if (!previous_proc)
      return;
   subclassed_windows.emplace(window, SubclassedWindow{.original_proc = previous_proc, .unicode = unicode});
}

// With focus_mutex held. Restores the original WndProc only when nothing else chained on top of CustomWndProc;
// otherwise CustomWndProc stays in the chain and passes messages through while suppress_focus_loss is false.
void UnsubclassWindow(HWND window)
{
   const auto it = subclassed_windows.find(window);
   if (it == subclassed_windows.end())
      return;
   if (!IsWindow(window))
   {
      subclassed_windows.erase(it);
      return;
   }
   if (GetWindowProc(window, it->second.unicode) == &CustomWndProc)
   {
      SetWindowProc(window, it->second.unicode, it->second.original_proc);
      subclassed_windows.erase(it);
   }
}
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

void SetFocusLossSuppression(HWND window, bool enabled)
{
   suppress_focus_loss.store(enabled, std::memory_order_relaxed);
   std::scoped_lock lock(focus_mutex);
   std::erase_if(tracked_windows, [](HWND tracked) { return !IsWindow(tracked); });
   std::erase_if(subclassed_windows, [](const auto& entry) { return !IsWindow(entry.first); });
   if (window && IsWindow(window))
      tracked_windows.insert(window);
   for (const HWND tracked : tracked_windows)
   {
      if (enabled)
         SubclassWindow(tracked);
      else
         UnsubclassWindow(tracked);
   }
}

void SetFocusLossSuppression(bool enabled)
{
   SetFocusLossSuppression(nullptr, enabled);
}
} // namespace Window
} // namespace System
} // namespace Luma
