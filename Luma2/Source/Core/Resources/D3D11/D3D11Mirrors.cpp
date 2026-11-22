#include "Resources/D3D11/D3D11Mirrors.hpp"

#include <cstring>
#include <format>
#include <optional>
#include <type_traits>
#include <vector>

#include <wrl/client.h>

#include "Draw/D3D11/D3D11Draw.hpp"
#include "Frame/Frame.hpp"
#include "Hooks/Lifetime.hpp"
#include "Resources/Mirrors.hpp"
#include "Resources/Upgrade.hpp"
#include "Shaders/Shaders.hpp"
#include "Utils/Debug.hpp"
#include "Utils/DXGI/DXGIFormat.hpp"
#include "Utils/Format.hpp"
#include "Utils/Log.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Resources {
namespace Mirrors {
namespace D3D11 {
namespace {
using namespace Utils::Format::DXGI;
using OwnedResource = ComPtr<ID3D11Resource>;
using ResourceMirrorState = MirrorState<ID3D11Resource, OwnedResource>;

struct ViewState
{
   ComPtr<ID3D11View> mirror;

   ~ViewState() { CountView(-1); }
};

Upgrade::Dimension ToDimension(D3D11_RESOURCE_DIMENSION dimension)
{
   switch (dimension)
   {
   case D3D11_RESOURCE_DIMENSION_TEXTURE1D: return Upgrade::Dimension::Texture1D;
   case D3D11_RESOURCE_DIMENSION_TEXTURE3D: return Upgrade::Dimension::Texture3D;
   default: return Upgrade::Dimension::Texture2D;
   }
}

template <typename Desc>
Upgrade::TextureInfo Describe(const Desc& desc)
{
   Upgrade::TextureInfo described = {.width = desc.Width, .levels = desc.MipLevels, .format = desc.Format};
   if constexpr (std::is_same_v<Desc, D3D11_TEXTURE1D_DESC>)
   {
      described.dimension = Upgrade::Dimension::Texture1D;
      described.depth_or_layers = desc.ArraySize;
   }
   else if constexpr (std::is_same_v<Desc, D3D11_TEXTURE2D_DESC1>)
   {
      described.height = desc.Height;
      described.depth_or_layers = desc.ArraySize;
      described.samples = desc.SampleDesc.Count;
      described.cube = (desc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) != 0 && desc.ArraySize % 6 == 0;
   }
   else
   {
      described.dimension = Upgrade::Dimension::Texture3D;
      described.height = desc.Height;
      described.depth_or_layers = desc.Depth;
   }
   described.render_target = (desc.BindFlags & D3D11_BIND_RENDER_TARGET) != 0;
   described.unordered_access = (desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0;
   described.depth_stencil = (desc.BindFlags & D3D11_BIND_DEPTH_STENCIL) != 0;
   described.gpu_only = desc.Usage == D3D11_USAGE_DEFAULT || desc.Usage == D3D11_USAGE_IMMUTABLE;
   return described;
}

// The resolutions the size filters compare with: the device's (Frame)
Upgrade::FrameState GetFrameState(ID3D11Device* device)
{
   const Frame::Resolutions resolutions = Frame::Read(Frame::Find(device));
   return {.render = resolutions.render, .output = resolutions.output, .display = resolutions.display};
}

uint32_t MipSize(uint32_t size, uint32_t level)
{
   return std::max(size >> level, 1u);
}

// The game's initial data, every subresource converted into the mirror's format. None when there's nothing to convert or it can't be.
struct ConvertedData
{
   std::vector<Converted> subresources;
   std::vector<D3D11_SUBRESOURCE_DATA> data;
};

std::optional<ConvertedData> ConvertInitialData(const Upgrade::TextureInfo& desc, DXGI_FORMAT mirror_format, const D3D11_SUBRESOURCE_DATA* initial_data)
{
   if (!initial_data || mirror_format != DXGI_FORMAT_R16G16B16A16_FLOAT)
      return std::nullopt;
   const bool volume = desc.dimension == Upgrade::Dimension::Texture3D;
   const uint32_t layers = volume ? 1 : desc.depth_or_layers;
   ConvertedData converted;
   for (uint32_t layer = 0; layer < layers; layer++)
   {
      for (uint32_t level = 0; level < desc.levels; level++)
      {
         const D3D11_SUBRESOURCE_DATA& source = initial_data[layer * desc.levels + level];
         std::optional<Converted> subresource = ConvertToRgba16Float(source.pSysMem, source.SysMemPitch, source.SysMemSlicePitch, desc.format, MipSize(desc.width, level),
            MipSize(desc.height, level), volume ? MipSize(desc.depth_or_layers, level) : 1);
         if (!subresource)
            return std::nullopt;
         converted.subresources.push_back(std::move(*subresource));
      }
   }
   for (const Converted& subresource : converted.subresources)
      converted.data.push_back({subresource.bytes.data(), subresource.row_pitch, subresource.depth_pitch});
   return converted;
}

// What a mirror is created with besides the game's desc: readable by Luma's passes (converting copies, readbacks), shared flags gone
template <typename Desc>
Desc MakeMirrorDesc(Desc desc, DXGI_FORMAT format)
{
   desc.Format = format;
   desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
   desc.MiscFlags &= D3D11_RESOURCE_MISC_TEXTURECUBE | D3D11_RESOURCE_MISC_GENERATE_MIPS | D3D11_RESOURCE_MISC_RESOURCE_CLAMP;
   desc.CPUAccessFlags = 0;
   desc.Usage = D3D11_USAGE_DEFAULT;
   return desc;
}

template <typename Desc, typename Texture>
void Create(ID3D11Device* device, const Desc& desc, const D3D11_SUBRESOURCE_DATA* initial_data, Texture* texture)
{
   const Upgrade::TextureInfo described = Describe(desc);
   const std::optional<Upgrade::TextureInfo> mirrored = Upgrade::Match(described, GetFrameState(device), initial_data != nullptr);
   if (!mirrored)
      return;
   const std::optional<ConvertedData> data = ConvertInitialData(described, mirrored->format, initial_data);
   ASSERT_ONCE_MSG(!initial_data || data, "A texture matching the upgrade rules has initial data that can't be converted to its mirror's format");
   ComPtr<ID3D11Resource> mirror;
   HRESULT result = E_FAIL;
   if constexpr (std::is_same_v<Desc, D3D11_TEXTURE1D_DESC>)
   {
      const D3D11_TEXTURE1D_DESC mirror_desc = MakeMirrorDesc(desc, mirrored->format);
      result = device->CreateTexture1D(&mirror_desc, data ? data->data.data() : nullptr, reinterpret_cast<ID3D11Texture1D**>(mirror.GetAddressOf()));
   }
   else if constexpr (std::is_same_v<Desc, D3D11_TEXTURE2D_DESC1>)
   {
      D3D11_TEXTURE2D_DESC mirror_desc;
      std::memcpy(&mirror_desc, &desc, sizeof(mirror_desc));
      mirror_desc = MakeMirrorDesc(mirror_desc, mirrored->format);
      result = device->CreateTexture2D(&mirror_desc, data ? data->data.data() : nullptr, reinterpret_cast<ID3D11Texture2D**>(mirror.GetAddressOf()));
   }
   else
   {
      D3D11_TEXTURE3D_DESC mirror_desc;
      std::memcpy(&mirror_desc, &desc, sizeof(mirror_desc));
      mirror_desc = MakeMirrorDesc(mirror_desc, mirrored->format);
      result = device->CreateTexture3D(&mirror_desc, data ? data->data.data() : nullptr, reinterpret_cast<ID3D11Texture3D**>(mirror.GetAddressOf()));
   }
   if (FAILED(result) || !mirror)
      return LogCreationFailure(std::format("a {} mirror of a {} texture", Utils::Format::GetName(mirrored->format), Utils::Format::GetName(desc.Format)), result);
   AttachMirror<ID3D11Resource>(texture, std::move(mirror), described, *mirrored);
}

template <typename Desc>
Desc ToBaseDesc(const auto& desc1)
{
   Desc desc;
   std::memcpy(&desc, &desc1, sizeof(desc));
   return desc;
}

Upgrade::ViewKind GetViewKind(const D3D11_SHADER_RESOURCE_VIEW_DESC1*)
{
   return Upgrade::ViewKind::ShaderResource;
}

Upgrade::ViewKind GetViewKind(const D3D11_RENDER_TARGET_VIEW_DESC1*)
{
   return Upgrade::ViewKind::RenderTarget;
}

Upgrade::ViewKind GetViewKind(const D3D11_UNORDERED_ACCESS_VIEW_DESC1*)
{
   return Upgrade::ViewKind::UnorderedAccess;
}

Upgrade::ViewKind GetViewKind(const D3D11_DEPTH_STENCIL_VIEW_DESC*)
{
   return Upgrade::ViewKind::DepthStencil;
}

HRESULT CreateView(ID3D11Device* device, ID3D11Resource* mirror, const D3D11_SHADER_RESOURCE_VIEW_DESC1* desc, ComPtr<ID3D11View>& view)
{
   const D3D11_SHADER_RESOURCE_VIEW_DESC base = desc ? ToBaseDesc<D3D11_SHADER_RESOURCE_VIEW_DESC>(*desc) : D3D11_SHADER_RESOURCE_VIEW_DESC{};
   return device->CreateShaderResourceView(mirror, desc ? &base : nullptr, reinterpret_cast<ID3D11ShaderResourceView**>(view.GetAddressOf()));
}

HRESULT CreateView(ID3D11Device* device, ID3D11Resource* mirror, const D3D11_RENDER_TARGET_VIEW_DESC1* desc, ComPtr<ID3D11View>& view)
{
   const D3D11_RENDER_TARGET_VIEW_DESC base = desc ? ToBaseDesc<D3D11_RENDER_TARGET_VIEW_DESC>(*desc) : D3D11_RENDER_TARGET_VIEW_DESC{};
   return device->CreateRenderTargetView(mirror, desc ? &base : nullptr, reinterpret_cast<ID3D11RenderTargetView**>(view.GetAddressOf()));
}

HRESULT CreateView(ID3D11Device* device, ID3D11Resource* mirror, const D3D11_UNORDERED_ACCESS_VIEW_DESC1* desc, ComPtr<ID3D11View>& view)
{
   const D3D11_UNORDERED_ACCESS_VIEW_DESC base = desc ? ToBaseDesc<D3D11_UNORDERED_ACCESS_VIEW_DESC>(*desc) : D3D11_UNORDERED_ACCESS_VIEW_DESC{};
   return device->CreateUnorderedAccessView(mirror, desc ? &base : nullptr, reinterpret_cast<ID3D11UnorderedAccessView**>(view.GetAddressOf()));
}

HRESULT CreateView(ID3D11Device* device, ID3D11Resource* mirror, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc, ComPtr<ID3D11View>& view)
{
   return device->CreateDepthStencilView(mirror, desc, reinterpret_cast<ID3D11DepthStencilView**>(view.GetAddressOf()));
}

template <typename Desc, typename View>
void CreateMirroredView(ID3D11Device* device, ID3D11Resource* resource, const Desc* desc, View* view)
{
   const ResourceMirrorState* state = FindMirrorState<OwnedResource>(resource);
   if (!state || !view)
      return;
   std::optional<Desc> mirror_desc;
   if (desc)
   {
      mirror_desc = *desc;
      const DXGI_FORMAT format = Upgrade::GetBestViewFormat(desc->Format, GetViewKind(desc), state->original_info.format, state->info.format);
      mirror_desc->Format = format != DXGI_FORMAT_UNKNOWN ? format : desc->Format;
   }
   ComPtr<ID3D11View> mirror_view;
   const HRESULT result = CreateView(device, state->mirror.Get(), mirror_desc ? &*mirror_desc : nullptr, mirror_view);
   if (FAILED(result) || !mirror_view)
      return LogCreationFailure(std::format("a view of a {} mirror", Utils::Format::GetName(state->info.format)), result);
   CountView(1);
   Hooks::Lifetime::SetState<ViewState>(view, std::move(mirror_view));
}

template <typename View>
View* FindMirrorView(View* view)
{
   const ViewState* state = view ? Hooks::Lifetime::GetState<ViewState>(view) : nullptr;
   return state ? static_cast<View*>(state->mirror.Get()) : nullptr;
}

template <typename View>
void Swap(View** views, UINT count)
{
   if (!views || !GetStats().views)
      return;
   for (UINT i = 0; i < count; i++)
   {
      if (View* mirror = FindMirrorView(views[i]))
         views[i] = mirror;
   }
}

// A format a shader samples or a render target writes a resource of `format` as, raw values: typeless gets its typed family, sRGB its
// UNORM
DXGI_FORMAT GetRawFormat(DXGI_FORMAT format)
{
   switch (format)
   {
   case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
   case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
   case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8X8_UNORM;
   default: return IsTypeless(format) ? GetDefaultTyped(format) : format;
   }
}

// A copy of `texture`'s desc, single sampled, one mip, bound as `bind`
ComPtr<ID3D11Texture2D> CreateProxy(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& desc, UINT bind)
{
   D3D11_TEXTURE2D_DESC proxy = desc;
   proxy.MipLevels = 1;
   proxy.ArraySize = 1;
   proxy.SampleDesc = {1, 0};
   proxy.Usage = D3D11_USAGE_DEFAULT;
   proxy.BindFlags = bind;
   proxy.CPUAccessFlags = 0;
   proxy.MiscFlags = 0;
   ComPtr<ID3D11Texture2D> texture;
   device->CreateTexture2D(&proxy, nullptr, &texture);
   return texture;
}

// A copy between formats CopyResource can't take (a mirror's and an original's), drawn by Luma's copy
// pass. Single sampled 2D textures, one mip and slice; the size may differ (then scaled). False when it can't.
bool ConvertingCopy(ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source)
{
   ComPtr<ID3D11Texture2D> source_texture;
   ComPtr<ID3D11Texture2D> destination_texture;
   if (FAILED(source->QueryInterface(IID_PPV_ARGS(&source_texture))) || FAILED(destination->QueryInterface(IID_PPV_ARGS(&destination_texture))))
      return false;
   D3D11_TEXTURE2D_DESC source_desc;
   D3D11_TEXTURE2D_DESC destination_desc;
   source_texture->GetDesc(&source_desc);
   destination_texture->GetDesc(&destination_desc);
   if (source_desc.SampleDesc.Count != 1 || destination_desc.SampleDesc.Count != 1 || source_desc.MipLevels != 1 || destination_desc.MipLevels != 1 ||
      source_desc.ArraySize != 1 || destination_desc.ArraySize != 1)
      return false;
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   // Through proxies where a side can't be read or drawn to, made per copy
   ComPtr<ID3D11Texture2D> read = source_texture;
   if (!(source_desc.BindFlags & D3D11_BIND_SHADER_RESOURCE))
   {
      read = CreateProxy(device.Get(), source_desc, D3D11_BIND_SHADER_RESOURCE);
      if (!read)
         return false;
      context->CopyResource(read.Get(), source_texture.Get());
   }
   ComPtr<ID3D11Texture2D> write = destination_texture;
   if (!(destination_desc.BindFlags & D3D11_BIND_RENDER_TARGET))
   {
      write = CreateProxy(device.Get(), destination_desc, D3D11_BIND_RENDER_TARGET);
      if (!write)
         return false;
   }
   D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {.Format = GetRawFormat(source_desc.Format), .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D};
   srv_desc.Texture2D = {0, 1};
   D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {.Format = GetRawFormat(destination_desc.Format), .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D};
   ComPtr<ID3D11ShaderResourceView> srv;
   ComPtr<ID3D11RenderTargetView> rtv;
   if (FAILED(device->CreateShaderResourceView(read.Get(), &srv_desc, &srv)) || FAILED(device->CreateRenderTargetView(write.Get(), &rtv_desc, &rtv)))
      return false;
   const bool same_size = source_desc.Width == destination_desc.Width && source_desc.Height == destination_desc.Height;
   if (!(same_size ? Draw::D3D11::Copy(context, srv.Get(), rtv.Get()) : Draw::D3D11::Scale(context, srv.Get(), rtv.Get())))
      return false;
   if (write != destination_texture)
      context->CopyResource(destination_texture.Get(), write.Get());
   return true;
}

DXGI_FORMAT GetFormat(ID3D11Resource* resource)
{
   D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
   resource->GetType(&dimension);
   switch (dimension)
   {
   case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
   {
      D3D11_TEXTURE1D_DESC desc;
      static_cast<ID3D11Texture1D*>(resource)->GetDesc(&desc);
      return desc.Format;
   }
   case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
   {
      D3D11_TEXTURE2D_DESC desc;
      static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
      return desc.Format;
   }
   case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
   {
      D3D11_TEXTURE3D_DESC desc;
      static_cast<ID3D11Texture3D*>(resource)->GetDesc(&desc);
      return desc.Format;
   }
   default: return DXGI_FORMAT_UNKNOWN;
   }
}
} // namespace

void CreateMirror(ID3D11Device* device, const D3D11_TEXTURE1D_DESC& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture1D* texture)
{
   Create(device, desc, initial_data, texture);
}

void CreateMirror(ID3D11Device* device, const D3D11_TEXTURE2D_DESC1& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture2D* texture)
{
   Create(device, desc, initial_data, texture);
}

void CreateMirror(ID3D11Device* device, const D3D11_TEXTURE3D_DESC1& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture3D* texture)
{
   Create(device, desc, initial_data, texture);
}

void CreateMirrorView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_SHADER_RESOURCE_VIEW_DESC1* desc, ID3D11ShaderResourceView* view)
{
   CreateMirroredView(device, resource, desc, view);
}

void CreateMirrorView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_RENDER_TARGET_VIEW_DESC1* desc, ID3D11RenderTargetView* view)
{
   CreateMirroredView(device, resource, desc, view);
}

void CreateMirrorView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC1* desc, ID3D11UnorderedAccessView* view)
{
   CreateMirroredView(device, resource, desc, view);
}

