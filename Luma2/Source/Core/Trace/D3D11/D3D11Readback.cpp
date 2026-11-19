#include "Trace/D3D11/D3D11Readback.hpp"

#include "Swapchain/DXGI/DXGISwapchain.hpp"

#if DEVELOPMENT
#include <array>
#include <format>
#include <mutex>
#include <optional>
#include <string>

#include <wrl/client.h>

#include "Hooks/D3D11/D3D11Stages.hpp"
#include "Resources/D3D11/D3D11Mirrors.hpp"
#include "Resources/Mirrors.hpp"
#include "Shaders/D3D11/D3D11Variants.hpp"
#include "Shaders/Database.hpp"
#include "Trace/Readback.hpp"
#include "Utils/DXGI/DXGIFormat.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Trace {
namespace Readback {
namespace D3D11 {
namespace {
using namespace Utils::Format::DXGI;

// The view copied at the call, read at the next present
struct Copy
{
   ComPtr<ID3D11Resource> texture;
   DXGI_FORMAT view_format = DXGI_FORMAT_UNKNOWN;
   std::string resource;
   bool mirrored = false;
};

// Never held with another lock
std::mutex mutex;
std::optional<Copy> pending;

std::string GetId(const void* object)
{
   return std::format("0x{:X}", reinterpret_cast<uintptr_t>(object));
}

// A GPU copy of `source` (no bindings, not CPU readable: the staging copy is made at present, on the immediate context)
ComPtr<ID3D11Resource> CreateCopy(ID3D11Device* device, ID3D11Resource* source)
{
   D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
   source->GetType(&dimension);
   ComPtr<ID3D11Resource> copy;
   const auto strip = [](auto& desc) {
      desc.Usage = D3D11_USAGE_DEFAULT;
      desc.BindFlags = 0;
      desc.CPUAccessFlags = 0;
      desc.MiscFlags &= D3D11_RESOURCE_MISC_TEXTURECUBE;
   };
   if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
   {
      D3D11_TEXTURE2D_DESC desc;
      static_cast<ID3D11Texture2D*>(source)->GetDesc(&desc);
      strip(desc);
      device->CreateTexture2D(&desc, nullptr, reinterpret_cast<ID3D11Texture2D**>(copy.GetAddressOf()));
   }
   else if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D)
   {
      D3D11_TEXTURE3D_DESC desc;
      static_cast<ID3D11Texture3D*>(source)->GetDesc(&desc);
      strip(desc);
      device->CreateTexture3D(&desc, nullptr, reinterpret_cast<ID3D11Texture3D**>(copy.GetAddressOf()));
   }
   else if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE1D)
   {
      D3D11_TEXTURE1D_DESC desc;
      static_cast<ID3D11Texture1D*>(source)->GetDesc(&desc);
      strip(desc);
      device->CreateTexture1D(&desc, nullptr, reinterpret_cast<ID3D11Texture1D**>(copy.GetAddressOf()));
   }
   return copy;
}

struct Layout
{
   std::string dimension = "2d";
   UINT width = 1, height = 1, depth = 1, slices = 1, mips = 1, samples = 1;
   DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};

// The staging copy's desc and the texture's layout
template <typename Texture, typename Desc>
HRESULT CreateStaging(ID3D11Device* device, ID3D11Resource* source, Layout& layout, ComPtr<ID3D11Resource>& staging)
{
   Desc desc;
   static_cast<Texture*>(source)->GetDesc(&desc);
   layout.width = desc.Width;
   layout.mips = desc.MipLevels;
   layout.format = desc.Format;
   desc.Usage = D3D11_USAGE_STAGING;
   desc.BindFlags = 0;
   desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
   desc.MiscFlags = 0;
   if constexpr (std::is_same_v<Texture, ID3D11Texture3D>)
   {
      layout.dimension = "3d";
      layout.height = desc.Height;
      layout.depth = desc.Depth;
      return device->CreateTexture3D(&desc, nullptr, reinterpret_cast<ID3D11Texture3D**>(staging.GetAddressOf()));
   }
   else if constexpr (std::is_same_v<Texture, ID3D11Texture2D>)
   {
      layout.height = desc.Height;
      layout.slices = desc.ArraySize;
      desc.SampleDesc = {1, 0};
      return device->CreateTexture2D(&desc, nullptr, reinterpret_cast<ID3D11Texture2D**>(staging.GetAddressOf()));
   }
   else
   {
      layout.dimension = "1d";
      layout.slices = desc.ArraySize;
      return device->CreateTexture1D(&desc, nullptr, reinterpret_cast<ID3D11Texture1D**>(staging.GetAddressOf()));
   }
}

// 1.0's ReadTexture: one mip, every slice, multisampled textures resolved first. On the immediate context.
Result ReadTexture(ID3D11DeviceContext* context, ID3D11Resource* source, DXGI_FORMAT view_format, uint32_t mip)
{
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
   source->GetType(&dimension);
   ComPtr<ID3D11Resource> copy_source = source;
   UINT samples = 1;
   if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
   {
      D3D11_TEXTURE2D_DESC desc;
      static_cast<ID3D11Texture2D*>(source)->GetDesc(&desc);
      samples = desc.SampleDesc.Count;
      if (samples > 1)
      {
         // Staging textures can't be multisampled: every slice is resolved first, in a format that resolves
         DXGI_FORMAT resolve_format = view_format != DXGI_FORMAT_UNKNOWN ? view_format : desc.Format;
         resolve_format = IsTypeless(resolve_format) ? GetDefaultTyped(resolve_format) : resolve_format;
         D3D11_TEXTURE2D_DESC resolved_desc = desc;
         resolved_desc.SampleDesc = {1, 0};
         resolved_desc.Format = resolve_format;
         resolved_desc.BindFlags = 0;
         resolved_desc.CPUAccessFlags = 0;
         resolved_desc.MiscFlags = 0;
         resolved_desc.MipLevels = 1;
         ComPtr<ID3D11Texture2D> resolved;
         if (IsTypeless(resolve_format) || FAILED(device->CreateTexture2D(&resolved_desc, nullptr, &resolved)))
            return {.error = std::format("the multisampled texture can't be resolved for readback (format {})", uint32_t(resolve_format))};
         for (UINT slice = 0; slice < desc.ArraySize; slice++)
            context->ResolveSubresource(resolved.Get(), slice, source, D3D11CalcSubresource(0, slice, desc.MipLevels), resolve_format);
         copy_source = resolved;
         mip = 0;
      }
   }
   Layout layout;
   ComPtr<ID3D11Resource> staging;
   HRESULT hr = E_FAIL;
   if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
      hr = CreateStaging<ID3D11Texture2D, D3D11_TEXTURE2D_DESC>(device.Get(), copy_source.Get(), layout, staging);
   else if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D)
      hr = CreateStaging<ID3D11Texture3D, D3D11_TEXTURE3D_DESC>(device.Get(), copy_source.Get(), layout, staging);
   else if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE1D)
      hr = CreateStaging<ID3D11Texture1D, D3D11_TEXTURE1D_DESC>(device.Get(), copy_source.Get(), layout, staging);
   else
      return {.error = "the view is of a buffer, not a texture"};
   if (FAILED(hr) || !staging)
      return {.error = std::format("the staging texture couldn't be created (0x{:08X})", uint32_t(hr))};
   if (mip >= layout.mips)
      return {.error = std::format("mip {} is out of range ({} mips)", mip, layout.mips)};
   if (view_format == DXGI_FORMAT_UNKNOWN)
      view_format = layout.format;
   context->CopyResource(staging.Get(), copy_source.Get());

   Texture texture = {.dimension = layout.dimension,
      .width = std::max(layout.width >> mip, 1u),
      .height = std::max(layout.height >> mip, 1u),
      .depth = std::max(layout.depth >> mip, 1u),
      .slices = layout.slices,
      .mip = mip,
      .mips = layout.mips,
      .samples = samples,
      .format = uint32_t(layout.format),
      .view_format = uint32_t(view_format),
      .block_compressed = IsBlockCompressed(layout.format)};
   texture.rows = texture.block_compressed ? (texture.height + 3) / 4 : texture.height;
   for (UINT slice = 0; slice < layout.slices; slice++)
   {
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      const UINT subresource = D3D11CalcSubresource(mip, slice, layout.mips);
      // Stalls until the GPU caught up: fine for a one off read
      hr = context->Map(staging.Get(), subresource, D3D11_MAP_READ, 0, &mapped);
      if (FAILED(hr))
         return {.error = std::format("the staging texture couldn't be mapped (0x{:08X})", uint32_t(hr))};
      texture.row_pitch = mapped.RowPitch;
      const size_t plane_bytes = size_t(texture.rows) * mapped.RowPitch;
      texture.bytes.reserve(plane_bytes * texture.depth * layout.slices);
      for (UINT z = 0; z < texture.depth; z++)
      {
         const auto* plane = static_cast<const std::byte*>(mapped.pData) + size_t(z) * mapped.DepthPitch;
         texture.bytes.insert(texture.bytes.end(), plane, plane + plane_bytes);
      }
      context->Unmap(staging.Get(), subresource);
   }
   return {.texture = std::move(texture)};
}

