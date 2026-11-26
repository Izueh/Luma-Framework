#include "UI/Overlay.hpp"

#include <array>
#include <atomic>
#include <iterator>
#include <optional>
#include <utility>

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_win32.h>

#include "ReShade/ReShade.hpp"
#include "Hooks/InputHooks.hpp"
#include "Hooks/HookScope.hpp"
#include "Settings/SettingKeys.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Settings/SettingsStorage.hpp"
#include "System/JobQueue.hpp"
#include "UI/OverlayInput.hpp"
#include "UI/Icons.hpp"
#include "UI/OverlayWindow.hpp"
#include "UI/Style.hpp"
#include "UI/Widgets.hpp"
#include "Log/Log.hpp"
#include "Utils/Strings.hpp"

// Declared by ImGui's Win32 backend only in a comment, to keep <Windows.h> out of its header
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

namespace Luma {
namespace UI {
namespace {
struct Window
{
   HWND hwnd = nullptr;
   float dpi_scale = 1.f;
};

constexpr std::array SETTINGS = {
   Settings::Setting{
      .key = Settings::Core::OVERLAY_FONT_SIZE.key,
      .label = "Font size",
      .tooltip = "Of this overlay, before the display's scaling",
      .category = "Overlay",
      .default_value = Style::Sizes::DEFAULT_FONT,
      .range = Settings::Range{8.f, 48.f},
      .format = "%.0f px",
   },
   Settings::Setting{
      .key = Settings::Core::OVERLAY_OPACITY.key,
      .label = "Background opacity",
      .tooltip = "Of this overlay's window: lower lets the game show through",
      .category = "Overlay",
      .default_value = 100.f,
      .range = Settings::Range{20.f, 100.f},
      .format = "%.0f%%",
      .live = true,
   },
   Settings::Setting{
      .key = Settings::Core::OVERLAY_HDR_PAPER_WHITE.key,
      .label = "HDR brightness",
      .tooltip = "This overlay's white on an HDR swapchain, in nits. 203 is BT.2408's reference white.",
      .category = "Overlay",
      .default_value = 203.f,
      .range = Settings::Range{80.f, 500.f},
      .format = "%.0f nits",
      .live = true,
   },
};

// Guards the window and every ImGui call. Recursive: releasing the mouse capture in ImGui's message handler sends
// WM_CAPTURECHANGED back into the window procedure.
std::recursive_mutex mutex;
// ImGui has a context exactly while this is set
std::optional<Window> window;

std::atomic<bool> open = false;

// Render thread only, like the settings
std::filesystem::path settings_path;
bool settings_changed = false;
uint64_t toasted_job_report = 0;

// What a finished job reported (a shader build), also while the overlay is closed
void ToastJobReport()
{
   System::Jobs::Report report = System::Jobs::GetLastReport();
   if (report.id == toasted_job_report)
      return;
   toasted_job_report = report.id;
   Widgets::Toast(report.outcome.failed ? Widgets::Tone::Error : Widgets::Tone::Success, std::move(report.outcome.message));
}

void SaveSettings()
{
   settings_changed = false;
   if (!Settings::Storage::Save(settings_path))
      Log::Warning("UI: settings couldn't be saved to {}", Utils::Strings::ToUtf8(settings_path));
}

// With the lock held
void SaveLayout()
{
   size_t size = 0;
   const char* ini = ImGui::SaveIniSettingsToMemory(&size);
   Settings::Storage::SetOverlayLayout(ini && size ? std::string_view(ini, size) : std::string_view());
   ImGui::GetIO().WantSaveIniSettings = false;
   settings_changed = true;
}
} // namespace

void Initialize()
{
   Settings::Register(SETTINGS, Settings::Kind::Luma);
}

void SaveSettingsTo(const std::filesystem::path& path)
{
   settings_path = path;
   Settings::AddChangeListener([](const Settings::Setting&, const Settings::Value&) { settings_changed = true; });
}

std::unique_lock<std::recursive_mutex> Lock()
{
   return std::unique_lock(mutex);
}

bool AttachWindow(HWND hwnd)
{
   if (window || !hwnd)
      return false;
   ImGui::CreateContext();
   ImGuiIO& io = ImGui::GetIO();
   io.IniFilename = nullptr; // Persisted in Luma.toml instead of imgui.ini next to the game
   io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
   if (const std::string layout = Settings::Storage::GetOverlayLayout(); !layout.empty())
      ImGui::LoadIniSettingsFromMemory(layout.data(), layout.size());
   Fonts::Load();
   ImGui_ImplWin32_Init(hwnd);
   window = Window{hwnd, ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd)};
   Input::Attach(hwnd);
   return true;
}

void DetachWindow()
{
   if (!window)
      return;
   if (ImGui::GetCurrentContext() && ImGui::GetIO().WantSaveIniSettings)
      SaveLayout();
   ImGui_ImplWin32_Shutdown();
   ImGui::DestroyContext();
   Input::Detach(window->hwnd);
   window.reset();
}

bool BeginPresent()
{
   ToastJobReport();
   // Toasts are drawn while the overlay is closed too
   if (open || Widgets::HasToasts())
      return true;
   if (settings_changed)
      SaveSettings();
   return false;
}

void BuildFrame(Extent extent)
{
   ImGui_ImplWin32_NewFrame();
   ImGuiIO& io = ImGui::GetIO();
   // The back buffer may not have the window's size (stretched by DXGI): ImGui works in window coordinates
   if (io.DisplaySize.x > 0.f && io.DisplaySize.y > 0.f)
      io.DisplayFramebufferScale = ImVec2(float(extent.width) / io.DisplaySize.x, float(extent.height) / io.DisplaySize.y);
   // Games often hide the cursor
   io.MouseDrawCursor = open;
   Style::Apply(Settings::Get(Settings::Core::OVERLAY_FONT_SIZE), window->dpi_scale, Settings::Get(Settings::Core::OVERLAY_OPACITY) / 100.f);
   ImGui::NewFrame();
   if (open)
      DrawWindow();
   Widgets::DrawToasts();
   ImGui::Render();
}

void EndFrame()
{
   if (!ImGui::GetCurrentContext())
      return;
   if (ImGui::GetIO().WantSaveIniSettings && !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
      SaveLayout();
   // Saved once an edit is done (not on every step of a slider drag)
   if (settings_changed && !ImGui::IsAnyItemActive())
      SaveSettings();
}

void OnWindowMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
   // ImGui reads modifier keys, which the input hooks would hide from it
   Hooks::Scope::Guard scope;
   std::scoped_lock lock(mutex);
   if (window)
      ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam);
}

