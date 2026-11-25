#include "Effects/SR/D3D11/D3D12Interop.hpp"

#if defined(LUMA_USE_D3D12_INTEROP) && LUMA_USE_D3D12_INTEROP

#include <algorithm>
#include <format>

#include "Hooks/HookScope.hpp"
#include "Log/Log.hpp"
#include "Resources/D3D11/D3D11ResourceDatabase.hpp"
#include "Utils/DXGI/DXGIFormat.hpp"
#include "Utils/Hash.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Effects {
namespace SR {
namespace D3D11 {
namespace D3D12Interop {
namespace {
constexpr ULONGLONG FENCE_TIMEOUT_MS = 2000;
constexpr uint64_t COPY_DEPTH_CS_HASH = Utils::Hash::Name("Luma_CopyDepth_CS");

void LogError(Context& context, const std::string& message)
{
   if (context.logged_errors++ < 10)
      Log::Error("D3D12Interop: {}", message);
}

bool WaitForFence(Context& context, UINT64 value)
{
   if (!context.fence_12 || value == 0)
      return true;
   const ULONGLONG deadline = GetTickCount64() + FENCE_TIMEOUT_MS;
   while (context.fence_12->GetCompletedValue() < value)
   {
      if (FAILED(context.device_12->GetDeviceRemovedReason()))
         return true;
      const ULONGLONG now = GetTickCount64();
      if (now >= deadline || FAILED(context.fence_12->SetEventOnCompletion(value, context.fence_event)))
      {
         LogError(context, std::format("Timed out waiting for DX12 GPU work (fence {}, completed {})", value, context.fence_12->GetCompletedValue()));
         return false;
      }
      WaitForSingleObject(context.fence_event, DWORD(deadline - now));
   }
   return true;
}

void DrainRetiredTextures(Context& context)
{
   if (context.retired_textures.empty() || !context.fence_12)
      return;
   const UINT64 completed = context.fence_12->GetCompletedValue();
   std::erase_if(context.retired_textures, [completed](const RetiredTexture& entry) { return entry.fence_value <= completed; });
}

bool CreateSharedTexture(Context& context, SharedTexture& texture, UINT width, UINT height, DXGI_FORMAT format, bool unordered_access)
{
   if (texture.resource_11 || texture.resource_12)
   {
      context.retired_textures.push_back({std::move(texture.resource_11), std::move(texture.resource_12), std::move(texture.uav_11), context.fence_value});
   }
   texture = {};
   D3D11_TEXTURE2D_DESC desc = {};
   desc.Width = width;
   desc.Height = height;
   desc.MipLevels = 1;
   desc.ArraySize = 1;
   desc.Format = format;
   desc.SampleDesc.Count = 1;
   desc.Usage = D3D11_USAGE_DEFAULT;
   desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (unordered_access ? D3D11_BIND_UNORDERED_ACCESS : 0);
   desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

   Hooks::Scope::Guard scope;
   ComPtr<IDXGIResource> dxgi_resource;
   HANDLE shared_handle = nullptr;
   HRESULT hr = context.device_11->CreateTexture2D(&desc, nullptr, &texture.resource_11);
   if (SUCCEEDED(hr))
      hr = texture.resource_11.As(&dxgi_resource);
   if (SUCCEEDED(hr))
      hr = dxgi_resource->GetSharedHandle(&shared_handle);
   if (SUCCEEDED(hr))
      hr = context.device_12->OpenSharedHandle(shared_handle, IID_PPV_ARGS(&texture.resource_12));
   if (SUCCEEDED(hr) && unordered_access)
      hr = context.device_11->CreateUnorderedAccessView(texture.resource_11.Get(), nullptr, &texture.uav_11);
   if (FAILED(hr))
   {
      LogError(context, std::format("Failed to share DX11 texture with DX12 ({}x{}, format {}), hr = {:#010x}", width, height, int(format), uint32_t(hr)));
      texture = {};
      return false;
   }
   texture.width = width;
   texture.height = height;
   texture.format = format;
   return true;
}

DXGI_FORMAT ToDefaultTyped(DXGI_FORMAT format)
{
   return Utils::Format::DXGI::GetDefaultTyped(format);
}
} // namespace

bool Init(Context& context, ID3D11Device* device, IDXGIAdapter* adapter)
{
   if (context.initialized)
      return !context.poisoned;
   if (!device)
      return false;

   Hooks::Scope::Guard scope;
   context.device_11 = device;
   ComPtr<ID3D11Device5> device_11_5;
   if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device_11_5))))
   {
      Log::Error("D3D12Interop: ID3D11Device5 is not supported, cannot share fences");
      return false;
   }

   context.adapter = adapter;
   ComPtr<IDXGIDevice> dxgi_device;
   if (!context.adapter && SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgi_device))))
      dxgi_device->GetAdapter(&context.adapter);

   HMODULE d3d12_module = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
   const auto d3d12_create_device = d3d12_module ? reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(d3d12_module, "D3D12CreateDevice")) : nullptr;
   if (!context.adapter || FAILED(context.adapter->GetDesc(&context.adapter_desc)) || !d3d12_create_device)
   {
      Log::Error("D3D12Interop: failed to retrieve DX11 adapter or load System32 d3d12.dll");
      return false;
   }

   const HRESULT create_hr = d3d12_create_device(context.adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&context.device_12));
   if (FAILED(create_hr))
   {
      Log::Error("D3D12Interop: D3D12CreateDevice failed (0x{:08X})", uint32_t(create_hr));
      return false;
   }
   const LUID device_12_luid = context.device_12->GetAdapterLuid();
   if (device_12_luid.LowPart != context.adapter_desc.AdapterLuid.LowPart || device_12_luid.HighPart != context.adapter_desc.AdapterLuid.HighPart)
   {
      Log::Error("D3D12Interop: DX12 device adapter does not match DX11 adapter");
      return false;
   }

   const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
   HRESULT hr = context.device_12->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&context.command_queue));
   for (auto& allocator : context.command_allocators)
   {
      if (SUCCEEDED(hr))
         hr = context.device_12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
   }
   if (SUCCEEDED(hr))
      hr = context.device_12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, context.command_allocators[0].Get(), nullptr, IID_PPV_ARGS(&context.command_list));
   if (SUCCEEDED(hr))
      hr = context.command_list->Close();
   HANDLE fence_handle = nullptr;
   if (SUCCEEDED(hr))
      hr = context.device_12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&context.fence_12));
   if (SUCCEEDED(hr))
      hr = context.device_12->CreateSharedHandle(context.fence_12.Get(), nullptr, GENERIC_ALL, nullptr, &fence_handle);
   if (SUCCEEDED(hr))
   {
      hr = device_11_5->OpenSharedFence(fence_handle, IID_PPV_ARGS(&context.fence_11));
      CloseHandle(fence_handle);
   }
   if (SUCCEEDED(hr))
   {
      context.fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
      hr = context.fence_event ? S_OK : HRESULT_FROM_WIN32(GetLastError());
   }
   if (FAILED(hr))
   {
      Log::Error("D3D12Interop: failed to create DX12 queue/command list/shared fence (0x{:08X})", uint32_t(hr));
      Deinit(context);
      return false;
   }

   SharedTexture test_texture;
   if (!CreateSharedTexture(context, test_texture, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, true))
   {
      Deinit(context);
      return false;
   }
   context.retired_textures.clear();
   context.initialized = true;
   Log::Info("D3D12Interop: initialized");
   return true;
}

