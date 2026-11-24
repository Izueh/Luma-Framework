#include "Trace/DebugDraw.hpp"

#if DEVELOPMENT
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <format>
#include <mutex>
#include <string_view>

#include <wrl/client.h>

#include "Game/Game.hpp"
#include "Resources/D3D11/D3D11Mirrors.hpp"
#include "Resources/D3D12/D3D12Mirrors.hpp"
#include "Shaders/D3D11/D3D11Variants.hpp"
#include "Shaders/ShaderDatabase.hpp"
#include "Utils/DXGI/DXGIFormat.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Trace {
namespace DebugDraw {
namespace {
constexpr uint32_t DIM_BIT_MS = 1u << 11;
constexpr uint32_t DIM_BIT_ARRAY = 1u << 12;
constexpr uint32_t DIM_BIT_2D = 1u << 13;
constexpr uint32_t DIM_BIT_3D = 1u << 14;

// Matches Luma_DisplayComposition.hlsl t2..t8
enum class Slot : uint32_t
{
   Texture2D = 2,
   Texture2DMS = 3,
   Texture2DArray = 4,
   Texture2DMSArray = 5,
   Texture3D = 6,
   Texture1D = 7,
   Texture1DArray = 8,
};

struct D3D11Snapshot
{
   ID3D11Device* device = nullptr;
   ComPtr<ID3D11Resource> texture;
   ComPtr<ID3D11ShaderResourceView> srv;
   Slot slot = Slot::Texture2D;
   uint32_t dim_bits = DIM_BIT_2D;
   D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
   uint32_t width = 0;
   uint32_t height = 0;
   uint32_t depth = 0;
   uint32_t mips = 0;
   uint32_t array_size = 0;
   uint32_t sample_count = 1;
   DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};

struct D3D12Snapshot
{
   ID3D12Device* device = nullptr;
   ComPtr<ID3D12Resource> texture;
   Slot slot = Slot::Texture2D;
   uint32_t dim_bits = DIM_BIT_2D;
   D3D12_RESOURCE_DESC desc = {};
   DXGI_FORMAT srv_format = DXGI_FORMAT_UNKNOWN;
};

std::atomic<bool> active = false;
std::atomic<bool> pass_active = false;
std::atomic<uintptr_t> target_address = 0;
std::atomic<uint32_t> frame_instance_counter = 0;
std::mutex mutex;
std::string target_id;
std::optional<PassTarget> pass_target;
ViewMode last_view_mode = ViewMode::RenderTarget;
uint32_t last_view_slot = 0;
Options current_options;
D3D11Snapshot d3d11_snapshot;
D3D12Snapshot d3d12_snapshot;

std::string_view GetStageName(ShaderStage stage)
{
   constexpr std::string_view NAMES[] = {"VS", "HS", "DS", "GS", "PS", "CS"};
   return NAMES[size_t(stage)];
}

std::string_view GetModeName(ViewMode mode)
{
   switch (mode)
   {
   case ViewMode::RenderTarget: return "RTV";
   case ViewMode::ShaderResource: return "SRV";
   case ViewMode::UnorderedAccess: return "UAV";
   case ViewMode::Depth: return "DSV";
   default: return "Resource";
   }
}

uintptr_t ParseAddress(std::string_view text)
{
   if (text.starts_with("0x") || text.starts_with("0X"))
      text.remove_prefix(2);
   uintptr_t value = 0;
   const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
   return ec == std::errc() && ptr == text.data() + text.size() ? value : 0;
}

DXGI_FORMAT GetReadableSrvFormat(DXGI_FORMAT format)
{
   switch (format)
   {
   case DXGI_FORMAT_D32_FLOAT:
   case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
   case DXGI_FORMAT_D16_UNORM:
   case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_UNORM;
   case DXGI_FORMAT_D24_UNORM_S8_UINT:
   case DXGI_FORMAT_R24G8_TYPELESS:
   case DXGI_FORMAT_X24_TYPELESS_G8_UINT: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
   case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
   case DXGI_FORMAT_R32G8X24_TYPELESS:
   case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
   default: break;
   }
   const DXGI_FORMAT typeless = Utils::Format::DXGI::GetTypeless(format);
   if (typeless == format && typeless != DXGI_FORMAT_UNKNOWN)
      return Utils::Format::DXGI::GetDefaultTyped(typeless);
   return format;
}

bool MatchesD3D11Target(ID3D11Resource* resource, uintptr_t address)
{
   if (!resource || !address)
      return false;
   if (reinterpret_cast<uintptr_t>(resource) == address)
      return true;
   if (ID3D11Resource* original = Resources::Mirrors::D3D11::FindOriginal(resource))
      return reinterpret_cast<uintptr_t>(original) == address;
   if (ID3D11Resource* mirror = Resources::Mirrors::D3D11::FindMirror(resource))
      return reinterpret_cast<uintptr_t>(mirror) == address;
   return false;
}

bool MatchesD3D12Target(ID3D12Resource* resource, uintptr_t address)
{
   if (!resource || !address)
      return false;
   if (reinterpret_cast<uintptr_t>(resource) == address)
      return true;
   if (ID3D12Resource* original = Resources::Mirrors::D3D12::FindOriginal(resource))
      return reinterpret_cast<uintptr_t>(original) == address;
   if (ID3D12Resource* mirror = Resources::Mirrors::D3D12::FindMirror(resource))
      return reinterpret_cast<uintptr_t>(mirror) == address;
   return false;
}
} // namespace

bool IsActive()
{
   return active.load(std::memory_order_relaxed);
}

void SetTarget(std::string resource_id)
{
   const uintptr_t address = ParseAddress(resource_id);
   std::scoped_lock lock(mutex);
   pass_target.reset();
   pass_active.store(false, std::memory_order_release);
   frame_instance_counter.store(0, std::memory_order_relaxed);
   target_id = address ? std::move(resource_id) : std::string();
   target_address.store(address, std::memory_order_release);
   active.store(address != 0, std::memory_order_release);
   d3d11_snapshot = {};
   d3d12_snapshot = {};
}

std::string GetTarget()
{
   std::scoped_lock lock(mutex);
   return target_id;
}

void SetPassTarget(PassTarget target)
{
   std::scoped_lock lock(mutex);
   const bool prev_depth = pass_target && pass_target->mode == ViewMode::Depth;
   const bool next_depth = target.mode == ViewMode::Depth;
   if (next_depth)
   {
      current_options.red_only = true;
      current_options.mip_level = 0;
   }
   else
   {
      if (prev_depth)
         current_options.red_only = false;
      current_options.mip_level = int32_t(target.mip);
   }
   last_view_mode = target.mode;
   last_view_slot = target.slot;
   target_id.clear();
   target_address.store(0, std::memory_order_release);
   pass_target = std::move(target);
   frame_instance_counter.store(0, std::memory_order_relaxed);
   pass_active.store(true, std::memory_order_release);
   active.store(true, std::memory_order_release);
   d3d11_snapshot = {};
   d3d12_snapshot = {};
}

std::optional<PassTarget> GetPassTarget()
{
   std::scoped_lock lock(mutex);
   return pass_target;
}

bool IsPassTarget(const PassTarget& target)
{
   if (!pass_active.load(std::memory_order_relaxed))
      return false;
   std::scoped_lock lock(mutex);
   return pass_target && *pass_target == target;
}

bool IsSamePass(const PassTarget& target)
{
   if (!pass_active.load(std::memory_order_relaxed))
      return false;
   std::scoped_lock lock(mutex);
   return pass_target && pass_target->SamePass(target);
}

ViewMode GetLastViewMode()
{
   std::scoped_lock lock(mutex);
   return last_view_mode;
}

uint32_t GetLastViewSlot()
{
   std::scoped_lock lock(mutex);
   return last_view_slot;
}

std::string DescribeTarget()
{
   std::scoped_lock lock(mutex);
   if (pass_target)
   {
      const std::string view = pass_target->mode == ViewMode::Depth ? "DSV" : std::format("{}{}", GetModeName(pass_target->mode), pass_target->slot);
      if (!pass_target->luma_pass.empty())
         return std::format("{} #{} ({})", pass_target->luma_pass, pass_target->instance, view);
      return std::format("{} 0x{:08X} #{} ({})", GetStageName(pass_target->stage), pass_target->hash, pass_target->instance, view);
   }
   if (!target_id.empty())
      return std::format("texture {}", target_id);
   return {};
}

void Clear()
{
   std::scoped_lock lock(mutex);
   target_id.clear();
   target_address.store(0, std::memory_order_release);
   pass_target.reset();
   pass_active.store(false, std::memory_order_release);
   frame_instance_counter.store(0, std::memory_order_relaxed);
   active.store(false, std::memory_order_release);
   d3d11_snapshot = {};
   d3d12_snapshot = {};
}

void OnPresent()
{
   frame_instance_counter.store(0, std::memory_order_relaxed);
}

Options GetOptions()
{
   std::scoped_lock lock(mutex);
   return current_options;
}

void SetOptions(const Options& options)
{
   std::scoped_lock lock(mutex);
   current_options = options;
}

uint32_t PackFlags(const Options& options, uint32_t dimension_bits)
{
   uint32_t flags = dimension_bits;
   if (options.fullscreen) flags |= (1u << 0);
   if (options.render_resolution_scale) flags |= (1u << 1);
   if (options.show_alpha) flags |= (1u << 2);
   if (options.premultiply_alpha) flags |= (1u << 3);
   if (options.invert_colors) flags |= (1u << 4);
   if (options.linear_to_gamma) flags |= (1u << 5);
   if (options.gamma_to_linear) flags |= (1u << 6);
   if (options.flip_y) flags |= (1u << 7);
   if (options.do_saturate) flags |= (1u << 8);
   if (options.red_only) flags |= (1u << 9);
   if (options.background_passthrough) flags |= (1u << 10);
   if (options.do_abs) flags |= (1u << 15);
   if (options.zoom_4x) flags |= (1u << 16);
   if (options.bilinear) flags |= (1u << 17);
   if (options.tonemap) flags |= (1u << 18);
   if (options.srgb) flags |= (1u << 19);
   if (options.uv_to_pixel_space) flags |= (1u << 20);
   if (options.denormalize) flags |= (1u << 21);
   return flags;
}

namespace {
void SnapshotD3D11Resource(ID3D11DeviceContext* context, ID3D11Resource* resource)
{
   if (!context || !resource)
      return;
   D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
   resource->GetType(&dim);
   if (dim == D3D11_RESOURCE_DIMENSION_BUFFER || dim == D3D11_RESOURCE_DIMENSION_UNKNOWN)
      return;
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   if (!device)
      return;

   std::scoped_lock lock(mutex);
   if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
   {
      ComPtr<ID3D11Texture2D> src;
      if (FAILED(resource->QueryInterface(IID_PPV_ARGS(&src))))
         return;
      D3D11_TEXTURE2D_DESC desc = {};
      src->GetDesc(&desc);
      const DXGI_FORMAT srv_format = GetReadableSrvFormat(desc.Format);
      if (srv_format == DXGI_FORMAT_UNKNOWN)
         return;
      const bool needs_new = !d3d11_snapshot.texture || d3d11_snapshot.device != device.Get() || d3d11_snapshot.dimension != dim ||
         d3d11_snapshot.width != desc.Width || d3d11_snapshot.height != desc.Height || d3d11_snapshot.mips != desc.MipLevels ||
         d3d11_snapshot.array_size != desc.ArraySize || d3d11_snapshot.sample_count != desc.SampleDesc.Count || d3d11_snapshot.format != desc.Format;
      if (needs_new)
      {
         D3D11_TEXTURE2D_DESC copy_desc = desc;
         copy_desc.Usage = D3D11_USAGE_DEFAULT;
         copy_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
         copy_desc.CPUAccessFlags = 0;
         copy_desc.MiscFlags &= D3D11_RESOURCE_MISC_TEXTURECUBE;
         if (const DXGI_FORMAT typeless = Utils::Format::DXGI::GetTypeless(desc.Format); typeless != DXGI_FORMAT_UNKNOWN)
            copy_desc.Format = typeless;
         ComPtr<ID3D11Texture2D> copy;
         if (FAILED(device->CreateTexture2D(&copy_desc, nullptr, &copy)))
            return;
         D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
         srv_desc.Format = srv_format;
         const bool ms = desc.SampleDesc.Count > 1;
         const bool arr = desc.ArraySize > 1;
         if (ms && arr)
         {
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY;
            srv_desc.Texture2DMSArray = {0, desc.ArraySize};
            d3d11_snapshot.slot = Slot::Texture2DMSArray;
            d3d11_snapshot.dim_bits = DIM_BIT_2D | DIM_BIT_MS | DIM_BIT_ARRAY;
         }
         else if (ms)
         {
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
            d3d11_snapshot.slot = Slot::Texture2DMS;
            d3d11_snapshot.dim_bits = DIM_BIT_2D | DIM_BIT_MS;
         }
         else if (arr)
         {
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            srv_desc.Texture2DArray = {0, desc.MipLevels, 0, desc.ArraySize};
            d3d11_snapshot.slot = Slot::Texture2DArray;
            d3d11_snapshot.dim_bits = DIM_BIT_2D | DIM_BIT_ARRAY;
         }
         else
         {
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Texture2D = {0, desc.MipLevels};
            d3d11_snapshot.slot = Slot::Texture2D;
            d3d11_snapshot.dim_bits = DIM_BIT_2D;
         }
         ComPtr<ID3D11ShaderResourceView> srv;
         if (FAILED(device->CreateShaderResourceView(copy.Get(), &srv_desc, &srv)))
            return;
         d3d11_snapshot.device = device.Get();
         d3d11_snapshot.texture = copy;
         d3d11_snapshot.srv = srv;
         d3d11_snapshot.dimension = dim;
         d3d11_snapshot.width = desc.Width;
         d3d11_snapshot.height = desc.Height;
         d3d11_snapshot.depth = 1;
         d3d11_snapshot.mips = desc.MipLevels;
         d3d11_snapshot.array_size = desc.ArraySize;
         d3d11_snapshot.sample_count = desc.SampleDesc.Count;
         d3d11_snapshot.format = desc.Format;
      }
      context->CopyResource(d3d11_snapshot.texture.Get(), resource);
   }
   else if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE3D)
   {
      ComPtr<ID3D11Texture3D> src;
      if (FAILED(resource->QueryInterface(IID_PPV_ARGS(&src))))
         return;
      D3D11_TEXTURE3D_DESC desc = {};
      src->GetDesc(&desc);
      const DXGI_FORMAT srv_format = GetReadableSrvFormat(desc.Format);
      if (srv_format == DXGI_FORMAT_UNKNOWN)
         return;
      const bool needs_new = !d3d11_snapshot.texture || d3d11_snapshot.device != device.Get() || d3d11_snapshot.dimension != dim ||
         d3d11_snapshot.width != desc.Width || d3d11_snapshot.height != desc.Height || d3d11_snapshot.depth != desc.Depth ||
         d3d11_snapshot.mips != desc.MipLevels || d3d11_snapshot.format != desc.Format;
      if (needs_new)
      {
         D3D11_TEXTURE3D_DESC copy_desc = desc;
         copy_desc.Usage = D3D11_USAGE_DEFAULT;
         copy_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
         copy_desc.CPUAccessFlags = 0;
         copy_desc.MiscFlags = 0;
         ComPtr<ID3D11Texture3D> copy;
         if (FAILED(device->CreateTexture3D(&copy_desc, nullptr, &copy)))
            return;
         D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {.Format = srv_format, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D, .Texture3D = {0, desc.MipLevels}};
         ComPtr<ID3D11ShaderResourceView> srv;
         if (FAILED(device->CreateShaderResourceView(copy.Get(), &srv_desc, &srv)))
            return;
         d3d11_snapshot.device = device.Get();
         d3d11_snapshot.texture = copy;
         d3d11_snapshot.srv = srv;
         d3d11_snapshot.slot = Slot::Texture3D;
         d3d11_snapshot.dim_bits = DIM_BIT_3D;
         d3d11_snapshot.dimension = dim;
         d3d11_snapshot.width = desc.Width;
         d3d11_snapshot.height = desc.Height;
         d3d11_snapshot.depth = desc.Depth;
         d3d11_snapshot.mips = desc.MipLevels;
         d3d11_snapshot.array_size = 1;
         d3d11_snapshot.sample_count = 1;
         d3d11_snapshot.format = desc.Format;
      }
      context->CopyResource(d3d11_snapshot.texture.Get(), resource);
   }
   else if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE1D)
   {
      ComPtr<ID3D11Texture1D> src;
      if (FAILED(resource->QueryInterface(IID_PPV_ARGS(&src))))
         return;
      D3D11_TEXTURE1D_DESC desc = {};
      src->GetDesc(&desc);
      const DXGI_FORMAT srv_format = GetReadableSrvFormat(desc.Format);
      if (srv_format == DXGI_FORMAT_UNKNOWN)
         return;
      const bool needs_new = !d3d11_snapshot.texture || d3d11_snapshot.device != device.Get() || d3d11_snapshot.dimension != dim ||
         d3d11_snapshot.width != desc.Width || d3d11_snapshot.mips != desc.MipLevels || d3d11_snapshot.array_size != desc.ArraySize ||
         d3d11_snapshot.format != desc.Format;
      if (needs_new)
      {
         D3D11_TEXTURE1D_DESC copy_desc = desc;
         copy_desc.Usage = D3D11_USAGE_DEFAULT;
         copy_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
         copy_desc.CPUAccessFlags = 0;
         copy_desc.MiscFlags = 0;
         ComPtr<ID3D11Texture1D> copy;
         if (FAILED(device->CreateTexture1D(&copy_desc, nullptr, &copy)))
            return;
         D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {.Format = srv_format};
         if (desc.ArraySize > 1)
         {
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1DARRAY;
            srv_desc.Texture1DArray = {0, desc.MipLevels, 0, desc.ArraySize};
            d3d11_snapshot.slot = Slot::Texture1DArray;
            d3d11_snapshot.dim_bits = DIM_BIT_ARRAY;
         }
         else
         {
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D;
            srv_desc.Texture1D = {0, desc.MipLevels};
            d3d11_snapshot.slot = Slot::Texture1D;
            d3d11_snapshot.dim_bits = 0;
         }
         ComPtr<ID3D11ShaderResourceView> srv;
         if (FAILED(device->CreateShaderResourceView(copy.Get(), &srv_desc, &srv)))
            return;
         d3d11_snapshot.device = device.Get();
         d3d11_snapshot.texture = copy;
         d3d11_snapshot.srv = srv;
         d3d11_snapshot.dimension = dim;
         d3d11_snapshot.width = desc.Width;
         d3d11_snapshot.height = 1;
         d3d11_snapshot.depth = 1;
         d3d11_snapshot.mips = desc.MipLevels;
         d3d11_snapshot.array_size = desc.ArraySize;
         d3d11_snapshot.sample_count = 1;
         d3d11_snapshot.format = desc.Format;
      }
      context->CopyResource(d3d11_snapshot.texture.Get(), resource);
   }
}

