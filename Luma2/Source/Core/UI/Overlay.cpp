#include "UI/Overlay.hpp"

#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <variant>

#include <imgui.h>
#include <backends/imgui_impl_win32.h>

#include "Handlers/DXGI/DXGIHandlers.hpp"
#include "Hooks/InputHooks.hpp"
#include "Hooks/Scope.hpp"
#include "Settings/Keys.hpp"
#include "Settings/Registry.hpp"
#include "Settings/Storage.hpp"
#include "System/Jobs.hpp"
#include "UI/D3D11/D3D11Renderer.hpp"
#include "UI/D3D12/D3D12Renderer.hpp"
#include "UI/Encoding.hpp"
#include "UI/Input.hpp"
#include "UI/Icons.hpp"
#include "UI/Shell.hpp"
#include "UI/Style.hpp"
#include "UI/Widgets.hpp"
#include "Utils/Log.hpp"
#include "Utils/Strings.hpp"

// Declared by ImGui's Win32 backend only in a comment, to keep <Windows.h> out of its header
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

namespace Luma {
namespace UI {
namespace {
// Weak: the overlay detaches when the swapchain or its device goes
struct Attachment
{
   HWND window = nullptr;
   IDXGISwapChain* swapchain = nullptr;
   Presenter presenter;
   const void* device = nullptr;
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
      .tooltip = "This overlay's white on an HDR swapchain, in nits. 203 is the reference white of BT.2408 (as 1.0's overlay under ReShade).",
      .category = "Overlay",
      .default_value = 203.f,
      .range = Settings::Range{80.f, 500.f},
      .format = "%.0f nits",
      .live = true,
   },
};

// Guards the attachment and every ImGui call. Recursive: releasing the mouse capture in ImGui's message handler sends
// WM_CAPTURECHANGED back into the window procedure.
std::recursive_mutex mutex;
// ImGui has a context exactly while this is set
std::optional<Attachment> attached;

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

// With the lock held
bool Attach(IDXGISwapChain* swapchain, Presenter presenter, const void* device)
{
   DXGI_SWAP_CHAIN_DESC desc = {};
   if (FAILED(swapchain->GetDesc(&desc)) || !desc.OutputWindow)
      return false;
   ImGui::CreateContext();
   ImGuiIO& io = ImGui::GetIO();
   io.IniFilename = nullptr; // Nothing written next to the game
   io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
   Fonts::Load();
   ImGui_ImplWin32_Init(desc.OutputWindow);

   attached = Attachment{desc.OutputWindow, swapchain, presenter, device, ImGui_ImplWin32_GetDpiScaleForHwnd(desc.OutputWindow)};
   Input::Attach(desc.OutputWindow);
   Utils::Log::Info("UI: overlay attached to swapchain {} (window {})", static_cast<void*>(swapchain), static_cast<void*>(desc.OutputWindow));
   return true;
}

// With the lock held. ImGui's docking branch destroys the platform windows when a renderer shuts down, so a renderer is
// only recreated with a new context: detached, then attached again.
void Detach()
{
   if (!attached)
      return;
   D3D11::Shutdown();
   D3D12::Shutdown();
   ImGui_ImplWin32_Shutdown();
   ImGui::DestroyContext();
   Input::Detach(attached->window);
   Utils::Log::Info("UI: overlay detached from swapchain {}", static_cast<void*>(attached->swapchain));
   attached.reset();
}

// Between the renderer's NewFrame and Render
void BuildFrame(const DXGI_SWAP_CHAIN_DESC& desc)
{
   ImGui_ImplWin32_NewFrame();
   ImGuiIO& io = ImGui::GetIO();
   // The back buffer may not have the window's size (stretched by DXGI): ImGui works in window coordinates
   if (io.DisplaySize.x > 0.f && io.DisplaySize.y > 0.f)
      io.DisplayFramebufferScale = ImVec2(float(desc.BufferDesc.Width) / io.DisplaySize.x, float(desc.BufferDesc.Height) / io.DisplaySize.y);
   // Games often hide the cursor
   io.MouseDrawCursor = open;
   Style::Apply(Settings::Get(Settings::Core::OVERLAY_FONT_SIZE), attached->dpi_scale, Settings::Get(Settings::Core::OVERLAY_OPACITY) / 100.f);
   ImGui::NewFrame();
   if (open)
      DrawShell();
   Widgets::DrawToasts();
   ImGui::Render();
}

// Each API's renderer, by what presents (Presenter's types)
template <typename T>
struct Renderer;

template <>
struct Renderer<ID3D11Device>
{
   static constexpr auto NeedsRecreation = &D3D11::NeedsRecreation;
   static constexpr auto NewFrame = &D3D11::NewFrame;
   static constexpr auto Render = &D3D11::Render;
};

template <>
struct Renderer<ID3D12CommandQueue>
{
   static constexpr auto NeedsRecreation = &D3D12::NeedsRecreation;
   static constexpr auto NewFrame = &D3D12::NewFrame;
   static constexpr auto Render = &D3D12::Render;
};

// How the overlay is written to the swapchain's buffer
Encoding::Output GetOutput(IDXGISwapChain* swapchain, const DXGI_SWAP_CHAIN_DESC& desc)
{
   return {Encoding::GetKind(desc.BufferDesc.Format, Handlers::GetColorSpace(swapchain)), Settings::Get(Settings::Core::OVERLAY_HDR_PAPER_WHITE)};
}

// Whether a frame was drawn
template <typename T>
bool Draw(IDXGISwapChain* swapchain, T* presenter, const DXGI_SWAP_CHAIN_DESC& desc)
{
   using Traits = Renderer<T>;
   // A new format or buffer count needs new renderer objects, and with them a new context (see Detach)
   if (Traits::NeedsRecreation(desc))
   {
      const void* device = attached->device;
      Detach();
      if (!Attach(swapchain, presenter, device))
         return false;
   }
   if (!Traits::NewFrame(presenter, desc))
      return false;
   BuildFrame(desc);
   Traits::Render(presenter, swapchain, ImGui::GetDrawData(), GetOutput(swapchain, desc));
   return true;
}

void SaveSettings()
{
   settings_changed = false;
   if (!Settings::Storage::Save(settings_path))
      Utils::Log::Warning("UI: settings couldn't be saved to {}", Utils::Strings::ToUtf8(settings_path));
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

void AttachOverlay(IDXGISwapChain* swapchain, const Presenter& presenter, const void* device)
{
   std::scoped_lock lock(mutex);
   if (!attached)
      Attach(swapchain, presenter, device);
}

void DrawOverlay(IDXGISwapChain* swapchain)
{
   ToastJobReport();
   // Toasts are drawn while the overlay is closed too
   if (!open && !Widgets::HasToasts())
   {
      if (settings_changed)
         SaveSettings();
      return;
   }

   Hooks::Scope::Guard scope;
   std::scoped_lock lock(mutex);
   if (!attached || attached->swapchain != swapchain)
      return;
   Input::ApplyCapturedKey();

   DXGI_SWAP_CHAIN_DESC desc = {};
   if (FAILED(swapchain->GetDesc(&desc)))
      return;
   // A copy: drawing may attach again
   const bool drawn = std::visit([&](auto* presenter) { return Draw(swapchain, presenter, desc); }, Presenter(attached->presenter));
   // Saved once an edit is done (not on every step of a slider drag)
   if (drawn && settings_changed && !ImGui::IsAnyItemActive())
      SaveSettings();
}

void DetachFromSwapchain(const void* swapchain)
{
   std::scoped_lock lock(mutex);
   if (attached && swapchain == attached->swapchain)
      Detach();
}

void DetachFromDevice(const void* device)
{
   Hooks::Scope::Guard scope;
   std::scoped_lock lock(mutex);
   if (attached && device == attached->device)
      Detach();
}

void OnWindowMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
   // ImGui reads modifier keys, which the input hooks would hide from it
   Hooks::Scope::Guard scope;
   std::scoped_lock lock(mutex);
   if (attached)
      ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam);
}

bool IsOpen()
{
   return open;
}

void SetOpen(bool value)
{
   if (open.exchange(value) == value)
      return;
   Utils::Log::Info("UI: overlay {}", value ? "opened" : "closed");
   Hooks::Input::OnOpenChanged(value);
   if (value)
      return;
   // ImGui stops getting messages, so keys held now would stay down for it
   std::scoped_lock lock(mutex);
   if (attached)
   {
      ImGui::GetIO().ClearInputKeys();
      ImGui::GetIO().ClearInputMouse();
   }
}

const void* GetSwapchain()
{
   std::scoped_lock lock(mutex);
   return attached ? attached->swapchain : nullptr;
}
} // namespace UI
} // namespace Luma
