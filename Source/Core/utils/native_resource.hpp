#pragma once

#include <cstdint>
#include <cstring>

#include <d3d11_4.h>
#include <wrl/client.h>

#include "../hooks/scope.hpp"

// Any D3D11 resource's desc, in one shape for the rules that don't depend on the dimension
struct ResourceDesc
{
   D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
   UINT width = 0; // Bytes for buffers
   UINT height = 1;
   UINT depth_or_layers = 1;
   UINT levels = 1;
   DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
   UINT samples = 1;
   UINT quality = 0;
   D3D11_USAGE usage = D3D11_USAGE_DEFAULT;
   UINT bind_flags = 0;
   UINT cpu_access_flags = 0;
   UINT misc_flags = 0;
   UINT structure_byte_stride = 0;
   D3D11_TEXTURE_LAYOUT layout = D3D11_TEXTURE_LAYOUT_UNDEFINED;

   bool IsTexture() const { return dimension == D3D11_RESOURCE_DIMENSION_TEXTURE1D || dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D || dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D; }
   bool IsCube() const { return (misc_flags & D3D11_RESOURCE_MISC_TEXTURECUBE) != 0 && depth_or_layers != 0 && (depth_or_layers % 6) == 0; }

   static ResourceDesc From(const D3D11_BUFFER_DESC& desc)
   {
      ResourceDesc result;
      result.dimension = D3D11_RESOURCE_DIMENSION_BUFFER;
      result.width = desc.ByteWidth;
      result.usage = desc.Usage;
      result.bind_flags = desc.BindFlags;
      result.cpu_access_flags = desc.CPUAccessFlags;
      result.misc_flags = desc.MiscFlags;
      result.structure_byte_stride = desc.StructureByteStride;
      return result;
   }
   static ResourceDesc From(const D3D11_TEXTURE1D_DESC& desc)
   {
      ResourceDesc result;
      result.dimension = D3D11_RESOURCE_DIMENSION_TEXTURE1D;
      result.width = desc.Width;
      result.depth_or_layers = desc.ArraySize;
      result.levels = desc.MipLevels;
      result.format = desc.Format;
      result.usage = desc.Usage;
      result.bind_flags = desc.BindFlags;
      result.cpu_access_flags = desc.CPUAccessFlags;
      result.misc_flags = desc.MiscFlags;
      return result;
   }
   static ResourceDesc From(const D3D11_TEXTURE2D_DESC1& desc)
   {
      ResourceDesc result;
      result.dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
      result.width = desc.Width;
      result.height = desc.Height;
      result.depth_or_layers = desc.ArraySize;
      result.levels = desc.MipLevels;
      result.format = desc.Format;
      result.samples = desc.SampleDesc.Count;
      result.quality = desc.SampleDesc.Quality;
      result.usage = desc.Usage;
      result.bind_flags = desc.BindFlags;
      result.cpu_access_flags = desc.CPUAccessFlags;
      result.misc_flags = desc.MiscFlags;
      result.layout = desc.TextureLayout;
      return result;
   }
   static ResourceDesc From(const D3D11_TEXTURE2D_DESC& desc)
   {
      D3D11_TEXTURE2D_DESC1 desc1 = {};
      std::memcpy(&desc1, &desc, sizeof(desc));
      return From(desc1);
   }
   static ResourceDesc From(const D3D11_TEXTURE3D_DESC1& desc)
   {
      ResourceDesc result;
      result.dimension = D3D11_RESOURCE_DIMENSION_TEXTURE3D;
      result.width = desc.Width;
      result.height = desc.Height;
      result.depth_or_layers = desc.Depth;
      result.levels = desc.MipLevels;
      result.format = desc.Format;
      result.usage = desc.Usage;
      result.bind_flags = desc.BindFlags;
      result.cpu_access_flags = desc.CPUAccessFlags;
      result.misc_flags = desc.MiscFlags;
      result.layout = desc.TextureLayout;
      return result;
   }
   static ResourceDesc From(const D3D11_TEXTURE3D_DESC& desc)
   {
      D3D11_TEXTURE3D_DESC1 desc1 = {};
      std::memcpy(&desc1, &desc, sizeof(desc));
      return From(desc1);
   }

