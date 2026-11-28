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
#include "Hooks/D3D12/D3D12Lifetime.hpp"
#include "Hooks/HookScope.hpp"
#include "Resources/D3D11/D3D11Mirrors.hpp"
#include "Resources/D3D12/D3D12Mirrors.hpp"
#include "Shaders/D3D11/D3D11Variants.hpp"
#include "Shaders/D3D12/D3D12Variants.hpp"
#include "Shaders/ShaderDatabase.hpp"
#include "Trace/D3D12/D3D12Capture.hpp"
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
   DXGI_FORMAT srv_format = DXGI_FORMAT_UNKNOWN;
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
bool captured_this_frame = false;
bool captured_prev_frame = false;

bool IsDisplayCompositionTargetLocked()
{
   return pass_target && pass_target->luma_pass == "Luma Display Composition";
}

bool HasFreshSnapshotLocked()
{
   return captured_this_frame || (captured_prev_frame && IsDisplayCompositionTargetLocked());
}

std::string_view GetStageName(ShaderStage stage)
{
   constexpr std::string_view NAMES[] = {"VS", "HS", "DS", "GS", "PS", "CS", "AS", "MS"};
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

// Targeting and lifecycle
bool IsActive()
{
   return active.load(std::memory_order_relaxed);
}

void SetTarget(std::string resource_id)
{
   const uintptr_t address = ParseAddress(resource_id);
   std::scoped_lock lock(mutex);
   if (pass_target && pass_target->mode == ViewMode::Depth)
      current_options.red_only = false;
   pass_target.reset();
   pass_active.store(false, std::memory_order_release);
   frame_instance_counter.store(0, std::memory_order_relaxed);
   target_id = address ? std::move(resource_id) : std::string();
   target_address.store(address, std::memory_order_release);
   active.store(address != 0, std::memory_order_release);
   d3d11_snapshot = {};
   d3d12_snapshot = {};
   captured_this_frame = false;
   captured_prev_frame = false;
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
   captured_this_frame = false;
   captured_prev_frame = false;
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
   if (pass_target && pass_target->mode == ViewMode::Depth)
      current_options.red_only = false;
   target_id.clear();
   target_address.store(0, std::memory_order_release);
   pass_target.reset();
   pass_active.store(false, std::memory_order_release);
   frame_instance_counter.store(0, std::memory_order_relaxed);
   active.store(false, std::memory_order_release);
   d3d11_snapshot = {};
   d3d12_snapshot = {};
   captured_this_frame = false;
   captured_prev_frame = false;
}

void ResetFrame()
{
   frame_instance_counter.store(0, std::memory_order_relaxed);
   if (!IsActive())
      return;
   std::scoped_lock lock(mutex);
   captured_prev_frame = captured_this_frame;
   if (!captured_this_frame)
   {
      d3d11_snapshot = {};
      d3d12_snapshot = {};
   }
   captured_this_frame = false;
}

// Display options
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
void ClearD3D11SnapshotLocked()
{
   d3d11_snapshot = {};
   captured_this_frame = false;
}

bool IsFloatFormat(DXGI_FORMAT format)
{
   const Utils::Format::DXGI::Encoding encoding = Utils::Format::DXGI::GetEncoding(format);
   return encoding == Utils::Format::DXGI::Encoding::SFloat || encoding == Utils::Format::DXGI::Encoding::UFloat;
}

void SnapshotD3D11Resource(ID3D11DeviceContext* context, ID3D11Resource* resource, DXGI_FORMAT view_format = DXGI_FORMAT_UNKNOWN)
{
   if (!context || !resource)
   {
      std::scoped_lock lock(mutex);
      ClearD3D11SnapshotLocked();
      return;
   }
   D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
   resource->GetType(&dim);
   if (dim == D3D11_RESOURCE_DIMENSION_BUFFER || dim == D3D11_RESOURCE_DIMENSION_UNKNOWN)
   {
      std::scoped_lock lock(mutex);
      ClearD3D11SnapshotLocked();
      return;
   }
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   if (!device)
      return;

   std::scoped_lock lock(mutex);
   const auto resolve_srv_format = [&](DXGI_FORMAT resource_format, DXGI_FORMAT& fallback) -> DXGI_FORMAT {
      fallback = GetReadableSrvFormat(resource_format);
      const DXGI_FORMAT preferred = view_format != DXGI_FORMAT_UNKNOWN ? GetReadableSrvFormat(view_format) : fallback;
      return preferred != DXGI_FORMAT_UNKNOWN ? preferred : fallback;
   };

   if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
   {
      ComPtr<ID3D11Texture2D> src;
      if (FAILED(resource->QueryInterface(IID_PPV_ARGS(&src))))
      {
         ClearD3D11SnapshotLocked();
         return;
      }
      D3D11_TEXTURE2D_DESC desc = {};
      src->GetDesc(&desc);
      DXGI_FORMAT fallback_srv_format = DXGI_FORMAT_UNKNOWN;
      DXGI_FORMAT srv_format = resolve_srv_format(desc.Format, fallback_srv_format);
      if (srv_format == DXGI_FORMAT_UNKNOWN)
      {
         ClearD3D11SnapshotLocked();
         return;
      }
      const bool needs_new = !d3d11_snapshot.texture || d3d11_snapshot.device != device.Get() || d3d11_snapshot.dimension != dim ||
         d3d11_snapshot.width != desc.Width || d3d11_snapshot.height != desc.Height || d3d11_snapshot.mips != desc.MipLevels ||
         d3d11_snapshot.array_size != desc.ArraySize || d3d11_snapshot.sample_count != desc.SampleDesc.Count || d3d11_snapshot.format != desc.Format ||
         d3d11_snapshot.srv_format != srv_format;
      if (needs_new)
      {
         D3D11_TEXTURE2D_DESC copy_desc = desc;
         copy_desc.Usage = D3D11_USAGE_DEFAULT;
         copy_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
         copy_desc.CPUAccessFlags = 0;
         // Strip cube/shared/gen-mips flags (matching 1.0) so cubemaps can be sampled as Texture2DArray
         copy_desc.MiscFlags = 0;
         if (const DXGI_FORMAT typeless = Utils::Format::DXGI::GetTypeless(desc.Format); typeless != DXGI_FORMAT_UNKNOWN)
            copy_desc.Format = typeless;
         ComPtr<ID3D11Texture2D> copy;
         if (FAILED(device->CreateTexture2D(&copy_desc, nullptr, &copy)))
         {
            ClearD3D11SnapshotLocked();
            return;
         }
         D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
         srv_desc.Format = srv_format;
         const bool ms = desc.SampleDesc.Count > 1;
         const bool arr = desc.ArraySize > 1;
         Slot slot = Slot::Texture2D;
         uint32_t dim_bits = DIM_BIT_2D;
         if (ms && arr)
         {
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY;
            srv_desc.Texture2DMSArray = {0, desc.ArraySize};
            slot = Slot::Texture2DMSArray;
            dim_bits = DIM_BIT_2D | DIM_BIT_MS | DIM_BIT_ARRAY;
         }
         else if (ms)
         {
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
            slot = Slot::Texture2DMS;
            dim_bits = DIM_BIT_2D | DIM_BIT_MS;
         }
         else if (arr)
         {
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            srv_desc.Texture2DArray = {0, desc.MipLevels, 0, desc.ArraySize};
            slot = Slot::Texture2DArray;
            dim_bits = DIM_BIT_2D | DIM_BIT_ARRAY;
         }
         else
         {
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Texture2D = {0, desc.MipLevels};
            slot = Slot::Texture2D;
            dim_bits = DIM_BIT_2D;
         }
         ComPtr<ID3D11ShaderResourceView> srv;
         if (FAILED(device->CreateShaderResourceView(copy.Get(), &srv_desc, &srv)))
         {
            if (fallback_srv_format != DXGI_FORMAT_UNKNOWN && fallback_srv_format != srv_format)
            {
               srv_desc.Format = fallback_srv_format;
               srv_format = fallback_srv_format;
               device->CreateShaderResourceView(copy.Get(), &srv_desc, &srv);
            }
            if (!srv)
            {
               ClearD3D11SnapshotLocked();
               return;
            }
         }
         d3d11_snapshot.device = device.Get();
         d3d11_snapshot.texture = copy;
         d3d11_snapshot.srv = srv;
         d3d11_snapshot.slot = slot;
         d3d11_snapshot.dim_bits = dim_bits;
         d3d11_snapshot.dimension = dim;
         d3d11_snapshot.width = desc.Width;
         d3d11_snapshot.height = desc.Height;
         d3d11_snapshot.depth = 1;
         d3d11_snapshot.mips = desc.MipLevels;
         d3d11_snapshot.array_size = desc.ArraySize;
         d3d11_snapshot.sample_count = desc.SampleDesc.Count;
         d3d11_snapshot.format = desc.Format;
         d3d11_snapshot.srv_format = srv_format;
      }
      context->CopyResource(d3d11_snapshot.texture.Get(), resource);
      captured_this_frame = true;
   }
   else if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE3D)
   {
      ComPtr<ID3D11Texture3D> src;
      if (FAILED(resource->QueryInterface(IID_PPV_ARGS(&src))))
      {
         ClearD3D11SnapshotLocked();
         return;
      }
      D3D11_TEXTURE3D_DESC desc = {};
      src->GetDesc(&desc);
      DXGI_FORMAT fallback_srv_format = DXGI_FORMAT_UNKNOWN;
      DXGI_FORMAT srv_format = resolve_srv_format(desc.Format, fallback_srv_format);
      if (srv_format == DXGI_FORMAT_UNKNOWN)
      {
         ClearD3D11SnapshotLocked();
         return;
      }
      const bool needs_new = !d3d11_snapshot.texture || d3d11_snapshot.device != device.Get() || d3d11_snapshot.dimension != dim ||
         d3d11_snapshot.width != desc.Width || d3d11_snapshot.height != desc.Height || d3d11_snapshot.depth != desc.Depth ||
         d3d11_snapshot.mips != desc.MipLevels || d3d11_snapshot.format != desc.Format || d3d11_snapshot.srv_format != srv_format;
      if (needs_new)
      {
         D3D11_TEXTURE3D_DESC copy_desc = desc;
         copy_desc.Usage = D3D11_USAGE_DEFAULT;
         copy_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
         copy_desc.CPUAccessFlags = 0;
         copy_desc.MiscFlags = 0;
         if (const DXGI_FORMAT typeless = Utils::Format::DXGI::GetTypeless(desc.Format); typeless != DXGI_FORMAT_UNKNOWN)
            copy_desc.Format = typeless;
         ComPtr<ID3D11Texture3D> copy;
         if (FAILED(device->CreateTexture3D(&copy_desc, nullptr, &copy)))
         {
            ClearD3D11SnapshotLocked();
            return;
         }
         D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {.Format = srv_format, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D, .Texture3D = {0, desc.MipLevels}};
         ComPtr<ID3D11ShaderResourceView> srv;
         if (FAILED(device->CreateShaderResourceView(copy.Get(), &srv_desc, &srv)))
         {
            if (fallback_srv_format != DXGI_FORMAT_UNKNOWN && fallback_srv_format != srv_format)
            {
               srv_desc.Format = fallback_srv_format;
               srv_format = fallback_srv_format;
               device->CreateShaderResourceView(copy.Get(), &srv_desc, &srv);
            }
            if (!srv)
            {
               ClearD3D11SnapshotLocked();
               return;
            }
         }
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
         d3d11_snapshot.srv_format = srv_format;
      }
      context->CopyResource(d3d11_snapshot.texture.Get(), resource);
      captured_this_frame = true;
   }
   else if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE1D)
   {
      ComPtr<ID3D11Texture1D> src;
      if (FAILED(resource->QueryInterface(IID_PPV_ARGS(&src))))
      {
         ClearD3D11SnapshotLocked();
         return;
      }
      D3D11_TEXTURE1D_DESC desc = {};
      src->GetDesc(&desc);
      DXGI_FORMAT fallback_srv_format = DXGI_FORMAT_UNKNOWN;
      DXGI_FORMAT srv_format = resolve_srv_format(desc.Format, fallback_srv_format);
      if (srv_format == DXGI_FORMAT_UNKNOWN)
      {
         ClearD3D11SnapshotLocked();
         return;
      }
      const bool needs_new = !d3d11_snapshot.texture || d3d11_snapshot.device != device.Get() || d3d11_snapshot.dimension != dim ||
         d3d11_snapshot.width != desc.Width || d3d11_snapshot.mips != desc.MipLevels || d3d11_snapshot.array_size != desc.ArraySize ||
         d3d11_snapshot.format != desc.Format || d3d11_snapshot.srv_format != srv_format;
      if (needs_new)
      {
         D3D11_TEXTURE1D_DESC copy_desc = desc;
         copy_desc.Usage = D3D11_USAGE_DEFAULT;
         copy_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
         copy_desc.CPUAccessFlags = 0;
         copy_desc.MiscFlags = 0;
         if (const DXGI_FORMAT typeless = Utils::Format::DXGI::GetTypeless(desc.Format); typeless != DXGI_FORMAT_UNKNOWN)
            copy_desc.Format = typeless;
         ComPtr<ID3D11Texture1D> copy;
         if (FAILED(device->CreateTexture1D(&copy_desc, nullptr, &copy)))
         {
            ClearD3D11SnapshotLocked();
            return;
         }
         D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {.Format = srv_format};
         Slot slot = Slot::Texture1D;
         uint32_t dim_bits = 0;
         if (desc.ArraySize > 1)
         {
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1DARRAY;
            srv_desc.Texture1DArray = {0, desc.MipLevels, 0, desc.ArraySize};
            slot = Slot::Texture1DArray;
            dim_bits = DIM_BIT_ARRAY;
         }
         else
         {
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D;
            srv_desc.Texture1D = {0, desc.MipLevels};
            slot = Slot::Texture1D;
            dim_bits = 0;
         }
         ComPtr<ID3D11ShaderResourceView> srv;
         if (FAILED(device->CreateShaderResourceView(copy.Get(), &srv_desc, &srv)))
         {
            if (fallback_srv_format != DXGI_FORMAT_UNKNOWN && fallback_srv_format != srv_format)
            {
               srv_desc.Format = fallback_srv_format;
               srv_format = fallback_srv_format;
               device->CreateShaderResourceView(copy.Get(), &srv_desc, &srv);
            }
            if (!srv)
            {
               ClearD3D11SnapshotLocked();
               return;
            }
         }
         d3d11_snapshot.device = device.Get();
         d3d11_snapshot.texture = copy;
         d3d11_snapshot.srv = srv;
         d3d11_snapshot.slot = slot;
         d3d11_snapshot.dim_bits = dim_bits;
         d3d11_snapshot.dimension = dim;
         d3d11_snapshot.width = desc.Width;
         d3d11_snapshot.height = 1;
         d3d11_snapshot.depth = 1;
         d3d11_snapshot.mips = desc.MipLevels;
         d3d11_snapshot.array_size = desc.ArraySize;
         d3d11_snapshot.sample_count = 1;
         d3d11_snapshot.format = desc.Format;
         d3d11_snapshot.srv_format = srv_format;
      }
      context->CopyResource(d3d11_snapshot.texture.Get(), resource);
      captured_this_frame = true;
   }
}

