#include "overlay/overlay.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <optional>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_win32.h>

#include "hooks/scope.hpp"
#include "overlay/d3d11_renderer.hpp"
#include "overlay/input.hpp"
#include "overlay/input_hooks.hpp"
#include "utils/config.hpp"
#include "utils/log.hpp"

// Declared by ImGui's Win32 backend only in a comment, to keep <Windows.h> out of its header
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

namespace Luma {
namespace Overlay {
namespace {
// Weak: the overlay detaches when the swapchain or its device goes
struct Attachment
{
   HWND window = nullptr;
   IDXGISwapChain* swapchain = nullptr;
   ID3D11Device* device = nullptr;
};

// Guards the attachment and every ImGui call. Recursive: releasing the mouse capture in ImGui's message handler sends
// WM_CAPTURECHANGED back into the window procedure.
std::recursive_mutex mutex;
// ImGui has a context exactly while this is set
std::optional<Attachment> attached;

std::atomic<bool> open = false;
DrawFunction draw_main = nullptr;
DrawFunction draw_always = nullptr;

constexpr const char* CONFIG_SECTION = "Luma";
constexpr const char* FONT_SIZE_CONFIG = "FontSize";
constexpr float MIN_FONT_SIZE = 8.f;
constexpr float MAX_FONT_SIZE = 48.f;
float font_size = 0.f;

// With the lock held
bool Attach(IDXGISwapChain* swapchain, ID3D11Device* device)
{
   DXGI_SWAP_CHAIN_DESC desc = {};
   if (FAILED(swapchain->GetDesc(&desc)) || !desc.OutputWindow)
      return false;
   ImGui::CreateContext();
   ImGuiIO& io = ImGui::GetIO();
   io.IniFilename = nullptr; // Nothing written next to the game
   io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
   ImGui::StyleColorsDark();
   const float scale = ImGui_ImplWin32_GetDpiScaleForHwnd(desc.OutputWindow);
   ImGui::GetStyle().ScaleAllSizes(scale);
   ImGui::GetStyle().FontScaleDpi = scale;
   float saved_font_size = 0.f;
   if (Config::Get(CONFIG_SECTION, FONT_SIZE_CONFIG, saved_font_size))
      font_size = std::clamp(saved_font_size, MIN_FONT_SIZE, MAX_FONT_SIZE);
   ImGui_ImplWin32_Init(desc.OutputWindow);

   attached = Attachment{desc.OutputWindow, swapchain, device};
   Input::Attach(desc.OutputWindow);
   Utils::Log::Info("Overlay: attached to swapchain {} (window {})", static_cast<void*>(swapchain), static_cast<void*>(desc.OutputWindow));
   return true;
}

// With the lock held
void Detach()
{
   if (!attached)
      return;
   D3D11::Shutdown();
   ImGui_ImplWin32_Shutdown();
   ImGui::DestroyContext();
   Input::Detach(attached->window);
   Utils::Log::Info("Overlay: detached from swapchain {}", static_cast<void*>(attached->swapchain));
   attached.reset();
}

void DrawDockSpace()
{
   const ImGuiViewport* viewport = ImGui::GetMainViewport();
   const ImGuiID root_space_id = ImGui::GetID("ViewportDockspace");
   if (!ImGui::DockBuilderGetNode(root_space_id))
   {
      ImGui::DockBuilderAddNode(root_space_id, ImGuiDockNodeFlags_DockSpace);
      ImGui::DockBuilderSetNodeSize(root_space_id, viewport->Size);
      ImGuiID main_space_id = 0;
      ImGuiID right_space_id = 0;
      ImGui::DockBuilderSplitNode(root_space_id, ImGuiDir_Left, 1.f / 3.f, &main_space_id, &right_space_id);
      ImGui::DockBuilderDockWindow("Luma", main_space_id);
      ImGui::DockBuilderDockWindow("Overlay Settings", main_space_id);
      ImGui::DockBuilderFinish(root_space_id);
   }

   ImGui::SetNextWindowPos(viewport->Pos);
   ImGui::SetNextWindowSize(viewport->Size);
   ImGui::SetNextWindowViewport(viewport->ID);
   ImGui::Begin("Viewport", nullptr,
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);
   ImGui::DockSpace(root_space_id, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
   ImGui::End();
}

void DrawOverlaySettings()
{
   const std::string key_label = Input::IsCapturingToggleKey() ? "Press a key (Escape cancels)" : Input::GetKeyName(Input::GetToggleKey());
   if (ImGui::Button((key_label + "###OverlayKey").c_str()))
      Input::CaptureToggleKey();
   ImGui::SameLine();
   ImGui::TextUnformatted("Overlay key");
   if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Opens and closes this overlay");

   ImGui::SliderFloat("Font size", &font_size, MIN_FONT_SIZE, MAX_FONT_SIZE, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
   if (ImGui::IsItemDeactivatedAfterEdit())
   {
      Config::SetValue(CONFIG_SECTION, FONT_SIZE_CONFIG, double(font_size));
      Config::Save();
   }
}

void BuildFrame(const DXGI_SWAP_CHAIN_DESC& desc, ID3D11Device* device)
{
   ImGui_ImplWin32_NewFrame();
   ImGuiIO& io = ImGui::GetIO();
   // The back buffer may not have the window's size (stretched by DXGI): ImGui works in window coordinates
   if (io.DisplaySize.x > 0.f && io.DisplaySize.y > 0.f)
      io.DisplayFramebufferScale = ImVec2(float(desc.BufferDesc.Width) / io.DisplaySize.x, float(desc.BufferDesc.Height) / io.DisplaySize.y);
   // Games often hide the cursor
   io.MouseDrawCursor = open;
   if (font_size > 0.f)
      ImGui::GetStyle().FontSizeBase = font_size;
   ImGui::NewFrame();
   if (font_size <= 0.f)
      font_size = std::clamp(ImGui::GetStyle().FontSizeBase, MIN_FONT_SIZE, MAX_FONT_SIZE);
   if (draw_always)
      draw_always(device);
   if (open && draw_main)
   {
      DrawDockSpace();
      if (ImGui::Begin("Luma", nullptr, ImGuiWindowFlags_NoFocusOnAppearing))
         draw_main(device);
      ImGui::End();
      if (ImGui::Begin("Overlay Settings", nullptr, ImGuiWindowFlags_NoFocusOnAppearing))
         DrawOverlaySettings();
      ImGui::End();
   }
   ImGui::Render();
}
} // namespace

void SetDrawFunctions(DrawFunction main, DrawFunction always)
{
   draw_main = main;
   draw_always = always;
}

void OnSwapchainCreated(IDXGISwapChain* swapchain, ID3D11Device* device)
{
   std::scoped_lock lock(mutex);
   if (!attached)
      Attach(swapchain, device);
}

void OnPresent(IDXGISwapChain* swapchain)
{
   Hooks::Scope::Guard scope;
   std::scoped_lock lock(mutex);
   if (!attached || attached->swapchain != swapchain)
      return;
   Input::ApplyCapturedKey();

   DXGI_SWAP_CHAIN_DESC desc = {};
   if (FAILED(swapchain->GetDesc(&desc)) || !D3D11::NewFrame(attached->device))
      return;
   BuildFrame(desc, attached->device);
   D3D11::Render(attached->device, swapchain, ImGui::GetDrawData());
}

void OnSwapchainDestroyed(const IDXGISwapChain* swapchain)
{
   std::scoped_lock lock(mutex);
   if (attached && swapchain == attached->swapchain)
      Detach();
}

void OnDeviceDestroyed(const void* device)
{
   Hooks::Scope::Guard scope;
   std::scoped_lock lock(mutex);
   if (attached && device == attached->device)
      Detach();
}

void OnWindowMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
   // ImGui's backend reads the key state (modifiers): Luma's own calls pass through the input hooks
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
   Utils::Log::Info("Overlay: {}", value ? "opened" : "closed");
   InputHooks::OnOpenChanged(value);
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
} // namespace Overlay
} // namespace Luma