// The view the call has at the target's slot, holding its resource, and the format to read it as
struct Found
{
   ComPtr<ID3D11Resource> resource;
   DXGI_FORMAT view_format = DXGI_FORMAT_UNKNOWN;
};

template <typename Desc, typename View>
Found FromView(View* view)
{
   Found found;
   if (!view)
      return found;
   view->GetResource(&found.resource);
   Desc desc;
   view->GetDesc(&desc);
   found.view_format = desc.Format;
   return found;
}

Found FindView(ID3D11DeviceContext* context, const Target& target, bool compute)
{
   const UINT slot = target.slot;
   switch (target.view)
   {
   case ViewKind::RenderTarget:
   {
      if (compute || slot >= D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT)
         return {};
      std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> views = {};
      context->OMGetRenderTargets(UINT(views.size()), views.data(), nullptr);
      Found found = FromView<D3D11_RENDER_TARGET_VIEW_DESC>(views[slot]);
      for (ID3D11RenderTargetView* view : views)
      {
         if (view)
            view->Release();
      }
      return found;
   }
   case ViewKind::Depth:
   case ViewKind::Stencil:
   {
      if (compute)
         return {};
      ComPtr<ID3D11DepthStencilView> view;
      context->OMGetRenderTargets(0, nullptr, &view);
      Found found = FromView<D3D11_DEPTH_STENCIL_VIEW_DESC>(view.Get());
      found.view_format = GetDepthReadFormat(found.view_format, target.view == ViewKind::Stencil);
      return found;
   }
   case ViewKind::ShaderResource:
   {
      if (slot >= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT)
         return {};
      ComPtr<ID3D11ShaderResourceView> view;
      Hooks::D3D11::WithStage(target.stage, [&]<ShaderStage Stage>() {
         (context->*Hooks::D3D11::StageTraits<Stage>::GetShaderResources)(slot, 1, view.GetAddressOf());
      });
      return FromView<D3D11_SHADER_RESOURCE_VIEW_DESC>(view.Get());
   }
   case ViewKind::UnorderedAccess:
   {
      if (slot >= D3D11_1_UAV_SLOT_COUNT)
         return {};
      ComPtr<ID3D11UnorderedAccessView> view;
      if (compute)
         context->CSGetUnorderedAccessViews(slot, 1, &view);
      else
         context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, slot, 1, &view);
      return FromView<D3D11_UNORDERED_ACCESS_VIEW_DESC>(view.Get());
   }
   default: return {};
   }
}
} // namespace