void OnRawMouse(USHORT button_flags, USHORT button_data)
{
   Hooks::Scope::Guard scope;
   std::scoped_lock lock(mutex);
   if (!window || button_flags == 0)
      return;

   struct ButtonFlags
   {
      USHORT down;
      USHORT up;
   };

   // In ImGui's order: left, right, middle, X1, X2
   ButtonFlags buttons[] = {
      {RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP},
      {RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP},
      {RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP},
      {RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP},
      {RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP},
   };
   // Raw input has the physical buttons: the legacy messages (the backend's) swap them for left-handed users
   if (GetSystemMetrics(SM_SWAPBUTTON))
      std::swap(buttons[0], buttons[1]);
   ImGuiIO& io = ImGui::GetIO();
   for (int i = 0; i < int(std::size(buttons)); i++)
   {
      if (button_flags & buttons[i].down)
         io.AddMouseButtonEvent(i, true);
      if (button_flags & buttons[i].up)
         io.AddMouseButtonEvent(i, false);
   }
   // As the backend does for "WM_MOUSEWHEEL" and "WM_MOUSEHWHEEL"
   const float wheel = float(SHORT(button_data)) / float(WHEEL_DELTA);
   if (button_flags & RI_MOUSE_WHEEL)
      io.AddMouseWheelEvent(0.f, wheel);
   if (button_flags & RI_MOUSE_HWHEEL)
      io.AddMouseWheelEvent(-wheel, 0.f);
}

bool IsOpen()
{
   return open;
}

void SetOpen(bool value)
{
   // One overlay open at a time, ReShade's or Luma's
   if (value && !open && ReShade::IsOverlayOpen())
   {
      Log::Info("UI: overlay kept closed, ReShade's is open");
      return;
   }
   if (open.exchange(value) == value)
      return;
   Log::Info("UI: overlay {}", value ? "opened" : "closed");
   Hooks::Input::OnOpenChanged(value);
   if (value)
      return;
   // ImGui stops getting messages, so keys held now would stay down for it
   std::scoped_lock lock(mutex);
   if (window)
   {
      ImGui::GetIO().ClearInputKeys();
      ImGui::GetIO().ClearInputMouse();
   }
}

void ResetLayout()
{
   std::scoped_lock lock(mutex);
   Settings::Storage::SetOverlayLayout({});
   if (window && ImGui::GetCurrentContext())
   {
      ImGui::ClearIniSettings();
      ImGui::GetIO().WantSaveIniSettings = false;
   }
   RequestResetWindowLayout();
   settings_changed = true;
}
} // namespace UI
} // namespace Luma
