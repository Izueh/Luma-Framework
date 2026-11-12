#include "UI/Overlay.hpp"

#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <variant>

#include <imgui.h>
#include <backends/imgui_impl_win32.h>

#include "Hooks/InputHooks.hpp"
#include "Hooks/Scope.hpp"
#include "Settings/Registry.hpp"
#include "Settings/Storage.hpp"
#include "UI/D3D11Renderer.hpp"
#include "UI/D3D12Renderer.hpp"
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

constexpr std::string_view FONT_SIZE_SETTING = "Luma.Overlay.FontSize";

constexpr std::array SETTINGS = {
   Settings::Setting{
      .key = FONT_SIZE_SETTING,
      .label = "Font size",
      .tooltip = "Of this overlay, before the display's scaling",
      .category = "Overlay",
      .default_value = Style::Sizes::DEFAULT_FONT,
      .range = Settings::Range{8.f, 48.f},
      .format = "%.0f px",
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
   Style::Apply(Settings::Get<float>(FONT_SIZE_SETTING), attached->dpi_scale);
   ImGui::NewFrame();
   if (open)
      DrawShell();
   Widgets::DrawToasts();
   ImGui::Render();
}

// Whether a frame was drawn
bool Draw(IDXGISwapChain* swapchain, ID3D11Device* device, const DXGI_SWAP_CHAIN_DESC& desc)
{
   if (!D3D11::NewFrame(device))
      return false;
   BuildFrame(desc);
   D3D11::Render(device, swapchain, ImGui::GetDrawData());
   return true;
}

bool Draw(IDXGISwapChain* swapchain, ID3D12CommandQueue* queue, const DXGI_SWAP_CHAIN_DESC& desc)
{
   // A new format or buffer count needs new renderer objects, and with them a new context (see Detach)
   if (D3D12::NeedsRecreation(desc.BufferDesc.Format, desc.BufferCount))
   {
      const void* device = attached->device;
      Detach();
      if (!Attach(swapchain, queue, device))
         return false;
   }
   if (!D3D12::NewFrame(queue, desc.BufferDesc.Format, desc.BufferCount))
      return false;
   BuildFrame(desc);
   D3D12::Render(queue, swapchain, ImGui::GetDrawData());
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
   Settings::Register(SETTINGS);
}

void SaveSettingsTo(const std::filesystem::path& path)
{
   settings_path = path;
   Settings::AddChangeListener([](const Settings::Setting&, const Settings::Value&) { settings_changed = true; });
}

void OnSwapchainCreated(IDXGISwapChain* swapchain, const Presenter& presenter, const void* device)
{
   std::scoped_lock lock(mutex);
   if (!attached)
      Attach(swapchain, presenter, device);
}

void OnPresent(IDXGISwapChain* swapchain)
{
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

void OnSwapchainDestroyed(const void* swapchain)
{
   std::scoped_lock lock(mutex);
   if (attached && swapchain == attached->swapchain)
      Detach();
}

void OnDeviceReleased(const void* device)
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