UINT GetMaxUavSlots(ID3D11DeviceContext* context)
{
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   return device && device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_1 ? D3D11_1_UAV_SLOT_COUNT : D3D11_PS_CS_UAV_REGISTER_COUNT;
}

void CaptureViewD3D11(ID3D11DeviceContext* context, const PassTarget& target, bool is_compute)
{
   if (!context)
      return;
   ComPtr<ID3D11Resource> resource;
   DXGI_FORMAT view_format = DXGI_FORMAT_UNKNOWN;
   switch (target.mode)
   {
   case ViewMode::RenderTarget:
   {
      if (target.slot >= D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT)
         break;
      std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rtvs = {};
      context->OMGetRenderTargets(UINT(rtvs.size()), rtvs.data(), nullptr);
      if (rtvs[target.slot])
      {
         rtvs[target.slot]->GetResource(&resource);
         D3D11_RENDER_TARGET_VIEW_DESC desc = {};
         rtvs[target.slot]->GetDesc(&desc);
         view_format = desc.Format;
      }
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
      {
         dsv->GetResource(&resource);
         D3D11_DEPTH_STENCIL_VIEW_DESC desc = {};
         dsv->GetDesc(&desc);
         view_format = desc.Format;
      }
      break;
   }
   case ViewMode::ShaderResource:
   {
      if (target.slot >= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT)
         break;
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
      {
         srv->GetResource(&resource);
         D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
         srv->GetDesc(&desc);
         view_format = desc.Format;
      }
      break;
   }
   case ViewMode::UnorderedAccess:
   {
      const UINT max_uavs = GetMaxUavSlots(context);
      if (target.slot >= max_uavs)
         break;
      ComPtr<ID3D11UnorderedAccessView> uav;
      if (is_compute)
         context->CSGetUnorderedAccessViews(target.slot, 1, &uav);
      else
         context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, target.slot, 1, &uav);
      if (uav)
      {
         uav->GetResource(&resource);
         D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
         uav->GetDesc(&desc);
         view_format = desc.Format;
      }
      break;
   }
   default: break;
   }
   SnapshotD3D11Resource(context, resource.Get(), view_format);
}

