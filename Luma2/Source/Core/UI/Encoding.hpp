#pragma once

#include <cstdint>
#include <optional>

#include <d3dcommon.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <imgui.h>

// How the overlay's colors are written to the swapchain. ImGui's colors are sRGB-encoded (gamma 2.2) values for an SDR buffer: on an
// HDR one they are linearized and scaled to the overlay's paper white, then encoded for its color space, in the pixel shader ImGui's
// backends draw with (1.0 drew its overlay through ReShade's, which does the same).
namespace Luma {
namespace UI {
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

// The color space the game set wins: PQ in any format (FP16 can hold it), scRGB only for FP16. Without one, FP16 is scRGB (DXGI's
// default for it) and anything else is SDR, as 1.0 assumed for 10 bit buffers.
Kind GetKind(DXGI_FORMAT format, std::optional<DXGI_COLOR_SPACE_TYPE> color_space);

Constants GetConstants(const Output& output);

// ImGui's pixel shader encoding for the kind and paper white in its constants, compiled for `target` ("ps_4_0", "ps_5_0") once: a
// change of either is data. Null if it didn't compile.
Microsoft::WRL::ComPtr<ID3DBlob> CompilePixelShader(const char* target);

// Puts `callback` first in every draw list of a rendered frame, so the backend calls it after it set its state up: it binds the
// encoding shader
void InsertCallback(ImDrawData* data, ImDrawCallback callback);
} // namespace Encoding
} // namespace UI
} // namespace Luma