void CaptureViewD3D11(ID3D11DeviceContext* context, const PassTarget& target, bool is_compute)
{
   if (!context)
      return;
   ComPtr<ID3D11Resource> resource;
   switch (target.mode)
   {
   case ViewMode::RenderTarget:
   {
      if (target.slot >= D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT)
         return;
      std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rtvs = {};
      context->OMGetRenderTargets(UINT(rtvs.size()), rtvs.data(), nullptr);
      if (rtvs[target.slot])
         rtvs[target.slot]->GetResource(&resource);
      for (ID3D11RenderTargetView* rtv : rtvs)
      {
         if (rtv)
            rtv->Release();
      }
      break;
   }
   case ViewMode::Depth:
   {
      ComPtr<ID3D11DepthStencilView> dsv;
      context->OMGetRenderTargets(0, nullptr, &dsv);
      if (dsv)
         dsv->GetResource(&resource);
      break;
   }
   case ViewMode::ShaderResource:
   {
      if (target.slot >= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT)
         return;
      ComPtr<ID3D11ShaderResourceView> srv;
      switch (target.stage)
      {
      case ShaderStage::Vertex: context->VSGetShaderResources(target.slot, 1, &srv); break;
      case ShaderStage::Hull: context->HSGetShaderResources(target.slot, 1, &srv); break;
      case ShaderStage::Domain: context->DSGetShaderResources(target.slot, 1, &srv); break;
      case ShaderStage::Geometry: context->GSGetShaderResources(target.slot, 1, &srv); break;
      case ShaderStage::Compute: context->CSGetShaderResources(target.slot, 1, &srv); break;
      case ShaderStage::Pixel:
      default: context->PSGetShaderResources(target.slot, 1, &srv); break;
      }
      if (srv)
         srv->GetResource(&resource);
      break;
   }
   case ViewMode::UnorderedAccess:
   {
      if (target.slot >= D3D11_1_UAV_SLOT_COUNT)
         return;
      if (is_compute)
      {
         ComPtr<ID3D11UnorderedAccessView> uav;
         context->CSGetUnorderedAccessViews(target.slot, 1, &uav);
         if (uav)
            uav->GetResource(&resource);
      }
      else
      {
         std::array<ID3D11UnorderedAccessView*, D3D11_1_UAV_SLOT_COUNT> uavs = {};
         context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, UINT(uavs.size()), uavs.data());
         if (uavs[target.slot])
            uavs[target.slot]->GetResource(&resource);
         for (ID3D11UnorderedAccessView* uav : uavs)
         {
            if (uav)
               uav->Release();
         }
      }
      break;
   }
   default: break;
   }
   if (resource)
      SnapshotD3D11Resource(context, resource.Get());
}
} // namespace