   D3D11_BUFFER_DESC ToBuffer() const
   {
      return {width, usage, bind_flags, cpu_access_flags, misc_flags, structure_byte_stride};
   }
   D3D11_TEXTURE1D_DESC ToTexture1D() const
   {
      return {width, levels, depth_or_layers, format, usage, bind_flags, cpu_access_flags, misc_flags};
   }
   D3D11_TEXTURE2D_DESC1 ToTexture2D() const
   {
      return {width, height, levels, depth_or_layers, format, {samples, quality}, usage, bind_flags, cpu_access_flags, misc_flags, layout};
   }
   D3D11_TEXTURE3D_DESC1 ToTexture3D() const
   {
      return {width, height, depth_or_layers, levels, format, usage, bind_flags, cpu_access_flags, misc_flags, layout};
   }
};

// D3D11 getters aren't const qualified, but they don't modify the object.
inline ResourceDesc GetResourceDesc(const ID3D11Resource* const_resource)
{
   if (!const_resource)
      return {};
   ID3D11Resource* resource = const_cast<ID3D11Resource*>(const_resource);
   D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
   resource->GetType(&dimension);
   switch (dimension)
   {
   case D3D11_RESOURCE_DIMENSION_BUFFER:
   {
      D3D11_BUFFER_DESC desc;
      static_cast<ID3D11Buffer*>(resource)->GetDesc(&desc);
      return ResourceDesc::From(desc);
   }
   case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
   {
      D3D11_TEXTURE1D_DESC desc;
      static_cast<ID3D11Texture1D*>(resource)->GetDesc(&desc);
      return ResourceDesc::From(desc);
   }
   case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
   {
      D3D11_TEXTURE2D_DESC desc;
      static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
      return ResourceDesc::From(desc);
   }
   case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
   {
      D3D11_TEXTURE3D_DESC desc;
      static_cast<ID3D11Texture3D*>(resource)->GetDesc(&desc);
      return ResourceDesc::From(desc);
   }
   default: return {};
   }
}

// Never seen by Luma's own hooks. The layout isn't kept, it needs ID3D11Device3.
inline HRESULT CreateResource(ID3D11Device* device, const ResourceDesc& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Resource** resource)
{
   Luma::Hooks::Scope::Guard scope;
   switch (desc.dimension)
   {
   case D3D11_RESOURCE_DIMENSION_BUFFER:
   {
      const D3D11_BUFFER_DESC buffer_desc = desc.ToBuffer();
      return device->CreateBuffer(&buffer_desc, initial_data, reinterpret_cast<ID3D11Buffer**>(resource));
   }
   case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
   {
      const D3D11_TEXTURE1D_DESC texture_desc = desc.ToTexture1D();
      return device->CreateTexture1D(&texture_desc, initial_data, reinterpret_cast<ID3D11Texture1D**>(resource));
   }
   case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
   {
      D3D11_TEXTURE2D_DESC texture_desc;
      const D3D11_TEXTURE2D_DESC1 texture_desc1 = desc.ToTexture2D();
      std::memcpy(&texture_desc, &texture_desc1, sizeof(texture_desc));
      return device->CreateTexture2D(&texture_desc, initial_data, reinterpret_cast<ID3D11Texture2D**>(resource));
   }
   case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
   {
      D3D11_TEXTURE3D_DESC texture_desc;
      const D3D11_TEXTURE3D_DESC1 texture_desc1 = desc.ToTexture3D();
      std::memcpy(&texture_desc, &texture_desc1, sizeof(texture_desc));
      return device->CreateTexture3D(&texture_desc, initial_data, reinterpret_cast<ID3D11Texture3D**>(resource));
   }
   default: return E_INVALIDARG;
   }
}

enum class ViewKind : uint8_t
{
   ShaderResource,
   RenderTarget,
   UnorderedAccess,
   DepthStencil,
};

// Any D3D11 view's desc. The versioned descs are a superset of the base ones.
struct ResourceViewDesc
{
   ViewKind kind = ViewKind::ShaderResource;
   union
   {
      D3D11_SHADER_RESOURCE_VIEW_DESC1 srv;
      D3D11_RENDER_TARGET_VIEW_DESC1 rtv;
      D3D11_UNORDERED_ACCESS_VIEW_DESC1 uav;
      D3D11_DEPTH_STENCIL_VIEW_DESC dsv;
   };

   ResourceViewDesc() : srv() {}

