#include "..\..\Core\core.hpp"
#include "helper.hpp"
#include "hooks.hpp"

namespace
{
// Mid-function anchor inside GBFR_InitializeDX11RenderingPipeline around the cache fast-path.
constexpr const char* kSigInitDx11Anchor =
   "48 89 4D 20 48 39 0D ?? ?? ?? ?? 75 ?? 48 39 35 ?? ?? ?? ?? 75 ?? B0 01";
constexpr const char* kSigDispatchViewport =
   "55 56 48 81 EC 88 00 00 00 48 8D AC 24 80 00 00 00 48 83 E4 E0 48 89 CE C5 F8 57 C0 C5 FC 29 44 24 60 C5 FC 29 44 24 40";
// Early-function anchor inside UIRenderOrchestrator; signature is stable across prologue/XMM save changes.
constexpr const char* kSigUIOrchestratorAnchor =
   "C7 81 50 02 00 00 65 00 00 00 48 83 3D ?? ?? ?? ?? 00 0F 84 ?? ?? ?? ??";
#ifdef PATCH_JITTER_TABLE_INIT
constexpr const char* kSigTAAInit =
   "56 57 48 83 EC 48 C5 F8 29 7C 24 30 C5 F8 29 74 24 20 48 89 CF 48 8D 0D ?? ?? ?? ?? BA 24 00 00 00 E8 ?? ?? ?? ?? 89 87 28 02 00 00";
#endif
constexpr const char* kSigJitterCore =
   "48 8B 05 ?? ?? ?? ?? 89 C1 80 E1 ?? 88 4E 24 83 E0 ?? 8B 4C C6 28 8B 44 C6 2C 48 8B 15 ?? ?? ?? ?? F6 42 0B 01";
constexpr const char* kSigOutputLoadCaller =
   "48 63 0D ?? ?? ?? ?? 48 63 15 ?? ?? ?? ?? E8 ?? ?? ?? ?? 4C 8B 35 ?? ?? ?? ?? 48 8B 3D ?? ?? ?? ??";
constexpr const char* kSigCameraGlobalLoad =
   "48 8B 05 ?? ?? ?? ?? FF 50 10 40 84 F6 74 07 C6 05 ?? ?? ?? ?? 01";

void LogResolve(const char* name, const void* addr)
{
   std::string msg = std::string("GBFR sigscan: ") + name + " = 0x" + std::to_string(reinterpret_cast<uintptr_t>(addr));
   reshade::log::message(reshade::log::level::info, msg.c_str());
}

void LogResolveFailure(const char* name, std::size_t matches)
{
   std::string msg = std::string("GBFR sigscan: failed to resolve ") + name + ", matches=" + std::to_string(matches);
   reshade::log::message(reshade::log::level::warning, msg.c_str());
}

std::uint8_t* FindUnique(void* module, const char* name, const char* sig)
{
   std::size_t matches = 0;
   std::uint8_t* p = Memory::PatternScanUnique(module, sig, ".text", matches);
   if (!p)
      LogResolveFailure(name, matches);
   return p;
}

void* FindFunctionStartFromAnchor(void* module, const char* name, const char* sig)
{
   std::uint8_t* anchor = FindUnique(module, name, sig);
   if (!anchor)
      return nullptr;

   const uintptr_t anchor_addr = reinterpret_cast<uintptr_t>(anchor);
   DWORD64 image_base = reinterpret_cast<DWORD64>(module);
   const RUNTIME_FUNCTION* rf = RtlLookupFunctionEntry(anchor_addr, &image_base, nullptr);
   if (!rf)
   {
      LogResolveFailure(name, 0);
      return nullptr;
   }

   return reinterpret_cast<void*>(image_base + rf->BeginAddress);
}
}

uintptr_t ResolveGBFRDataOrFallback(uintptr_t resolved_absolute, uintptr_t fallback_rva)
{
   if (resolved_absolute != 0)
      return resolved_absolute;

   const uintptr_t mod_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
   return (mod_base != 0) ? (mod_base + fallback_rva) : 0;
}

