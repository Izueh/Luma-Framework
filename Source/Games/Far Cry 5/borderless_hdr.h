#pragma once

// Saved opt-in, latched at startup. Does not upgrade SDR resources or shaders.
// Native FC5 must already request its scRGB output (saved hdr=2, valid refresh).
namespace FC5::BorderlessHDR
{
   inline bool configured = false;
   inline bool active = false;
   inline bool initialized = false;
   inline bool test_override = false;
   inline void LoadConfiguration()
   {
      Config::Get(NAME, "FC5BorderlessHDR", configured);
      if (initialized) return;
      wchar_t value[8]{};
      test_override = GetEnvironmentVariableW(L"FC5_TEST_BORDERLESS_HDR", value, 8) == 1 && value[0] == L'1';
      active = configured || test_override;
      initialized = true;
      prevent_fullscreen_state = force_borderless = active;
      Luma::Utils::Log::Write(Luma::Utils::Log::Level::Info, active ?
         "[FC5 HDR] Borderless HDR startup option enabled; native scRGB required" :
         "[FC5 HDR] Borderless HDR startup option disabled");
   }
   inline bool Enabled()
   {
      return active;
   }
   // ReShade's "create_swapchain" (creation and resize), with the values each DXGI function takes
   inline bool OnCreate(DXGI_FORMAT format, UINT samples, DXGI_SWAP_EFFECT* swap_effect, UINT& buffer_count, BOOL* windowed, UINT& flags)
   {
      if (!Enabled() || format != DXGI_FORMAT_R16G16B16A16_FLOAT || samples != 1) return false;
      if (swap_effect) *swap_effect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
      buffer_count = (std::max)(buffer_count, 2u);
      if (windowed) *windowed = TRUE;
      flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
      Luma::Utils::Log::Write(Luma::Utils::Log::Level::Info, "[FC5 HDR] TEST native FP16 flip-discard requested; SDR unchanged");
      return true;
   }
   // ReShade's "init_swapchain" (creation and resize)
   inline void OnInit(IDXGISwapChain* native)
   {
      if (!Enabled() || !native) return;
      DXGI_SWAP_CHAIN_DESC desc{};
      if (FAILED(native->GetDesc(&desc))) return;
      HRESULT result = E_NOINTERFACE;
      com_ptr<IDXGISwapChain3> chain3;
      if (desc.BufferDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT &&
          (desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD || desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL) &&
          SUCCEEDED(native->QueryInterface(IID_PPV_ARGS(&chain3))))
      {
         UINT support = 0;
         result = chain3->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &support);
         if (SUCCEEDED(result) && (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
            result = chain3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
         else if (SUCCEEDED(result)) result = DXGI_ERROR_UNSUPPORTED;
      }
      BOOL fullscreen = FALSE;
      const HRESULT fs = native->GetFullscreenState(&fullscreen, nullptr);
      char line[256];
      std::snprintf(line, sizeof(line), "[FC5 HDR] TEST actual %ux%u format=%u effect=%u buffers=%u windowed=%d fullscreen=%d fsHR=%08X scRGB_HR=%08X",
         desc.BufferDesc.Width, desc.BufferDesc.Height, UINT(desc.BufferDesc.Format), UINT(desc.SwapEffect), desc.BufferCount,
         desc.Windowed, fullscreen, UINT(fs), UINT(result));
      Luma::Utils::Log::Write(Luma::Utils::Log::Level::Info, line);
   }
}