   // Every desc starts with its format
   DXGI_FORMAT& Format() { return srv.Format; }
   DXGI_FORMAT Format() const { return srv.Format; }
};
static_assert(offsetof(D3D11_RENDER_TARGET_VIEW_DESC1, Format) == 0 && offsetof(D3D11_UNORDERED_ACCESS_VIEW_DESC1, Format) == 0 && offsetof(D3D11_DEPTH_STENCIL_VIEW_DESC, Format) == 0);

template <typename Desc1, typename Desc>
inline Desc1 WidenViewDesc(const Desc& desc)
{
   static_assert(sizeof(Desc) <= sizeof(Desc1));
   Desc1 desc1 = {};
   std::memcpy(&desc1, &desc, sizeof(Desc));
   return desc1;
}

inline ResourceViewDesc GetResourceViewDesc(const ID3D11View* const_view)
{
   ResourceViewDesc result;
   if (!const_view)
      return result;
   ID3D11View* view = const_cast<ID3D11View*>(const_view);
   Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
   Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
   Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
   Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
   if (SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&srv))))
   {
      D3D11_SHADER_RESOURCE_VIEW_DESC desc;
      srv->GetDesc(&desc);
      result.kind = ViewKind::ShaderResource;
      result.srv = WidenViewDesc<D3D11_SHADER_RESOURCE_VIEW_DESC1>(desc);
   }
   else if (SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&rtv))))
   {
      D3D11_RENDER_TARGET_VIEW_DESC desc;
      rtv->GetDesc(&desc);
      result.kind = ViewKind::RenderTarget;
      result.rtv = WidenViewDesc<D3D11_RENDER_TARGET_VIEW_DESC1>(desc);
   }
   else if (SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&uav))))
   {
      D3D11_UNORDERED_ACCESS_VIEW_DESC desc;
      uav->GetDesc(&desc);
      result.kind = ViewKind::UnorderedAccess;
      result.uav = WidenViewDesc<D3D11_UNORDERED_ACCESS_VIEW_DESC1>(desc);
   }
   else if (SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&dsv))))
   {
      result.kind = ViewKind::DepthStencil;
      dsv->GetDesc(&result.dsv);
   }
   return result;
}

inline ID3D11Resource* GetResourceFromView(const ID3D11View* view) // Not AddRef'd
{
   if (!view)
      return nullptr;
   ID3D11Resource* resource = nullptr;
   const_cast<ID3D11View*>(view)->GetResource(&resource);
   if (resource)
      resource->Release();
   return resource;
}

// Never seen by Luma's own hooks. The plane slices aren't kept, they need ID3D11Device3.
inline HRESULT CreateResourceView(ID3D11Device* device, ID3D11Resource* resource, const ResourceViewDesc& desc, ID3D11View** view)
{
   Luma::Hooks::Scope::Guard scope;
   switch (desc.kind)
   {
   case ViewKind::ShaderResource:
   {
      D3D11_SHADER_RESOURCE_VIEW_DESC base_desc;
      std::memcpy(&base_desc, &desc.srv, sizeof(base_desc));
      return device->CreateShaderResourceView(resource, &base_desc, reinterpret_cast<ID3D11ShaderResourceView**>(view));
   }
   case ViewKind::RenderTarget:
   {
      D3D11_RENDER_TARGET_VIEW_DESC base_desc;
      std::memcpy(&base_desc, &desc.rtv, sizeof(base_desc));
      return device->CreateRenderTargetView(resource, &base_desc, reinterpret_cast<ID3D11RenderTargetView**>(view));
   }
   case ViewKind::UnorderedAccess:
   {
      D3D11_UNORDERED_ACCESS_VIEW_DESC base_desc;
      std::memcpy(&base_desc, &desc.uav, sizeof(base_desc));
      return device->CreateUnorderedAccessView(resource, &base_desc, reinterpret_cast<ID3D11UnorderedAccessView**>(view));
   }
   case ViewKind::DepthStencil:
      return device->CreateDepthStencilView(resource, &desc.dsv, reinterpret_cast<ID3D11DepthStencilView**>(view));
   }
   return E_INVALIDARG;
}

enum class ViewType : uint8_t
{
   Unknown, // No desc (the whole resource)
   Buffer,
   Texture1D,
   Texture1DArray,
   Texture2D,
   Texture2DArray,
   Texture2DMS,
   Texture2DMSArray,
   Texture3D,
   TextureCube,
   TextureCubeArray,
};

