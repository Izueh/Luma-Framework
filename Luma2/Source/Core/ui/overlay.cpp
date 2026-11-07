#include "ui/overlay.hpp"

#include <atomic>
#include <mutex>
#include <optional>
#include <variant>

#include <imgui.h>
#include <backends/imgui_impl_win32.h>

#include "hooks/scope.hpp"
#include "settings/registry.hpp"
#include "settings/storage.hpp"
#include "ui/d3d11_renderer.hpp"
#include "ui/d3d12_renderer.hpp"
#include "ui/input.hpp"
#include "ui/main_ui.hpp"
#include "utils/log.hpp"
#include "utils/strings.hpp"

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
   ImGui::StyleColorsDark();
   const float scale = ImGui_ImplWin32_GetDpiScaleForHwnd(desc.OutputWindow);
   ImGui::GetStyle().ScaleAllSizes(scale);
   ImGui::GetStyle().FontScaleDpi = scale;
   ImGui_ImplWin32_Init(desc.OutputWindow);

   attached = Attachment{desc.OutputWindow, swapchain, presenter, device};
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
   io.MouseDrawCursor = true;
   ImGui::NewFrame();
   DrawMainUI();
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
   if (!open)
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