void CaptureD3D11(ID3D11DeviceContext* context, ID3D11Resource* resource)
{
   const uintptr_t address = target_address.load(std::memory_order_acquire);
   if (!context || !MatchesD3D11Target(resource, address))
      return;
   SnapshotD3D11Resource(context, resource);
}

bool CaptureCallD3D11(const Game::D3D11::Call& call, Shaders::StageSet stages, bool handled)
{
   if (!IsActive() || !call.context)
      return handled;
   if (!pass_active.load(std::memory_order_acquire))
   {
      CaptureBoundD3D11(call.context);
      return handled;
   }
   const std::optional<PassTarget> target = GetPassTarget();
   if (!target || !target->luma_pass.empty() || !stages.Has(target->stage))
      return handled;
   const Shaders::Variants::D3D11::BoundShaders* bound = Shaders::Variants::D3D11::GetBoundShaders(call.context);
   const Shaders::Database::Shader* shader = bound ? bound->shaders[size_t(target->stage)] : nullptr;
   if (!shader || shader->hash != target->hash)
      return handled;
   if (frame_instance_counter.fetch_add(1, std::memory_order_relaxed) != target->instance)
      return handled;
   // For output views (RTV/UAV/DSV), run the draw first so we snapshot what this call wrote
   if (!handled && target->mode != ViewMode::ShaderResource)
   {
      Game::D3D11::RunOriginal(call);
      handled = true;
   }
   CaptureViewD3D11(call.context, *target, call.dispatch != nullptr);
   return handled;
}