void CreateMirrorView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc, ID3D11DepthStencilView* view)
{
   CreateMirroredView(device, resource, desc, view);
}

void SwapViews(ID3D11ShaderResourceView** views, UINT count)
{
   Swap(views, count);
}

void SwapViews(ID3D11RenderTargetView** views, UINT count)
{
   Swap(views, count);
}

void SwapViews(ID3D11UnorderedAccessView** views, UINT count)
{
   Swap(views, count);
}

void SwapView(ID3D11DepthStencilView*& view)
{
   if (!GetStats().views)
      return;
   if (ID3D11DepthStencilView* mirror = FindMirrorView(view))
      view = mirror;
}

ID3D11Resource* GetMirror(ID3D11Resource* resource)
{
   return FindMirror<OwnedResource>(resource);
}

ID3D11Resource* GetOriginal(ID3D11Resource* mirror)
{
   return FindOriginal(mirror);
}

bool RedirectCopy(ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source)
{
   if (!IsSwapping())
      return false;
   const CopyPlan<ID3D11Resource> plan = PlanCopy(destination, source, &GetMirror, &GetFormat);
   if (plan.action == CopyAction::Original)
      return false;
   if (plan.action == CopyAction::DirectMirrorCopy)
   {
      context->CopyResource(plan.destination, plan.source);
      return true;
   }
   if (ConvertingCopy(context, plan.destination, plan.source))
      return true;
   ASSERT_ONCE_MSG(false, "A copy between a mirror and a texture of another format couldn't be converted: the game's textures are copied, the mirror is stale");
   return false;
}

