#include "Resources/ResourceMirrors.hpp"

#include <array>
#include <atomic>
#include <mutex>
#include <unordered_map>

#include "Resources/SamplerMirrors.hpp"
#include "Settings/SettingKeys.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Log/Log.hpp"

namespace Luma {
namespace Resources {
namespace Mirrors {
namespace {
// The runtime switch's value, read at every bind
std::atomic<bool> upgrades = true;
std::atomic<uint32_t> mirrors = 0;
std::atomic<int64_t> bytes = 0;
std::atomic<uint32_t> views = 0;

constexpr std::array SETTINGS = {
   Settings::Setting{
      .key = Settings::Core::UPGRADES.key,
      .label = "Texture Upgrades",
      .tooltip = "Luma's higher precision copies of the game's textures (mirrors) in place of the game's. Off shows the game's own formats; the mirrors are "
                 "kept, so switching back is immediate.",
      .category = "Resources",
      .visibility = Settings::Visibility::Development,
      .default_value = true,
   },
};

#if DEVELOPMENT
// Never held with another lock
std::mutex infos_mutex;
std::unordered_map<const void*, Info> infos;
#endif

uint32_t GetBitsPerTexel(DXGI_FORMAT format)
{
   switch (format)
   {
   case DXGI_FORMAT_R32G32B32A32_TYPELESS:
   case DXGI_FORMAT_R32G32B32A32_FLOAT:
   case DXGI_FORMAT_R32G32B32A32_UINT:
   case DXGI_FORMAT_R32G32B32A32_SINT: return 128;
   case DXGI_FORMAT_R16G16B16A16_TYPELESS:
   case DXGI_FORMAT_R16G16B16A16_FLOAT:
   case DXGI_FORMAT_R16G16B16A16_UNORM:
   case DXGI_FORMAT_R16G16B16A16_UINT:
   case DXGI_FORMAT_R16G16B16A16_SNORM:
   case DXGI_FORMAT_R16G16B16A16_SINT:
   case DXGI_FORMAT_R32G32_TYPELESS:
   case DXGI_FORMAT_R32G32_FLOAT:
   case DXGI_FORMAT_R32G8X24_TYPELESS:
   case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return 64;
   default: return 32;
   }
}
} // namespace

void Register()
{
   Settings::Register(SETTINGS, Settings::Kind::Luma);
   Settings::AddChangeListener([](const Settings::Setting& setting, const Settings::Value& value) {
      if (setting.key == Settings::Core::UPGRADES.key)
         upgrades = std::get<bool>(value);
   });
}

bool IsNeeded()
{
   return Upgrade::IsNeeded();
}

bool IsSwapping()
{
   return Settings::IsEnabled() && upgrades.load(std::memory_order_relaxed);
}

bool HasAnyMirrors()
{
   return mirrors.load(std::memory_order_relaxed) != 0;
}

Hooks::D3D11::Functions GetD3D11Functions()
{
   using enum Hooks::D3D11::Function;
   Hooks::D3D11::Functions functions;
   if (!IsNeeded())
      return functions;
   // Made with the textures and their views, swapped at every bind of a view, followed by writes, copies and clears
   for (const Hooks::D3D11::Function function :
      {CreateTexture1D, CreateTexture2D, CreateTexture3D, CreateShaderResourceView, CreateRenderTargetView, CreateUnorderedAccessView, CreateDepthStencilView,
         VSSetShaderResources, HSSetShaderResources, DSSetShaderResources, GSSetShaderResources, PSSetShaderResources, CSSetShaderResources,
         CSSetUnorderedAccessViews, OMSetRenderTargets, OMSetRenderTargetsAndUnorderedAccessViews, Map, Unmap, UpdateSubresource, CopyResource, CopySubresourceRegion,
         ResolveSubresource, ClearRenderTargetView, ClearDepthStencilView, ClearUnorderedAccessViewUint, ClearUnorderedAccessViewFloat, ClearView})
      functions.set(size_t(function));
   return functions;
}

Hooks::D3D12::Functions GetD3D12Functions()
{
   using enum Hooks::D3D12::Function;
   Hooks::D3D12::Functions functions;
   // Made with the resources, their views by descriptor, swapped at copies into shader-visible heaps, render target binds, copies and
   // clears; barriers given to the mirrors too; pipelines drawing into them get a format variant at the draw. A converting copy binds
   // back the root signature's, the targets' and the fixed state the game set.
   if (IsNeeded())
   {
      for (const Hooks::D3D12::Function function : {CreateResource, CreateDescriptorHeap, CreateView, CopyDescriptors, ResourceBarrier, OMSetRenderTargets, Copy,
              Clear, Reset, ClearState, SetPipelineState, Draw, ExecuteIndirect, SetRootSignature, SetDescriptorHeaps, SetFixedState})
         functions.set(size_t(function));
   }
   // Sampler mirrors the same way as views
   if (Samplers::IsNeeded())
   {
      for (const Hooks::D3D12::Function function : {CreateDescriptorHeap, CreateView, CopyDescriptors})
         functions.set(size_t(function));
   }
   return functions;
}

Stats GetStats()
{
   return {mirrors.load(), uint64_t(std::max<int64_t>(bytes.load(), 0)), views.load()};
}

void CountMirror(int32_t count, int64_t size)
{
   mirrors += uint32_t(count);
   bytes += size;
}

void CountView(int32_t count)
{
   views += uint32_t(count);
}

void LogCreationFailure(std::string_view what, HRESULT result)
{
   static std::atomic<uint32_t> failures = 0;
   if (failures++ < 8)
      Log::Warning("Mirrors: {} failed (0x{:08X}), the game's texture is used as it is", what, uint32_t(result));
}

uint64_t EstimateBytes(const Upgrade::TextureInfo& info)
{
   const uint64_t texel_bytes = GetBitsPerTexel(info.format) / 8;
   uint64_t total = 0;
   const bool volume = info.dimension == Upgrade::Dimension::Texture3D;
   for (uint32_t level = 0; level < std::max(info.levels, 1u); level++)
   {
      const uint64_t width = std::max(info.width >> level, 1u);
      const uint64_t height = std::max(info.height >> level, 1u);
      const uint64_t depth = volume ? std::max(info.depth_or_layers >> level, 1u) : info.depth_or_layers;
      total += width * height * depth * texel_bytes;
   }
   return total * std::max(info.samples, 1u);
}

DXGI_FORMAT GetResolveFormat(DXGI_FORMAT game_format, DXGI_FORMAT original_format, DXGI_FORMAT mirror_format)
{
   using namespace Utils::Format::DXGI;
   if (GetTypeless(game_format) == GetTypeless(mirror_format) && game_format != DXGI_FORMAT_UNKNOWN)
      return game_format;
   const DXGI_FORMAT mapped = Upgrade::GetBestViewFormat(game_format, Upgrade::ViewKind::ShaderResource, original_format, mirror_format);
   if (mapped != DXGI_FORMAT_UNKNOWN)
      return mapped;
   return IsTypeless(mirror_format) ? GetDefaultTyped(mirror_format) : mirror_format;
}

#if DEVELOPMENT
void AddInfo(const void* original, Info info)
{
   std::scoped_lock lock(infos_mutex);
   infos[original] = std::move(info);
}

void RemoveInfo(const void* original)
{
   std::scoped_lock lock(infos_mutex);
   infos.erase(original);
}

std::vector<Info> GetInfos()
{
   std::scoped_lock lock(infos_mutex);
   std::vector<Info> list;
   list.reserve(infos.size());
   for (const auto& [original, info] : infos)
      list.push_back(info);
   return list;
}
#endif
} // namespace Mirrors
} // namespace Resources
} // namespace Luma