void CaptureLumaPassD3D11(ID3D11DeviceContext* context, std::string_view pass_name)
{
   if (!pass_active.load(std::memory_order_relaxed) || !context)
      return;
   const std::optional<PassTarget> target = GetPassTarget();
   if (!target || target->luma_pass.empty() || target->luma_pass != pass_name)
      return;
   if (frame_instance_counter.fetch_add(1, std::memory_order_relaxed) != target->instance)
      return;
   CaptureViewD3D11(context, *target, false);
}

void CaptureBoundD3D11(ID3D11DeviceContext* context)
{
   if (!IsActive() || !context)
      return;
   const auto check_view = [&](ID3D11View* view) {
      if (!view)
         return;
      ComPtr<ID3D11Resource> resource;
      view->GetResource(&resource);
      if (resource)
         CaptureD3D11(context, resource.Get());
   };
   std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rtvs = {};
   ID3D11DepthStencilView* dsv = nullptr;
   context->OMGetRenderTargets(UINT(rtvs.size()), rtvs.data(), &dsv);
   for (ID3D11RenderTargetView* rtv : rtvs)
   {
      check_view(rtv);
      if (rtv)
         rtv->Release();
   }
   check_view(dsv);
   if (dsv)
      dsv->Release();

   std::array<ID3D11ShaderResourceView*, 32> srvs = {};
   context->PSGetShaderResources(0, UINT(srvs.size()), srvs.data());
   for (ID3D11ShaderResourceView* srv : srvs)
   {
      check_view(srv);
      if (srv)
         srv->Release();
   }
   srvs = {};
   context->CSGetShaderResources(0, UINT(srvs.size()), srvs.data());
   for (ID3D11ShaderResourceView* srv : srvs)
   {
      check_view(srv);
      if (srv)
         srv->Release();
   }
   std::array<ID3D11UnorderedAccessView*, D3D11_1_UAV_SLOT_COUNT> uavs = {};
   context->CSGetUnorderedAccessViews(0, 8, uavs.data());
   for (UINT i = 0; i < 8; i++)
   {
      check_view(uavs[i]);
      if (uavs[i])
         uavs[i]->Release();
   }
}