bool RedirectCopyRegion(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, UINT x, UINT y, UINT z, ID3D11Resource* source,
   UINT source_subresource, const D3D11_BOX* box, UINT copy_flags)
{
   if (!IsSwapping())
      return false;
   const CopyPlan<ID3D11Resource> plan = PlanCopy(destination, source, &GetMirror, &GetFormat);
   if (plan.action == CopyAction::Original)
      return false;
   if (plan.action == CopyAction::DirectMirrorCopy)
   {
      ComPtr<ID3D11DeviceContext1> context1;
      if (copy_flags && SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&context1))))
         context1->CopySubresourceRegion1(plan.destination, destination_subresource, x, y, z, plan.source, source_subresource, box, copy_flags);
      else
         context->CopySubresourceRegion(plan.destination, destination_subresource, x, y, z, plan.source, source_subresource, box);
      return true;
   }
   if (!box && x == 0 && y == 0 && z == 0 && destination_subresource == 0 && source_subresource == 0 && ConvertingCopy(context, plan.destination, plan.source))
      return true;
   ASSERT_ONCE_MSG(false, "A region copy between a mirror and a texture of another format isn't converted: the game's textures are copied, the mirror is stale");
   return false;
}

bool RedirectResolve(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, ID3D11Resource* source, UINT source_subresource,
   DXGI_FORMAT format)
{
   if (!IsSwapping())
      return false;
   const CopyPlan<ID3D11Resource> plan = PlanCopy(destination, source, &GetMirror, &GetFormat);
   if (plan.action == CopyAction::Original)
      return false;
   const DXGI_FORMAT destination_format = GetFormat(plan.destination);
   const DXGI_FORMAT source_format = GetFormat(plan.source);
   if (GetTypeless(destination_format) != GetTypeless(source_format) || destination_format == DXGI_FORMAT_UNKNOWN)
   {
      LUMA_UNIMPLEMENTED("Resources::Mirrors resolving between a mirror and a texture of another format");
      return false;
   }
   const DXGI_FORMAT resolve_format = GetResolveFormat(format, GetFormat(source), destination_format);
   context->ResolveSubresource(plan.destination, destination_subresource, plan.source, source_subresource, resolve_format);
   return true;
}