inline ViewType GetViewType(const ResourceViewDesc& desc)
{
   switch (desc.kind)
   {
   case ViewKind::ShaderResource:
      switch (desc.srv.ViewDimension)
      {
      case D3D11_SRV_DIMENSION_BUFFER:
      case D3D11_SRV_DIMENSION_BUFFEREX: return ViewType::Buffer;
      case D3D11_SRV_DIMENSION_TEXTURE1D: return ViewType::Texture1D;
      case D3D11_SRV_DIMENSION_TEXTURE1DARRAY: return ViewType::Texture1DArray;
      case D3D11_SRV_DIMENSION_TEXTURE2D: return ViewType::Texture2D;
      case D3D11_SRV_DIMENSION_TEXTURE2DARRAY: return ViewType::Texture2DArray;
      case D3D11_SRV_DIMENSION_TEXTURE2DMS: return ViewType::Texture2DMS;
      case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY: return ViewType::Texture2DMSArray;
      case D3D11_SRV_DIMENSION_TEXTURE3D: return ViewType::Texture3D;
      case D3D11_SRV_DIMENSION_TEXTURECUBE: return ViewType::TextureCube;
      case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY: return ViewType::TextureCubeArray;
      default: return ViewType::Unknown;
      }
   case ViewKind::RenderTarget:
      switch (desc.rtv.ViewDimension)
      {
      case D3D11_RTV_DIMENSION_BUFFER: return ViewType::Buffer;
      case D3D11_RTV_DIMENSION_TEXTURE1D: return ViewType::Texture1D;
      case D3D11_RTV_DIMENSION_TEXTURE1DARRAY: return ViewType::Texture1DArray;
      case D3D11_RTV_DIMENSION_TEXTURE2D: return ViewType::Texture2D;
      case D3D11_RTV_DIMENSION_TEXTURE2DARRAY: return ViewType::Texture2DArray;
      case D3D11_RTV_DIMENSION_TEXTURE2DMS: return ViewType::Texture2DMS;
      case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY: return ViewType::Texture2DMSArray;
      case D3D11_RTV_DIMENSION_TEXTURE3D: return ViewType::Texture3D;
      default: return ViewType::Unknown;
      }
   case ViewKind::UnorderedAccess:
      switch (desc.uav.ViewDimension)
      {
      case D3D11_UAV_DIMENSION_BUFFER: return ViewType::Buffer;
      case D3D11_UAV_DIMENSION_TEXTURE1D: return ViewType::Texture1D;
      case D3D11_UAV_DIMENSION_TEXTURE1DARRAY: return ViewType::Texture1DArray;
      case D3D11_UAV_DIMENSION_TEXTURE2D: return ViewType::Texture2D;
      case D3D11_UAV_DIMENSION_TEXTURE2DARRAY: return ViewType::Texture2DArray;
      case D3D11_UAV_DIMENSION_TEXTURE3D: return ViewType::Texture3D;
      default: return ViewType::Unknown;
      }
   case ViewKind::DepthStencil:
      switch (desc.dsv.ViewDimension)
      {
      case D3D11_DSV_DIMENSION_TEXTURE1D: return ViewType::Texture1D;
      case D3D11_DSV_DIMENSION_TEXTURE1DARRAY: return ViewType::Texture1DArray;
      case D3D11_DSV_DIMENSION_TEXTURE2D: return ViewType::Texture2D;
      case D3D11_DSV_DIMENSION_TEXTURE2DARRAY: return ViewType::Texture2DArray;
      case D3D11_DSV_DIMENSION_TEXTURE2DMS: return ViewType::Texture2DMS;
      case D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY: return ViewType::Texture2DMSArray;
      default: return ViewType::Unknown;
      }
   }
   return ViewType::Unknown;
}