bool WaitForIdle(Context& context)
{
   return !context.poisoned && (!context.fence_12 || WaitForFence(context, context.fence_value));
}

void Deinit(Context& context)
{
   if (context.fence_event)
      CloseHandle(context.fence_event);
   context = {};
}

bool UpdateSharedTexture(Context& context, SharedTexture& texture, UINT width, UINT height, DXGI_FORMAT format, bool unordered_access)
{
   if (texture.resource_11 && texture.width == width && texture.height == height && texture.format == format && (texture.uav_11 != nullptr) == unordered_access)
      return true;
   return CreateSharedTexture(context, texture, width, height, format, unordered_access);
}

bool UpdateSharedTextureFrom(Context& context, SharedTexture& texture, ID3D11Resource* source, bool is_depth, bool unordered_access)
{
   ComPtr<ID3D11Texture2D> source_texture;
   if (FAILED(source->QueryInterface(IID_PPV_ARGS(&source_texture))))
   {
      LogError(context, "Input/output resource is not a 2D texture");
      return false;
   }
   D3D11_TEXTURE2D_DESC source_desc = {};
   source_texture->GetDesc(&source_desc);
   const bool is_srgb = Utils::Format::DXGI::GetEncoding(source_desc.Format) == Utils::Format::DXGI::Encoding::UnormSrgb;
   if (!is_depth && (source_desc.MipLevels != 1 || source_desc.ArraySize != 1 || source_desc.SampleDesc.Count != 1 || is_srgb))
   {
      LogError(context, std::format("Unsupported texture (mips {}, array {}, samples {}, format {})", source_desc.MipLevels, source_desc.ArraySize,
         source_desc.SampleDesc.Count, int(source_desc.Format)));
      return false;
   }
   const DXGI_FORMAT format = is_depth ? DXGI_FORMAT_R32_FLOAT : ToDefaultTyped(source_desc.Format);
   return UpdateSharedTexture(context, texture, source_desc.Width, source_desc.Height, format, unordered_access);
}