bool RedirectClear(ID3D11DeviceContext* context, ID3D11RenderTargetView* view, const FLOAT color[4])
{
   if (!IsSwapping())
      return false;
   ID3D11RenderTargetView* mirror = FindMirrorView(view);
   if (!mirror)
      return false;
   context->ClearRenderTargetView(mirror, color);
   return true;
}

bool RedirectClear(ID3D11DeviceContext* context, ID3D11DepthStencilView* view, UINT flags, FLOAT depth, UINT8 stencil)
{
   if (!IsSwapping())
      return false;
   ID3D11DepthStencilView* mirror = FindMirrorView(view);
   if (!mirror)
      return false;
   context->ClearDepthStencilView(mirror, flags, depth, stencil);
   return true;
}

bool RedirectClear(ID3D11DeviceContext* context, ID3D11UnorderedAccessView* view, const UINT values[4])
{
   if (!IsSwapping())
      return false;
   ID3D11UnorderedAccessView* mirror = FindMirrorView(view);
   if (!mirror)
      return false;
   context->ClearUnorderedAccessViewUint(mirror, values);
   return true;
}

bool RedirectClear(ID3D11DeviceContext* context, ID3D11UnorderedAccessView* view, const FLOAT values[4])
{
   if (!IsSwapping())
      return false;
   ID3D11UnorderedAccessView* mirror = FindMirrorView(view);
   if (!mirror)
      return false;
   context->ClearUnorderedAccessViewFloat(mirror, values);
   return true;
}

