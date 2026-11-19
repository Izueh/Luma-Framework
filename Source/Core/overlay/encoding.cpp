#include "overlay/encoding.hpp"

#include <array>
#include <cstring>
#include <format>
#include <string>

#include <d3dcompiler.h>

#include "utils/log.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Overlay {
namespace Encoding {
namespace {
// ImGui's pixel shader, then ReShade's imgui_hdr.hlsl conversion
constexpr const char* PIXEL_SHADER = R"(
struct PS_INPUT
{
   float4 pos : SV_POSITION;
   float4 col : COLOR0;
   float2 uv : TEXCOORD0;
};
SamplerState sampler0 : register(s0);
Texture2D texture0 : register(t0);
cbuffer EncodingConstants : register(b1)
{
   uint encoding_kind; // 1 scRGB, 2 PQ
   float paper_white;
};

float3 LinearToPQ(float3 color)
{
   const float m1 = 0.1593017578125;
   const float m2 = 78.84375;
   const float c1 = 0.8359375;
   const float c2 = 18.8515625;
   const float c3 = 18.6875;
   const float3 power = pow(max(color, 0.0), m1);
   return pow((c1 + c2 * power) / (1.0 + c3 * power), m2);
}

float4 main(PS_INPUT input) : SV_Target
{
   const float4 color = input.col * texture0.Sample(sampler0, input.uv);
   // Decoded with sRGB, as Windows shows an SDR swapchain on an HDR display
   const float3 encoded = saturate(color.rgb);
   const float3 linear_color = encoded <= 0.04045 ? encoded / 12.92 : pow((encoded + 0.055) / 1.055, 2.4);
   if (encoding_kind == 1)
      return float4(linear_color * (paper_white / 80.0), color.a);
   const float3x3 BT709_TO_BT2020 = float3x3(
      0.627403914928436279296875, 0.3292830288410186767578125, 0.0433130674064159393310546875,
      0.069097287952899932861328125, 0.9195404052734375, 0.011362315155565738677978515625,
      0.01639143936336040496826171875, 0.08801330626010894775390625, 0.895595252513885498046875);
   return float4(LinearToPQ(mul(BT709_TO_BT2020, linear_color) * (paper_white / 10000.0)), color.a);
}
)";
} // namespace

Kind GetKind(DXGI_FORMAT format, std::optional<DXGI_COLOR_SPACE_TYPE> color_space)
{
   if (color_space == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 || color_space == DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020)
      return Kind::Pq;
   if (format != DXGI_FORMAT_R16G16B16A16_FLOAT)
      return Kind::Sdr;
   return !color_space || color_space == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 ? Kind::ScRgb : Kind::Sdr;
}

Constants GetConstants(const Output& output)
{
   return {uint32_t(output.kind), output.paper_white};
}

ComPtr<ID3DBlob> CompilePixelShader(const char* target)
{
   ComPtr<ID3DBlob> blob;
   ComPtr<ID3DBlob> errors;
   if (FAILED(D3DCompile(PIXEL_SHADER, std::strlen(PIXEL_SHADER), "LumaOverlay", nullptr, nullptr, "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors)))
   {
      Utils::Log::Error("Overlay: the overlay's HDR pixel shader didn't compile: {}", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no message");
      return nullptr;
   }
   return blob;
}

void InsertCallback(ImDrawData* data, ImDrawCallback callback)
{
   for (ImDrawList* list : data->CmdLists)
   {
      ImDrawCmd command;
      command.UserCallback = callback;
      list->CmdBuffer.insert(list->CmdBuffer.begin(), command);
   }
}
} // namespace Encoding
} // namespace Overlay
} // namespace Luma