struct MatchedResource
{
   ComPtr<ID3D11Resource> resource;
   DXGI_FORMAT view_format = DXGI_FORMAT_UNKNOWN;
};

MatchedResource FindBoundOutputD3D11(ID3D11DeviceContext* context, uintptr_t address, bool is_compute)
{
   MatchedResource match;
   if (!is_compute)
   {
      std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rtvs = {};
      ComPtr<ID3D11DepthStencilView> dsv;
      context->OMGetRenderTargets(UINT(rtvs.size()), rtvs.data(), &dsv);
      for (ID3D11RenderTargetView* rtv : rtvs)
      {
         if (!match.resource && rtv)
         {
            ComPtr<ID3D11Resource> res;
            rtv->GetResource(&res);
            if (MatchesD3D11Target(res.Get(), address))
            {
               D3D11_RENDER_TARGET_VIEW_DESC desc = {};
               rtv->GetDesc(&desc);
               match = {std::move(res), desc.Format};
            }
         }
         if (rtv)
            rtv->Release();
      }
      if (!match.resource && dsv)
      {
         ComPtr<ID3D11Resource> res;
         dsv->GetResource(&res);
         if (MatchesD3D11Target(res.Get(), address))
         {
            D3D11_DEPTH_STENCIL_VIEW_DESC desc = {};
            dsv->GetDesc(&desc);
            match = {std::move(res), desc.Format};
         }
      }
   }
   if (!match.resource)
   {
      const UINT max_uavs = GetMaxUavSlots(context);
      std::array<ID3D11UnorderedAccessView*, D3D11_1_UAV_SLOT_COUNT> uavs = {};
      if (is_compute)
         context->CSGetUnorderedAccessViews(0, max_uavs, uavs.data());
      else
         context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, max_uavs, uavs.data());
      for (UINT i = 0; i < max_uavs; i++)
      {
         if (!match.resource && uavs[i])
         {
            ComPtr<ID3D11Resource> res;
            uavs[i]->GetResource(&res);
            if (MatchesD3D11Target(res.Get(), address))
            {
               D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
               uavs[i]->GetDesc(&desc);
               match = {std::move(res), desc.Format};
            }
         }
         if (uavs[i])
            uavs[i]->Release();
      }
   }
   return match;
}