bool ReadAfterCall(const Game::D3D11::Call& call, Shaders::StageSet stages, bool handled)
{
   const std::optional<Target> target = GetTarget();
   if (!target || target->view == ViewKind::Swapchain || !stages.Has(target->stage))
      return handled;
   const Shaders::Variants::D3D11::BoundShaders* bound = Shaders::Variants::D3D11::GetBoundShaders(call.context);
   const Shaders::Database::Shader* shader = bound ? bound->shaders[size_t(target->stage)] : nullptr;
   if (!shader || shader->hash != target->hash || !CountCall())
      return handled;
   // The views are read once the call wrote them
   if (!handled)
      Game::D3D11::RunOriginal(call);
   const bool compute = call.dispatch != nullptr;
   Found found = FindView(call.context, *target, compute);
   // The bound view is a mirror's: named after the game's texture, which `original` reads instead
   ComPtr<ID3D11Resource> named = found.resource;
   bool mirrored = false;
   if (ID3D11Resource* original = found.resource ? Resources::Mirrors::D3D11::GetOriginal(found.resource.Get()) : nullptr)
   {
      named = original;
      mirrored = !target->original;
      if (target->original)
      {
         found.resource = original;
         found.view_format = DXGI_FORMAT_UNKNOWN;
      }
   }
   if (!found.resource)
   {
      // Reported at the present: the slot was empty at the call
      MarkCopied();
      Finish({.error = std::format("the call has no {} at slot {}", GetName(target->view), target->slot)});
      return true;
   }
   ComPtr<ID3D11Device> device;
   call.context->GetDevice(&device);
   Copy copy = {.texture = CreateCopy(device.Get(), found.resource.Get()), .view_format = found.view_format, .resource = GetId(named.Get()), .mirrored = mirrored};
   if (!copy.texture)
   {
      MarkCopied();
      Finish({.error = "the view is of a buffer, or its copy couldn't be created"});
      return true;
   }
   call.context->CopyResource(copy.texture.Get(), found.resource.Get());
   if (call.context->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED)
   {
      MarkCopied();
      Finish({.error = "the call is on a deferred context, which readbacks don't follow yet"});
      return true;
   }
   {
      std::scoped_lock lock(mutex);
      pending = std::move(copy);
   }
   MarkCopied();
   return true;
}

