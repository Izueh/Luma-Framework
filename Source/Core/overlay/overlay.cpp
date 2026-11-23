#include "overlay/overlay.hpp"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_win32.h>

#include "reshade/reshade.hpp"
#include "hooks/scope.hpp"
#include "overlay/d3d11_renderer.hpp"
#include "overlay/encoding.hpp"
#include "overlay/input.hpp"
#include "overlay/input_hooks.hpp"
#include "utils/config.hpp"
#include "utils/log.hpp"
#include "utils/modules.hpp"

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

constexpr const char* PAPER_WHITE_CONFIG = "HDRBrightness";
constexpr float MIN_PAPER_WHITE = 20.f;
constexpr float MAX_PAPER_WHITE = 500.f;
float paper_white = 203.f; // Nits

// The color space recorded on the swapchain (Special K's convention, as ReShade)
std::optional<DXGI_COLOR_SPACE_TYPE> GetColorSpace(IDXGISwapChain* swapchain)
{
   constexpr GUID SKID_SwapChainColorSpace = {0x18b57e4, 0x1493, 0x4953, {0xad, 0xf2, 0xde, 0x6d, 0x99, 0xcc, 0x5, 0xe5}};
   DXGI_COLOR_SPACE_TYPE color_space = {};
   UINT size = sizeof(color_space);
   if (SUCCEEDED(swapchain->GetPrivateData(SKID_SwapChainColorSpace, &size, &color_space)) && size == sizeof(color_space))
      return color_space;
   return std::nullopt;
}

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
   io.Fonts->AddFontDefault();
   if (HMODULE self = Utils::Modules::Self())
   {
      if (HRSRC res = FindResourceW(self, L"FontAwesomeSolid", MAKEINTRESOURCEW(10)))
      {
         if (HGLOBAL loaded = LoadResource(self, res))
         {
            if (void* data = LockResource(loaded))
            {
               ImFontConfig icon_config;
               icon_config.MergeMode = true;
               icon_config.FontDataOwnedByAtlas = false;
               icon_config.ExtraSizeScale = 0.85f;
               io.Fonts->AddFontFromMemoryTTF(data, int(SizeofResource(self, res)), 13.f, &icon_config);
            }
         }
      }
   }
   ImGui::StyleColorsDark();
   const float scale = ImGui_ImplWin32_GetDpiScaleForHwnd(desc.OutputWindow);
   ImGui::GetStyle().ScaleAllSizes(scale);
   ImGui::GetStyle().FontScaleDpi = scale;
   float saved_font_size = 0.f;
   if (Config::Get(CONFIG_SECTION, FONT_SIZE_CONFIG, saved_font_size))
      font_size = std::clamp(saved_font_size, MIN_FONT_SIZE, MAX_FONT_SIZE);
   float saved_paper_white = 0.f;
   if (Config::Get(CONFIG_SECTION, PAPER_WHITE_CONFIG, saved_paper_white))
      paper_white = std::clamp(saved_paper_white, MIN_PAPER_WHITE, MAX_PAPER_WHITE);
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

   ImGui::SliderFloat("HDR brightness", &paper_white, MIN_PAPER_WHITE, MAX_PAPER_WHITE, "%.0f nits", ImGuiSliderFlags_AlwaysClamp);
   if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Brightness of the overlay's white on an HDR swapchain (scRGB or HDR10)");
   if (ImGui::IsItemDeactivatedAfterEdit())
   {
      Config::SetValue(CONFIG_SECTION, PAPER_WHITE_CONFIG, double(paper_white));
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
   const Encoding::Output output{Encoding::GetKind(desc.BufferDesc.Format, GetColorSpace(swapchain)), paper_white};
   D3D11::Render(attached->device, swapchain, ImGui::GetDrawData(), output);
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

// The mouse's clicks and wheel from raw input, for games that registered it without legacy messages: the backend ignores "WM_INPUT"
void OnRawMouse(USHORT button_flags, USHORT button_data)
{
   Hooks::Scope::Guard scope;
   std::scoped_lock lock(mutex);
   if (!attached || button_flags == 0)
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
   // Raw input has the physical buttons: legacy messages (the backend's) swap them for left-handed users
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
      Utils::Log::Info("Overlay: kept closed, ReShade's is open");
      return;
   }
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