bool BindD3D11(ID3D11DeviceContext* context, uint32_t& custom_data_2, float& custom_data_3)
{
   if (!IsActive() || !context)
      return false;
   std::scoped_lock lock(mutex);
   if (!d3d11_snapshot.srv)
      return false;
   ID3D11ShaderResourceView* srv = d3d11_snapshot.srv.Get();
   context->PSSetShaderResources(UINT(d3d11_snapshot.slot), 1, &srv);
   custom_data_2 = PackFlags(current_options, d3d11_snapshot.dim_bits);
   custom_data_3 = float(std::max(current_options.mip_level, 0));
   return true;
}

void ReleaseD3D11(const ID3D11Device* device)
{
   std::scoped_lock lock(mutex);
   if (d3d11_snapshot.device == device)
      d3d11_snapshot = {};
}

void CaptureD3D12(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES current_state)
{
   const uintptr_t address = target_address.load(std::memory_order_acquire);
   if (!list || !MatchesD3D12Target(resource, address))
      return;
   const D3D12_RESOURCE_DESC desc = resource->GetDesc();
   if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER || desc.Dimension == D3D12_RESOURCE_DIMENSION_UNKNOWN)
      return;
   const DXGI_FORMAT srv_format = GetReadableSrvFormat(desc.Format);
   if (srv_format == DXGI_FORMAT_UNKNOWN)
      return;
   ComPtr<ID3D12Device> device;
   if (FAILED(list->GetDevice(IID_PPV_ARGS(&device))) || !device)
      return;

   std::scoped_lock lock(mutex);
   const bool needs_new = !d3d12_snapshot.texture || d3d12_snapshot.device != device.Get() || d3d12_snapshot.desc.Dimension != desc.Dimension ||
      d3d12_snapshot.desc.Width != desc.Width || d3d12_snapshot.desc.Height != desc.Height || d3d12_snapshot.desc.DepthOrArraySize != desc.DepthOrArraySize ||
      d3d12_snapshot.desc.MipLevels != desc.MipLevels || d3d12_snapshot.desc.SampleDesc.Count != desc.SampleDesc.Count || d3d12_snapshot.desc.Format != desc.Format;
   if (needs_new)
   {
      D3D12_RESOURCE_DESC copy_desc = desc;
      copy_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
      const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
      ComPtr<ID3D12Resource> copy;
      if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &copy_desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&copy))))
         return;
      copy->SetName(L"Luma debug draw snapshot");
      d3d12_snapshot.device = device.Get();
      d3d12_snapshot.texture = copy;
      d3d12_snapshot.desc = desc;
      d3d12_snapshot.srv_format = srv_format;
      if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
      {
         d3d12_snapshot.slot = Slot::Texture3D;
         d3d12_snapshot.dim_bits = DIM_BIT_3D;
      }
      else if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D)
      {
         d3d12_snapshot.slot = desc.DepthOrArraySize > 1 ? Slot::Texture1DArray : Slot::Texture1D;
         d3d12_snapshot.dim_bits = desc.DepthOrArraySize > 1 ? DIM_BIT_ARRAY : 0;
      }
      else
      {
         const bool ms = desc.SampleDesc.Count > 1;
         const bool arr = desc.DepthOrArraySize > 1;
         d3d12_snapshot.slot = ms ? (arr ? Slot::Texture2DMSArray : Slot::Texture2DMS) : (arr ? Slot::Texture2DArray : Slot::Texture2D);
         d3d12_snapshot.dim_bits = DIM_BIT_2D | (ms ? DIM_BIT_MS : 0u) | (arr ? DIM_BIT_ARRAY : 0u);
      }
   }

   std::array<D3D12_RESOURCE_BARRIER, 2> before = {};
   UINT before_count = 0;
   before[before_count++] = {.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
      .Transition = {d3d12_snapshot.texture.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST}};
   if (current_state != D3D12_RESOURCE_STATE_COPY_SOURCE)
      before[before_count++] = {.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
         .Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, current_state, D3D12_RESOURCE_STATE_COPY_SOURCE}};
   list->ResourceBarrier(before_count, before.data());
   list->CopyResource(d3d12_snapshot.texture.Get(), resource);
   for (UINT i = 0; i < before_count; i++)
      std::swap(before[i].Transition.StateBefore, before[i].Transition.StateAfter);
   list->ResourceBarrier(before_count, before.data());
}