void* ResolveGBFRCodeOrFallback(void* resolved_absolute, uintptr_t fallback_rva)
{
   if (resolved_absolute != nullptr)
      return resolved_absolute;

   const uintptr_t mod_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
   return (mod_base != 0) ? reinterpret_cast<void*>(mod_base + fallback_rva) : nullptr;
}

bool ResolveGBFRAddresses()
{
   if (g_resolved_addresses.resolve_attempted)
      return g_resolved_addresses.ready;

   g_resolved_addresses.resolve_attempted = true;

   void* module = GetModuleHandleA(NULL);
   if (!module)
      return false;

   // Resolve hook/function targets.
   if (void* p = FindFunctionStartFromAnchor(module, "InitializeDX11RenderingPipeline", kSigInitDx11Anchor))
      g_resolved_addresses.initialize_dx11_rendering_pipeline = p;

   if (std::uint8_t* p = FindUnique(module, "DispatchRenderPassViewport", kSigDispatchViewport))
      g_resolved_addresses.dispatch_render_pass_viewport = p;

   if (void* p = FindFunctionStartFromAnchor(module, "UIRenderOrchestrator", kSigUIOrchestratorAnchor))
      g_resolved_addresses.ui_render_orchestrator = p;

#ifdef PATCH_JITTER_TABLE_INIT
   if (std::uint8_t* p = FindUnique(module, "TemporalAntiAliasingComponentInit", kSigTAAInit))
      g_resolved_addresses.temporal_aa_component_init = p;
#endif

   if (std::uint8_t* p = FindUnique(module, "JitterCore", kSigJitterCore))
   {
      // Pattern starts at: mov rax, [rip+disp32]
      g_resolved_addresses.jitter_phase_counter = Memory::GetAbsolute64(reinterpret_cast<uintptr_t>(p), 3, 7);
      g_resolved_addresses.jitter_phase_mask_cl_imm = reinterpret_cast<uintptr_t>(p + 11);
      g_resolved_addresses.jitter_phase_mask_eax_imm = reinterpret_cast<uintptr_t>(p + 17);
      g_resolved_addresses.jitter_write_site = reinterpret_cast<void*>(p + 0x30);
      g_resolved_addresses.taa_settings_global = Memory::GetAbsolute64(reinterpret_cast<uintptr_t>(p + 26), 3, 7);
   }

   if (std::uint8_t* p = FindUnique(module, "OutputDimensionCaller", kSigOutputLoadCaller))
   {
      g_resolved_addresses.output_width = Memory::GetAbsolute64(reinterpret_cast<uintptr_t>(p), 3, 7);
      g_resolved_addresses.output_height = Memory::GetAbsolute64(reinterpret_cast<uintptr_t>(p + 7), 3, 7);

      // g_renderWidth/g_renderHeight are contiguous 8 bytes before output globals in current layouts.
      if (g_resolved_addresses.output_width != 0 && g_resolved_addresses.output_height != 0)
      {
         g_resolved_addresses.render_width = g_resolved_addresses.output_width - 8;
         g_resolved_addresses.render_height = g_resolved_addresses.output_height - 8;
      }
   }

   if (std::uint8_t* p = FindUnique(module, "CameraGlobalLoad", kSigCameraGlobalLoad))
   {
      g_resolved_addresses.camera_global = Memory::GetAbsolute64(reinterpret_cast<uintptr_t>(p), 3, 7);
   }

   g_resolved_addresses.ready =
      g_resolved_addresses.initialize_dx11_rendering_pipeline != nullptr &&
      g_resolved_addresses.dispatch_render_pass_viewport != nullptr &&
      g_resolved_addresses.ui_render_orchestrator != nullptr &&
      g_resolved_addresses.jitter_write_site != nullptr;

   LogResolve("InitializeDX11RenderingPipeline", g_resolved_addresses.initialize_dx11_rendering_pipeline);
   LogResolve("DispatchRenderPassViewport", g_resolved_addresses.dispatch_render_pass_viewport);
   LogResolve("UIRenderOrchestrator", g_resolved_addresses.ui_render_orchestrator);
   LogResolve("JitterWrite", g_resolved_addresses.jitter_write_site);
   LogResolve("CameraGlobal", reinterpret_cast<void*>(g_resolved_addresses.camera_global));

   return g_resolved_addresses.ready;
}