void ReadBack(IDXGISwapChain* swapchain, ID3D11Device* device)
{
   if (!IsArmed())
      return;
   const std::optional<Target> target = GetTarget();
   if (!target)
      return;
   ComPtr<ID3D11DeviceContext> context;
   device->GetImmediateContext(&context);
   if (target->view == ViewKind::Swapchain)
   {
      // The game's image: its own buffer where Luma composes into the real one
      ComPtr<ID3D11Texture2D> back_buffer;
      const ComPtr<IUnknown> game_buffer = Swapchain::DXGI::GetGameBuffer(swapchain);
      if (game_buffer ? FAILED(game_buffer.As(&back_buffer)) : FAILED(swapchain->GetBuffer(0, IID_PPV_ARGS(&back_buffer))))
         return Finish({.error = "the swapchain has no back buffer"});
      // What the game drew is in its buffer's mirror while mirrors are swapped in
      if (ID3D11Resource* mirror = Resources::Mirrors::IsSwapping() ? Resources::Mirrors::D3D11::GetMirror(back_buffer.Get()) : nullptr)
         mirror->QueryInterface(IID_PPV_ARGS(back_buffer.ReleaseAndGetAddressOf()));
      Result result = ReadTexture(context.Get(), back_buffer.Get(), DXGI_FORMAT_UNKNOWN, target->mip);
      if (result.texture)
         result.texture->resource = GetId(back_buffer.Get());
      return Finish(std::move(result));
   }
   std::optional<Copy> copy;
   {
      std::scoped_lock lock(mutex);
      copy = std::exchange(pending, std::nullopt);
   }
   if (!copy)
      return;
   Result result = ReadTexture(context.Get(), copy->texture.Get(), copy->view_format, target->mip);
   if (result.texture)
   {
      result.texture->resource = std::move(copy->resource);
      result.texture->mirrored = copy->mirrored;
   }
   Finish(std::move(result));
}
} // namespace D3D11
} // namespace Readback
} // namespace Trace
} // namespace Luma
#endif