MatchedResource FindBoundSrvD3D11(ID3D11DeviceContext* context, uintptr_t address, Shaders::StageSet stages)
{
   MatchedResource match;
   std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> srvs = {};
   const auto scan = [&](ShaderStage stage) {
      if (match.resource || !stages.Has(stage))
         return;
      srvs = {};
      switch (stage)
      {
      case ShaderStage::Vertex: context->VSGetShaderResources(0, UINT(srvs.size()), srvs.data()); break;
      case ShaderStage::Hull: context->HSGetShaderResources(0, UINT(srvs.size()), srvs.data()); break;
      case ShaderStage::Domain: context->DSGetShaderResources(0, UINT(srvs.size()), srvs.data()); break;
      case ShaderStage::Geometry: context->GSGetShaderResources(0, UINT(srvs.size()), srvs.data()); break;
      case ShaderStage::Pixel: context->PSGetShaderResources(0, UINT(srvs.size()), srvs.data()); break;
      case ShaderStage::Compute: context->CSGetShaderResources(0, UINT(srvs.size()), srvs.data()); break;
      }
      for (ID3D11ShaderResourceView* srv : srvs)
      {
         if (!match.resource && srv)
         {
            ComPtr<ID3D11Resource> res;
            srv->GetResource(&res);
            if (MatchesD3D11Target(res.Get(), address))
            {
               D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
               srv->GetDesc(&desc);
               match = {std::move(res), desc.Format};
            }
         }
         if (srv)
            srv->Release();
      }
   };
   for (const ShaderStage stage : {ShaderStage::Pixel, ShaderStage::Compute, ShaderStage::Vertex, ShaderStage::Geometry, ShaderStage::Hull, ShaderStage::Domain})
      scan(stage);
   return match;
}
} // namespace