bool CopyDepth(Context& context, SharedTexture& texture, ID3D11DeviceContext* device_context, ID3D11Resource* source)
{
   const Shaders::D3D11::Live* live_cs = Resources::Database::D3D11::GetShader(context.device_11.Get(), COPY_DEPTH_CS_HASH);
   if (!live_cs || !live_cs->object)
      return false;
   auto* depth_copy_cs = static_cast<ID3D11ComputeShader*>(live_cs->object.Get());

   Hooks::Scope::Guard scope;
   if (!context.depth_source_srv || context.depth_source.Get() != source)
   {
      context.depth_source_srv.Reset();
      context.depth_source = source;
      D3D11_TEXTURE2D_DESC desc = {};
      static_cast<ID3D11Texture2D*>(source)->GetDesc(&desc);
      D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
      srv_desc.Format = desc.Format == DXGI_FORMAT_R16_TYPELESS ? DXGI_FORMAT_R16_UNORM : ToDefaultTyped(desc.Format);
      srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D.MipLevels = 1;
      HRESULT hr = E_INVALIDARG;
      if (desc.SampleDesc.Count == 1)
         hr = context.device_11->CreateShaderResourceView(source, &srv_desc, &context.depth_source_srv);
      if (FAILED(hr))
      {
         LogError(context, std::format("Failed to create depth SRV (format {}, samples {}), hr = {:#010x}", int(desc.Format), desc.SampleDesc.Count, uint32_t(hr)));
         context.depth_source.Reset();
         return false;
      }
   }

   device_context->OMSetRenderTargets(0, nullptr, nullptr);
   ID3D11ShaderResourceView* const depth_srv = context.depth_source_srv.Get();
   ID3D11UnorderedAccessView* const depth_uav = texture.uav_11.Get();
   device_context->CSSetShader(depth_copy_cs, nullptr, 0);
   device_context->CSSetShaderResources(0, 1, &depth_srv);
   device_context->CSSetUnorderedAccessViews(0, 1, &depth_uav, nullptr);
   device_context->Dispatch((texture.width + 7) / 8, (texture.height + 7) / 8, 1);
   return true;
}

ID3D12GraphicsCommandList* BeginCommandList(Context& context, ID3D11DeviceContext* device_context)
{
   Hooks::Scope::Guard scope;
   DrainRetiredTextures(context);

   ComPtr<ID3D11DeviceContext4> device_context_4;
   HRESULT hr = device_context->QueryInterface(IID_PPV_ARGS(&device_context_4));
   if (SUCCEEDED(hr))
      hr = device_context_4->Signal(context.fence_11.Get(), context.fence_value + 1);
   if (FAILED(hr))
   {
      LogError(context, std::format("Failed to signal DX11 fence (0x{:08X})", uint32_t(hr)));
      return nullptr;
   }
   context.fence_value++;
   device_context->Flush();

   context.command_allocator_index = (context.command_allocator_index + 1) % COMMAND_ALLOCATORS_COUNT;
   ID3D12CommandAllocator* const allocator = context.command_allocators[context.command_allocator_index].Get();
   hr = WaitForFence(context, context.command_allocators_fence_values[context.command_allocator_index]) ? S_OK : HRESULT_FROM_WIN32(WAIT_TIMEOUT);
   if (SUCCEEDED(hr))
      hr = allocator->Reset();
   if (SUCCEEDED(hr))
      hr = context.command_list->Reset(allocator, nullptr);
   if (FAILED(hr))
   {
      LogError(context, std::format("Failed to reset DX12 command list (0x{:08X})", uint32_t(hr)));
      return nullptr;
   }
   return context.command_list.Get();
}

bool ExecuteCommandList(Context& context, ID3D11DeviceContext* device_context)
{
   Hooks::Scope::Guard scope;
   const UINT64 inputs_ready = context.fence_value;
   const UINT64 output_ready = inputs_ready + 1;
   HRESULT hr = context.command_list->Close();
   if (SUCCEEDED(hr))
      hr = context.command_queue->Wait(context.fence_12.Get(), inputs_ready);
   if (FAILED(hr))
   {
      LogError(context, std::format("Failed to submit DX12 work (0x{:08X})", uint32_t(hr)));
      return false;
   }
   ID3D12CommandList* const lists[] = {context.command_list.Get()};
   context.command_queue->ExecuteCommandLists(1, lists);
   hr = context.command_queue->Signal(context.fence_12.Get(), output_ready);
   ComPtr<ID3D11DeviceContext4> device_context_4;
   if (SUCCEEDED(hr))
   {
      context.fence_value = output_ready;
      context.command_allocators_fence_values[context.command_allocator_index] = output_ready;
      hr = device_context->QueryInterface(IID_PPV_ARGS(&device_context_4));
   }
   if (SUCCEEDED(hr))
      hr = device_context_4->Wait(context.fence_11.Get(), output_ready);
   if (FAILED(hr))
   {
      LogError(context, std::format("Failed to sync DX12 work with DX11 (0x{:08X})", uint32_t(hr)));
      context.poisoned = true;
      return false;
   }
   return true;
}
} // namespace D3D12Interop
} // namespace D3D11
} // namespace SR
} // namespace Effects
} // namespace Luma

#endif
