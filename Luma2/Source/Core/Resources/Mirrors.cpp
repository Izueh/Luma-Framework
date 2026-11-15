#include "Resources/Mirrors.hpp"

#include <array>
#include <atomic>
#include <mutex>
#include <unordered_map>

#include "Luma.hpp"
#include "Settings/Keys.hpp"
#include "Settings/Registry.hpp"

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
   return IsEnabled() && upgrades.load(std::memory_order_relaxed);
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
         CSSetUnorderedAccessViews, OMSetRenderTargets, OMSetRenderTargetsAndUnorderedAccessViews, UpdateSubresource, CopyResource, CopySubresourceRegion,
         ResolveSubresource, ClearRenderTargetView, ClearDepthStencilView, ClearUnorderedAccessViewUint, ClearUnorderedAccessViewFloat, ClearView})
      functions.set(size_t(function));
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

uint64_t EstimateBytes(const Upgrade::TextureDesc& desc)
{
   const uint64_t texel_bytes = GetBitsPerTexel(desc.format) / 8;
   uint64_t total = 0;
   const bool volume = desc.dimension == Upgrade::Dimension::Texture3D;
   for (uint32_t level = 0; level < std::max(desc.levels, 1u); level++)
   {
      const uint64_t width = std::max(desc.width >> level, 1u);
      const uint64_t height = std::max(desc.height >> level, 1u);
      const uint64_t depth = volume ? std::max(desc.depth_or_layers >> level, 1u) : desc.depth_or_layers;
      total += width * height * depth * texel_bytes;
   }
   return total * std::max(desc.samples, 1u);
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