// D3D11 capture and bind
void CaptureD3D11(ID3D11DeviceContext* context, ID3D11Resource* resource)
{
   const uintptr_t address = target_address.load(std::memory_order_acquire);
   if (!context || !MatchesD3D11Target(resource, address))
      return;
   if (ID3D11Resource* mirror = Resources::Mirrors::D3D11::FindMirror(resource))
      resource = mirror;
   SnapshotD3D11Resource(context, resource);
}

bool CaptureCallD3D11(const Game::D3D11::Call& call, Shaders::StageSet stages, bool handled)
{
   if (!IsActive() || !call.context)
      return handled;
   if (!pass_active.load(std::memory_order_acquire))
   {
      const uintptr_t address = target_address.load(std::memory_order_acquire);
      if (!address)
         return handled;
      const bool is_compute = call.dispatch != nullptr;
      if (const MatchedResource output = FindBoundOutputD3D11(call.context, address, is_compute); output.resource)
      {
         // Run the draw/dispatch first so we snapshot what this call wrote rather than the previous frame
         if (!handled)
         {
            Game::D3D11::RunOriginal(call);
            handled = true;
         }
         SnapshotD3D11Resource(call.context, output.resource.Get(), output.view_format);
         return handled;
      }
      if (const MatchedResource srv = FindBoundSrvD3D11(call.context, address, stages); srv.resource)
         SnapshotD3D11Resource(call.context, srv.resource.Get(), srv.view_format);
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

void CaptureSrPassD3D11(ID3D11DeviceContext* context, std::string_view pass_name, ID3D11Resource* color, ID3D11Resource* depth,
   ID3D11Resource* motion_vectors, ID3D11Resource* exposure, ID3D11Resource* reactive, ID3D11Resource* transparency, ID3D11Resource* output)
{
   if (!IsActive() || !context)
      return;
   const std::array<ID3D11Resource*, 6> inputs = {color, depth, motion_vectors, exposure, reactive, transparency};
   if (pass_active.load(std::memory_order_acquire))
   {
      const std::optional<PassTarget> target = GetPassTarget();
      if (!target || target->luma_pass.empty() || target->luma_pass != pass_name)
         return;
      if (frame_instance_counter.fetch_add(1, std::memory_order_relaxed) != target->instance)
         return;
      ID3D11Resource* chosen = nullptr;
      if (target->mode == ViewMode::RenderTarget && target->slot == 0)
         chosen = output;
      else if (target->mode == ViewMode::ShaderResource && target->slot < inputs.size())
         chosen = inputs[target->slot];
      if (chosen)
      {
         Hooks::Scope::Guard scope;
         SnapshotD3D11Resource(context, chosen);
      }
      return;
   }
   const uintptr_t address = target_address.load(std::memory_order_acquire);
   if (!address)
      return;
   if (MatchesD3D11Target(output, address))
   {
      Hooks::Scope::Guard scope;
      SnapshotD3D11Resource(context, output);
      return;
   }
   for (ID3D11Resource* res : inputs)
   {
      if (MatchesD3D11Target(res, address))
      {
         Hooks::Scope::Guard scope;
         SnapshotD3D11Resource(context, res);
         return;
      }
   }
}

void CaptureBoundD3D11(ID3D11DeviceContext* context)
{
   if (!IsActive() || !context)
      return;
   const uintptr_t address = target_address.load(std::memory_order_acquire);
   if (!address)
      return;
   if (const MatchedResource output = FindBoundOutputD3D11(context, address, false); output.resource)
   {
      SnapshotD3D11Resource(context, output.resource.Get(), output.view_format);
      return;
   }
   if (const MatchedResource compute_output = FindBoundOutputD3D11(context, address, true); compute_output.resource)
   {
      SnapshotD3D11Resource(context, compute_output.resource.Get(), compute_output.view_format);
      return;
   }
   if (const MatchedResource srv = FindBoundSrvD3D11(context, address, Shaders::GRAPHICS_STAGES | Shaders::COMPUTE_STAGES); srv.resource)
      SnapshotD3D11Resource(context, srv.resource.Get(), srv.view_format);
}

bool BindD3D11(ID3D11DeviceContext* context, uint32_t& custom_data_2, float& custom_data_3)
{
   if (!IsActive() || !context)
      return false;
   std::scoped_lock lock(mutex);
   if (!d3d11_snapshot.srv || !HasFreshSnapshotLocked())
      return false;
   ID3D11ShaderResourceView* srv = d3d11_snapshot.srv.Get();
   context->PSSetShaderResources(UINT(d3d11_snapshot.slot), 1, &srv);
   Options effective = current_options;
   // Match 1.0: disable tonemapping when saturating or viewing non-float formats
   if (effective.do_saturate || !IsFloatFormat(d3d11_snapshot.srv_format))
      effective.tonemap = false;
   custom_data_2 = PackFlags(effective, d3d11_snapshot.dim_bits);
   custom_data_3 = float(std::max(effective.mip_level, 0));
   return true;
}

void ReleaseD3D11(const ID3D11Device* device)
{
   std::scoped_lock lock(mutex);
   if (d3d11_snapshot.device == device)
   {
      d3d11_snapshot = {};
      captured_this_frame = false;
      captured_prev_frame = false;
   }
}

namespace {
Hooks::Lifetime::D3D12::Object d3d12_snapshot_lifetime;

void SnapshotD3D12Resource(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES current_state,
   DXGI_FORMAT view_format = DXGI_FORMAT_UNKNOWN)
{
   if (!list || !resource)
      return;
   const D3D12_RESOURCE_DESC desc = resource->GetDesc();
   if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER || desc.Dimension == D3D12_RESOURCE_DIMENSION_UNKNOWN)
      return;
   const DXGI_FORMAT fallback_srv_format = GetReadableSrvFormat(desc.Format);
   const DXGI_FORMAT preferred = view_format != DXGI_FORMAT_UNKNOWN ? GetReadableSrvFormat(view_format) : fallback_srv_format;
   const DXGI_FORMAT srv_format = preferred != DXGI_FORMAT_UNKNOWN ? preferred : fallback_srv_format;
   if (srv_format == DXGI_FORMAT_UNKNOWN)
   {
      std::scoped_lock lock(mutex);
      d3d12_snapshot = {};
      Hooks::Lifetime::D3D12::Replace(d3d12_snapshot_lifetime, nullptr);
      captured_this_frame = false;
      return;
   }
   ComPtr<ID3D12Device> device;
   if (FAILED(list->GetDevice(IID_PPV_ARGS(&device))) || !device)
      return;

   Hooks::Scope::Guard scope;
   std::scoped_lock lock(mutex);
   const bool needs_new = !d3d12_snapshot.texture || d3d12_snapshot.device != device.Get() || d3d12_snapshot.desc.Dimension != desc.Dimension ||
      d3d12_snapshot.desc.Width != desc.Width || d3d12_snapshot.desc.Height != desc.Height || d3d12_snapshot.desc.DepthOrArraySize != desc.DepthOrArraySize ||
      d3d12_snapshot.desc.MipLevels != desc.MipLevels || d3d12_snapshot.desc.SampleDesc.Count != desc.SampleDesc.Count || d3d12_snapshot.desc.Format != desc.Format ||
      d3d12_snapshot.srv_format != srv_format;
   if (needs_new)
   {
      D3D12_RESOURCE_DESC copy_desc = desc;
      copy_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
      const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
      ComPtr<ID3D12Resource> copy;
      if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &copy_desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&copy))))
      {
         d3d12_snapshot = {};
         Hooks::Lifetime::D3D12::Replace(d3d12_snapshot_lifetime, nullptr);
         captured_this_frame = false;
         return;
      }
      copy->SetName(L"Luma debug draw snapshot");
      d3d12_snapshot.device = device.Get();
      d3d12_snapshot.texture = copy;
      Hooks::Lifetime::D3D12::Replace(d3d12_snapshot_lifetime, copy);
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

   Hooks::Lifetime::D3D12::Reference(list, d3d12_snapshot_lifetime);
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
   captured_this_frame = true;
}
} // namespace