// The desc D3D11 uses for a null one: the whole texture ("mip_levels" only applies to shader resource views)
inline ResourceViewDesc MakeFullViewDesc(ViewKind kind, const ResourceDesc& resource, DXGI_FORMAT format, UINT mip_levels = UINT(-1))
{
   ResourceViewDesc desc;
   desc.kind = kind;
   desc.Format() = format;
   const bool is_array = resource.depth_or_layers > 1;
   const bool is_ms = resource.samples > 1;
   const bool is_cube = kind == ViewKind::ShaderResource && resource.IsCube();
   switch (kind)
   {
   case ViewKind::ShaderResource:
      if (resource.dimension == D3D11_RESOURCE_DIMENSION_TEXTURE1D)
      {
         desc.srv.ViewDimension = is_array ? D3D11_SRV_DIMENSION_TEXTURE1DARRAY : D3D11_SRV_DIMENSION_TEXTURE1D;
         desc.srv.Texture1DArray = {0, mip_levels, 0, resource.depth_or_layers};
      }
      else if (resource.dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D)
      {
         desc.srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
         desc.srv.Texture3D = {0, mip_levels};
      }
      else if (is_cube)
      {
         desc.srv.ViewDimension = resource.depth_or_layers == 6 ? D3D11_SRV_DIMENSION_TEXTURECUBE : D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
         desc.srv.TextureCubeArray = {0, mip_levels, 0, resource.depth_or_layers / 6};
      }
      else if (is_ms)
      {
         desc.srv.ViewDimension = is_array ? D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY : D3D11_SRV_DIMENSION_TEXTURE2DMS;
         desc.srv.Texture2DMSArray = {0, resource.depth_or_layers};
      }
      else
      {
         desc.srv.ViewDimension = is_array ? D3D11_SRV_DIMENSION_TEXTURE2DARRAY : D3D11_SRV_DIMENSION_TEXTURE2D;
         desc.srv.Texture2DArray = {0, mip_levels, 0, resource.depth_or_layers, 0};
      }
      break;
   case ViewKind::RenderTarget:
      if (resource.dimension == D3D11_RESOURCE_DIMENSION_TEXTURE1D)
      {
         desc.rtv.ViewDimension = is_array ? D3D11_RTV_DIMENSION_TEXTURE1DARRAY : D3D11_RTV_DIMENSION_TEXTURE1D;
         desc.rtv.Texture1DArray = {0, 0, resource.depth_or_layers};
      }
      else if (resource.dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D)
      {
         desc.rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
         desc.rtv.Texture3D = {0, 0, UINT(-1)};
      }
      else if (is_ms)
      {
         desc.rtv.ViewDimension = is_array ? D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY : D3D11_RTV_DIMENSION_TEXTURE2DMS;
         desc.rtv.Texture2DMSArray = {0, resource.depth_or_layers};
      }
      else
      {
         desc.rtv.ViewDimension = is_array ? D3D11_RTV_DIMENSION_TEXTURE2DARRAY : D3D11_RTV_DIMENSION_TEXTURE2D;
         desc.rtv.Texture2DArray = {0, 0, resource.depth_or_layers, 0};
      }
      break;
   case ViewKind::UnorderedAccess:
      if (resource.dimension == D3D11_RESOURCE_DIMENSION_TEXTURE1D)
      {
         desc.uav.ViewDimension = is_array ? D3D11_UAV_DIMENSION_TEXTURE1DARRAY : D3D11_UAV_DIMENSION_TEXTURE1D;
         desc.uav.Texture1DArray = {0, 0, resource.depth_or_layers};
      }
      else if (resource.dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D)
      {
         desc.uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
         desc.uav.Texture3D = {0, 0, UINT(-1)};
      }
      else
      {
         desc.uav.ViewDimension = is_array ? D3D11_UAV_DIMENSION_TEXTURE2DARRAY : D3D11_UAV_DIMENSION_TEXTURE2D;
         desc.uav.Texture2DArray = {0, 0, resource.depth_or_layers, 0};
      }
      break;
   case ViewKind::DepthStencil:
      if (resource.dimension == D3D11_RESOURCE_DIMENSION_TEXTURE1D)
      {
         desc.dsv.ViewDimension = is_array ? D3D11_DSV_DIMENSION_TEXTURE1DARRAY : D3D11_DSV_DIMENSION_TEXTURE1D;
         desc.dsv.Texture1DArray = {0, 0, resource.depth_or_layers};
      }
      else if (is_ms)
      {
         desc.dsv.ViewDimension = is_array ? D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY : D3D11_DSV_DIMENSION_TEXTURE2DMS;
         desc.dsv.Texture2DMSArray = {0, resource.depth_or_layers};
      }
      else
      {
         desc.dsv.ViewDimension = is_array ? D3D11_DSV_DIMENSION_TEXTURE2DARRAY : D3D11_DSV_DIMENSION_TEXTURE2D;
         desc.dsv.Texture2DArray = {0, 0, resource.depth_or_layers};
      }
      break;
   }
   return desc;
}
