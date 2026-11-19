#pragma once

#include <cstdint>
#include <optional>

#include <d3dcommon.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <imgui.h>

// How the overlay's colors are written to the swapchain: SDR as ImGui writes them, scRGB or PQ through a pixel shader
namespace Luma {
namespace Overlay {
namespace Encoding {
enum class Kind : uint8_t
{
   Sdr, // As ImGui writes it
   ScRgb, // Linear BT.709, 1 is 80 nits
   Pq, // PQ BT.2020 (HDR10)
};

struct Output
{
   Kind kind = Kind::Sdr;
   float paper_white = 203.f; // Nits
};

// The encoding shader's constants, at b1 (a cbuffer on D3D11, root constants on D3D12)
struct Constants
{
   uint32_t kind = 0; // Kind
   float paper_white = 0.f;
   uint32_t padding[2] = {};
};

inline constexpr uint32_t CONSTANTS_REGISTER = 1;

// The recorded color space wins, without one FP16 is scRGB and anything else SDR
Kind GetKind(DXGI_FORMAT format, std::optional<DXGI_COLOR_SPACE_TYPE> color_space);

Constants GetConstants(const Output& output);

// ImGui's pixel shader with the encoding, null if it didn't compile
Microsoft::WRL::ComPtr<ID3DBlob> CompilePixelShader(const char* target);

// Puts `callback` first in every draw list, so it runs after the backend set its state up
void InsertCallback(ImDrawData* data, ImDrawCallback callback);
} // namespace Encoding
} // namespace Overlay
} // namespace Luma