// D3D12 capture and bind
void CaptureD3D12(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES current_state)
{
   const uintptr_t address = target_address.load(std::memory_order_acquire);
   if (!list || !MatchesD3D12Target(resource, address))
      return;
   if (ID3D12Resource* mirror = Resources::Mirrors::D3D12::FindMirror(resource))
      resource = mirror;
   SnapshotD3D12Resource(list, resource, current_state);
}

bool CaptureCallD3D12(const Game::D3D12::Call& call, Shaders::StageSet stages, bool handled)
{
   if (!IsActive() || !call.list || !pass_active.load(std::memory_order_acquire))
      return handled;
   const std::optional<PassTarget> target = GetPassTarget();
   if (!target || !target->luma_pass.empty() || !stages.Has(target->stage))
      return handled;
   const Shaders::Variants::D3D12::BoundPipeline bound = Shaders::Variants::D3D12::GetBoundPipeline(call.list);
   const Shaders::Database::Shader* shader = bound.tracked ? bound.tracked->shaders[size_t(target->stage)] : nullptr;
   if (!shader || shader->hash != target->hash)
      return handled;
   if (frame_instance_counter.fetch_add(1, std::memory_order_relaxed) != target->instance)
      return handled;

   Readback::ViewKind kind = Readback::ViewKind::RenderTarget;
   D3D12_RESOURCE_STATES default_state = D3D12_RESOURCE_STATE_RENDER_TARGET;
   switch (target->mode)
   {
   case ViewMode::RenderTarget:
      kind = Readback::ViewKind::RenderTarget;
      default_state = D3D12_RESOURCE_STATE_RENDER_TARGET;
      break;
   case ViewMode::Depth:
      kind = Readback::ViewKind::Depth;
      default_state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
      break;
   case ViewMode::ShaderResource:
      kind = Readback::ViewKind::ShaderResource;
      default_state = target->stage == ShaderStage::Pixel ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
      break;
   case ViewMode::UnorderedAccess:
      kind = Readback::ViewKind::UnorderedAccess;
      default_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      break;
   default: return handled;
   }

   if (!handled && target->mode != ViewMode::ShaderResource)
   {
      Game::D3D12::RunOriginal(call);
      handled = true;
   }

   const std::optional<Capture::D3D12::BoundView> view = Capture::D3D12::FindBoundView(call.list, kind, target->stage, target->slot);
   if (!view || !view->resource)
      return handled;
   ID3D12Resource* resource = view->resource;
   if (ID3D12Resource* mirror = Resources::Mirrors::D3D12::FindMirror(resource))
      resource = mirror;
   SnapshotD3D12Resource(call.list, resource, view->state.value_or(default_state), view->format);
   return handled;
}