bool RedirectClearView(ID3D11DeviceContext* context, ID3D11View* view, const FLOAT color[4], const D3D11_RECT* rects, UINT rect_count)
{
   if (!IsSwapping())
      return false;
   ID3D11View* mirror = FindMirrorView(view);
   ComPtr<ID3D11DeviceContext1> context1;
   if (!mirror || FAILED(context->QueryInterface(IID_PPV_ARGS(&context1))))
      return false;
   context1->ClearView(mirror, color, rects, rect_count);
   return true;
}

void WriteToMirror(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, const D3D11_BOX* box, const void* data, UINT row_pitch, UINT depth_pitch)
{
   const ResourceMirrorState* state = FindMirrorState<OwnedResource>(resource);
   if (!state || !data)
      return;
   const Upgrade::TextureInfo& desc = state->original_info;
   const uint32_t level = subresource % std::max(desc.levels, 1u);
   const bool volume = desc.dimension == Upgrade::Dimension::Texture3D;
   const uint32_t width = box ? box->right - box->left : MipSize(desc.width, level);
   const uint32_t height = box ? box->bottom - box->top : MipSize(desc.height, level);
   const uint32_t depth = box ? box->back - box->front : volume ? MipSize(desc.depth_or_layers, level)
                                                                : 1;
   std::optional<Converted> converted;
   if (state->info.format == DXGI_FORMAT_R16G16B16A16_FLOAT)
      converted = ConvertToRgba16Float(data, row_pitch, depth_pitch, desc.format, width, height, depth);
   if (!converted)
   {
      ASSERT_ONCE_MSG(false, "CPU data written to a mirrored texture can't be converted to its mirror's format: the mirror is stale");
      return;
   }
   context->UpdateSubresource(state->mirror.Get(), subresource, box, converted->bytes.data(), converted->row_pitch, converted->depth_pitch);
}
} // namespace D3D11
} // namespace Mirrors
} // namespace Resources
} // namespace Luma
