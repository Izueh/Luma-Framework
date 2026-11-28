#pragma once

#if defined(LUMA_USE_D3D12_INTEROP) && LUMA_USE_D3D12_INTEROP

#include <array>
#include <cstdint>
#include <vector>

#include <d3d11_4.h>
#include <d3d12.h>
#include <wrl/client.h>

namespace Luma {
namespace Effects {
namespace SR {
namespace D3D11 {
// Shared per-D3D11-device DX11->DX12 interop context for DX12-only SR libraries (FSR 4, non-Intel XeSS).
namespace D3D12Interop {
constexpr size_t COMMAND_ALLOCATORS_COUNT = 4;

enum class TextureSlot : size_t
{
   Color = 0,
   Depth,
   MotionVectors,
   Exposure,
   Reactive,
   Transparency,
   Output,
   Count,
};

struct SharedTexture
{
   Microsoft::WRL::ComPtr<ID3D11Texture2D> resource_11;
   Microsoft::WRL::ComPtr<ID3D12Resource> resource_12;
   Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav_11;
   UINT width = 0;
   UINT height = 0;
   DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};

struct RetiredTexture
{
   Microsoft::WRL::ComPtr<ID3D11Texture2D> resource_11;
   Microsoft::WRL::ComPtr<ID3D12Resource> resource_12;
   Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav_11;
   UINT64 fence_value = 0;
};

struct Context
{
   bool initialized = false;
   Microsoft::WRL::ComPtr<ID3D11Device> device_11;
   Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
   DXGI_ADAPTER_DESC adapter_desc = {};
   Microsoft::WRL::ComPtr<ID3D12Device> device_12;
   Microsoft::WRL::ComPtr<ID3D12CommandQueue> command_queue;
   std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, COMMAND_ALLOCATORS_COUNT> command_allocators;
   std::array<UINT64, COMMAND_ALLOCATORS_COUNT> command_allocators_fence_values = {};
   size_t command_allocator_index = 0;
   Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list;

   Microsoft::WRL::ComPtr<ID3D12Fence> fence_12;
   Microsoft::WRL::ComPtr<ID3D11Fence> fence_11;
   UINT64 fence_value = 0;
   HANDLE fence_event = nullptr;

   std::array<SharedTexture, size_t(TextureSlot::Count)> textures;
   std::vector<RetiredTexture> retired_textures;

   Microsoft::WRL::ComPtr<ID3D11Resource> depth_source;
   Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depth_source_srv;

   uint32_t logged_errors = 0;
   bool poisoned = false;
};

bool Init(Context& context, ID3D11Device* device, IDXGIAdapter* adapter = nullptr);
bool WaitForIdle(Context& context);
void Deinit(Context& context);

struct SharedTextureOptions
{
   bool is_depth = false;
   bool unordered_access = false;
};

bool UpdateSharedTextureFrom(Context& context, SharedTexture& texture, ID3D11Resource* source, SharedTextureOptions options);
bool CopyDepth(Context& context, SharedTexture& texture, ID3D11DeviceContext* device_context, ID3D11Resource* source);

ID3D12GraphicsCommandList* BeginCommandList(Context& context, ID3D11DeviceContext* device_context);
bool ExecuteCommandList(Context& context, ID3D11DeviceContext* device_context);
} // namespace D3D12Interop
} // namespace D3D11
} // namespace SR
} // namespace Effects
} // namespace Luma

#endif