void CaptureSrPassD3D12(ID3D12GraphicsCommandList* list, std::string_view pass_name, ID3D12Resource* color, ID3D12Resource* depth,
   ID3D12Resource* motion_vectors, ID3D12Resource* exposure, ID3D12Resource* reactive, ID3D12Resource* transparency, ID3D12Resource* output)
{
   if (!IsActive() || !list)
      return;
   const std::array<ID3D12Resource*, 6> inputs = {color, depth, motion_vectors, exposure, reactive, transparency};
   if (pass_active.load(std::memory_order_acquire))
   {
      const std::optional<PassTarget> target = GetPassTarget();
      if (!target || target->luma_pass.empty() || target->luma_pass != pass_name)
         return;
      if (frame_instance_counter.fetch_add(1, std::memory_order_relaxed) != target->instance)
         return;
      if (target->mode == ViewMode::RenderTarget && target->slot == 0 && output)
         SnapshotD3D12Resource(list, output, D3D12_RESOURCE_STATE_RENDER_TARGET);
      else if (target->mode == ViewMode::ShaderResource && target->slot < inputs.size() && inputs[target->slot])
         SnapshotD3D12Resource(list, inputs[target->slot], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
      return;
   }
   const uintptr_t address = target_address.load(std::memory_order_acquire);
   if (!address)
      return;
   if (MatchesD3D12Target(output, address))
   {
      SnapshotD3D12Resource(list, output, D3D12_RESOURCE_STATE_RENDER_TARGET);
      return;
   }
   for (ID3D12Resource* res : inputs)
   {
      if (MatchesD3D12Target(res, address))
      {
         SnapshotD3D12Resource(list, res, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
         return;
      }
   }
}

bool BindD3D12(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE table_start, uint32_t view_increment, uint32_t& custom_data_2, float& custom_data_3)
{
   if (!IsActive() || !device)
      return false;
   std::scoped_lock lock(mutex);
   if (!d3d12_snapshot.texture || d3d12_snapshot.device != device || !HasFreshSnapshotLocked())
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
   Options effective = current_options;
   if (effective.do_saturate || !IsFloatFormat(d3d12_snapshot.srv_format))
      effective.tonemap = false;
   custom_data_2 = PackFlags(effective, d3d12_snapshot.dim_bits);
   custom_data_3 = float(std::max(effective.mip_level, 0));
   return true;
}

void ReleaseD3D12(const ID3D12Device* device)
{
   std::scoped_lock lock(mutex);
   if (d3d12_snapshot.device == device)
   {
      d3d12_snapshot = {};
      Hooks::Lifetime::D3D12::Replace(d3d12_snapshot_lifetime, nullptr);
      captured_this_frame = false;
      captured_prev_frame = false;
   }
}
} // namespace DebugDraw
} // namespace Trace
} // namespace Luma
#endif