bool BindD3D12(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE table_start, uint32_t view_increment, uint32_t& custom_data_2, float& custom_data_3)
{
   if (!IsActive() || !device)
      return false;
   std::scoped_lock lock(mutex);
   if (!d3d12_snapshot.texture || d3d12_snapshot.device != device)
      return false;
   D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {.Format = d3d12_snapshot.srv_format, .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING};
   switch (d3d12_snapshot.slot)
   {
   case Slot::Texture2D:
      srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D = {0, d3d12_snapshot.desc.MipLevels, 0, 0.f};
      break;
   case Slot::Texture2DMS:
      srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
      break;
   case Slot::Texture2DArray:
      srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
      srv_desc.Texture2DArray = {0, d3d12_snapshot.desc.MipLevels, 0, d3d12_snapshot.desc.DepthOrArraySize, 0, 0.f};
      break;
   case Slot::Texture2DMSArray:
      srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
      srv_desc.Texture2DMSArray = {0, d3d12_snapshot.desc.DepthOrArraySize};
      break;
   case Slot::Texture3D:
      srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
      srv_desc.Texture3D = {0, d3d12_snapshot.desc.MipLevels, 0.f};
      break;
   case Slot::Texture1D:
      srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
      srv_desc.Texture1D = {0, d3d12_snapshot.desc.MipLevels, 0.f};
      break;
   case Slot::Texture1DArray:
      srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
      srv_desc.Texture1DArray = {0, d3d12_snapshot.desc.MipLevels, 0, d3d12_snapshot.desc.DepthOrArraySize, 0.f};
      break;
   }
   const D3D12_CPU_DESCRIPTOR_HANDLE handle = {table_start.ptr + SIZE_T(d3d12_snapshot.slot) * view_increment};
   device->CreateShaderResourceView(d3d12_snapshot.texture.Get(), &srv_desc, handle);
   custom_data_2 = PackFlags(current_options, d3d12_snapshot.dim_bits);
   custom_data_3 = float(std::max(current_options.mip_level, 0));
   return true;
}

void ReleaseD3D12(const ID3D12Device* device)
{
   std::scoped_lock lock(mutex);
   if (d3d12_snapshot.device == device)
      d3d12_snapshot = {};
}
} // namespace DebugDraw
} // namespace Trace
} // namespace Luma
#endif
