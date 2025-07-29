#pragma once

#if DEVELOPMENT
std::optional<std::string> GetD3DName(ID3D11DeviceChild* obj)
{
   if (obj == nullptr) return std::nullopt;

   byte data[128] = {};
   UINT size = sizeof(data);
   if (obj->GetPrivateData(WKPDID_D3DDebugObjectName, &size, data) == S_OK)
   {
      if (size > 0) return std::string{ data, data + size };
   }
   return std::nullopt;
}

// Wide string with normal string fallback
std::optional<std::string> GetD3DNameW(ID3D11DeviceChild* obj)
{
   if (obj == nullptr) return std::nullopt;

   byte data[128] = {};
   UINT size = sizeof(data);
   if (obj->GetPrivateData(WKPDID_D3DDebugObjectNameW, &size, data) == S_OK)
   {
      if (size > 0)
      {
         char c_name[128] = {};
         size_t out_size;
         // wide-character-string-to-multibyte-string_safe
         auto ret = wcstombs_s(&out_size, c_name, sizeof(c_name), reinterpret_cast<wchar_t*>(data), size);
         if (ret == 0 && out_size > 0)
         {
            return std::string(c_name, c_name + out_size);
         }
      }
   }
   return GetD3DName(obj);
}
#endif

void GetResourceInfo(ID3D11Resource* resource, uint3& size, DXGI_FORMAT& format, std::string* hash = nullptr)
{
   size = { };
   format = DXGI_FORMAT_UNKNOWN;
   if (hash)
   {
      *hash = "";
   }
   if (!resource) return;
   // Go in order of popularity
   com_ptr<ID3D11Texture2D> texture_2d;
   HRESULT hr = resource->QueryInterface(&texture_2d);
   if (SUCCEEDED(hr) && texture_2d)
   {
      D3D11_TEXTURE2D_DESC texture_2d_desc;
      texture_2d->GetDesc(&texture_2d_desc);
      size = uint3{ texture_2d_desc.Width, texture_2d_desc.Height, 0 };
      format = texture_2d_desc.Format;
      if (hash)
      {
         *hash = std::to_string(std::hash<void*>{}(resource));
      }
      return;
   }
   com_ptr<ID3D11Texture3D> texture_3d;
   hr = resource->QueryInterface(&texture_3d);
   if (SUCCEEDED(hr) && texture_3d)
   {
      D3D11_TEXTURE3D_DESC texture_3d_desc;
      texture_3d->GetDesc(&texture_3d_desc);
      size = uint3{ texture_3d_desc.Width, texture_3d_desc.Height, texture_3d_desc.Depth };
      format = texture_3d_desc.Format;
      if (hash)
      {
         *hash = std::to_string(std::hash<void*>{}(resource));
      }
      return;
   }
   com_ptr<ID3D11Texture1D> texture_1d;
   hr = resource->QueryInterface(&texture_1d);
   if (SUCCEEDED(hr) && texture_1d)
   {
      D3D11_TEXTURE1D_DESC texture_1d_desc;
      texture_1d->GetDesc(&texture_1d_desc);
      size = uint3{ texture_1d_desc.Width, 0, 0 };
      format = texture_1d_desc.Format;
      if (hash)
      {
         *hash = std::to_string(std::hash<void*>{}(resource));
      }
      return;
   }
}
void GetResourceInfo(ID3D11View* view, uint3& size, DXGI_FORMAT& format, std::string* hash = nullptr)
{
   if (!view)
   {
      GetResourceInfo((ID3D11Resource*)nullptr, size, format, hash);
      return;
   }
   com_ptr<ID3D11Resource> srv_resource;
   view->GetResource(&srv_resource);
   return GetResourceInfo(srv_resource.get(), size, format, hash);
}

bool AreResourcesEqual(ID3D11Resource* resource1, ID3D11Resource* resource2, bool check_format = true)
{
	uint3 size1, size2;
	DXGI_FORMAT format1, format2;
	GetResourceInfo(resource1, size1, format1);
	GetResourceInfo(resource2, size2, format2);
	return size1 == size2 && (!check_format || format1 == format2);
}

com_ptr<ID3D11Texture2D> CloneTexture2D(ID3D11Device* native_device, ID3D11Resource* texture_2d_resource, DXGI_FORMAT overridden_format = DXGI_FORMAT_UNKNOWN, bool black_initial_data = false, bool copy_data = true, ID3D11DeviceContext* native_device_context = nullptr)
{
   com_ptr<ID3D11Texture2D> cloned_resource;
   ASSERT_ONCE(texture_2d_resource);
   if (texture_2d_resource)
   {
      com_ptr<ID3D11Texture2D> texture_2d;
      HRESULT hr = texture_2d_resource->QueryInterface(&texture_2d);
      if (SUCCEEDED(hr) && texture_2d)
      {
         D3D11_TEXTURE2D_DESC texture_2d_desc;
         texture_2d->GetDesc(&texture_2d_desc);
         if (overridden_format != DXGI_FORMAT_UNKNOWN)
         {
            texture_2d_desc.Format = overridden_format;
         }
         D3D11_SUBRESOURCE_DATA initial_data = {};
         uint8_t* data = nullptr;
         if (black_initial_data)
         {
            uint8_t channels = 0;
            uint8_t bits_per_channel = 0;
            switch (texture_2d_desc.Format)
            {
            case DXGI_FORMAT_R16G16B16A16_TYPELESS:
            case DXGI_FORMAT_R16G16B16A16_FLOAT:
            case DXGI_FORMAT_R16G16B16A16_UNORM:
            {
               channels = 4;
               bits_per_channel = 16;
               break;
            }
            case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            case DXGI_FORMAT_R8G8B8A8_UNORM:
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            {
               channels = 4;
               bits_per_channel = 8;
               break;
            }
            }

            if (bits_per_channel == 8)
            {
               data = (uint8_t*)malloc(texture_2d_desc.Width * texture_2d_desc.Height * channels * sizeof(uint8_t));
               for (int i = 0; i < texture_2d_desc.Width * texture_2d_desc.Height * channels; i++)
               {
                  data[i] = (uint8_t)0;
               }
            }
            else if (bits_per_channel == 16)
            {
               uint16_t* data_16 = nullptr;
               data_16 = (uint16_t*)malloc(texture_2d_desc.Width * texture_2d_desc.Height * channels * sizeof(uint16_t));
               for (int i = 0; i < texture_2d_desc.Width * texture_2d_desc.Height * channels; i++)
               {
                  data_16[i] = (uint16_t)0;
               }
               data = (uint8_t*)data_16;
            }
            else
            {
               ASSERT_ONCE(false); // Unsupported format
               black_initial_data = false;
            }

            if (black_initial_data)
            {
               initial_data.pSysMem = data;
               initial_data.SysMemPitch = texture_2d_desc.Width * channels; // Width * bytes per pixel
               initial_data.SysMemSlicePitch = 0; // Only for 3D textures
            }
         }
         hr = native_device->CreateTexture2D(&texture_2d_desc, black_initial_data ? &initial_data : nullptr, &cloned_resource);
         ASSERT_ONCE(SUCCEEDED(hr));
         if (black_initial_data)
         {
            delete data; data = nullptr;
         }
         if (copy_data && SUCCEEDED(hr) && cloned_resource.get())
         {
            native_device_context->CopyResource(texture_2d_resource, cloned_resource.get());
         }
      }
   }
   return cloned_resource;
}

#if DEVELOPMENT
enum class DebugDrawTextureOptionsMask : uint32_t
{
   None = 0,
   Fullscreen = 1 << 0,
   RenderResolutionScale = 1 << 1,
   ShowAlpha = 1 << 2,
   PreMultiplyAlpha = 1 << 3,
   InvertColors = 1 << 4,
   LinearToGamma = 1 << 5,
   GammaToLinear = 1 << 6,
   FlipY = 1 << 7,
   Saturate = 1 << 8,
};
// If true we are drawing the render target texture, otherwise the shader resource texture
enum class DebugDrawMode : uint32_t
{
   RenderTarget,
   UnorderedAccessView,
   ShaderResource,
};

void CopyDebugDrawTexture(DebugDrawMode debug_draw_mode, int32_t debug_draw_view_index, reshade::api::command_list* cmd_list, bool is_dispatch /*= false*/)
{
   ID3D11Device* native_device = (ID3D11Device*)(cmd_list->get_device()->get_native());
   ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());
   DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();

   com_ptr<ID3D11Resource> texture_resource;
   if (debug_draw_mode == DebugDrawMode::RenderTarget)
   {
      com_ptr<ID3D11RenderTargetView> render_target_view;

      ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
      native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], nullptr);
      render_target_view = rtvs[debug_draw_view_index];
      for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
      {
         if (rtvs[i] != nullptr)
         {
            rtvs[i]->Release();
            rtvs[i] = nullptr;
         }
      }

      if (render_target_view)
      {
         render_target_view->GetResource(&texture_resource);
         GetResourceInfo(texture_resource.get(), device_data.debug_draw_texture_size, device_data.debug_draw_texture_format); // Note: this isn't synchronized with the conditions that update "debug_draw_texture" below but it should work anyway
         D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
         render_target_view->GetDesc(&rtv_desc);
         device_data.debug_draw_texture_format = rtv_desc.Format;
      }
   }
   else if (debug_draw_mode == DebugDrawMode::UnorderedAccessView)
   {
      com_ptr<ID3D11UnorderedAccessView> unordered_access_view;

      ID3D11UnorderedAccessView* uavs[D3D11_1_UAV_SLOT_COUNT] = {};
      // Not sure there's a difference between these two but probably the second one is just meant for pixel shader draw calls
      if (is_dispatch)
      {
         native_device_context->CSGetUnorderedAccessViews(0, D3D11_1_UAV_SLOT_COUNT, &uavs[0]);
      }
      else
      {
         native_device_context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, D3D11_PS_CS_UAV_REGISTER_COUNT, &uavs[0]);
      }

      unordered_access_view = uavs[debug_draw_view_index];
      for (UINT i = 0; i < D3D11_1_UAV_SLOT_COUNT; i++)
      {
         if (uavs[i] != nullptr)
         {
            uavs[i]->Release();
            uavs[i] = nullptr;
         }
      }

      if (unordered_access_view)
      {
         unordered_access_view->GetResource(&texture_resource);
         GetResourceInfo(texture_resource.get(), device_data.debug_draw_texture_size, device_data.debug_draw_texture_format);
         D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc;
         unordered_access_view->GetDesc(&uav_desc);
         device_data.debug_draw_texture_format = uav_desc.Format; // Note: this isn't synchronized with the conditions that update "debug_draw_texture" below but it should work anyway
      }
   }
   else /*if (debug_draw_mode == DebugDrawMode::ShaderResource)*/
   {
      com_ptr<ID3D11ShaderResourceView> shader_resource_view;
      // Note: these might assert if you query an invalid index (there's no way of knowing it without tracking the previous sets)
      if (is_dispatch)
      {
         native_device_context->CSGetShaderResources(debug_draw_view_index, 1, &shader_resource_view);
      }
      else
      {
         native_device_context->PSGetShaderResources(debug_draw_view_index, 1, &shader_resource_view);
      }
      if (shader_resource_view)
      {
         shader_resource_view->GetResource(&texture_resource);
         GetResourceInfo(texture_resource.get(), device_data.debug_draw_texture_size, device_data.debug_draw_texture_format);
         D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
         shader_resource_view->GetDesc(&srv_desc);
         device_data.debug_draw_texture_format = srv_desc.Format;
      }
   }

   if (texture_resource)
   {
      com_ptr<ID3D11Texture2D> texture;
      texture_resource->QueryInterface(&texture);
      // For now we re-create it every frame as we don't care for performance
      if (texture)
      {
         D3D11_TEXTURE2D_DESC texture_desc;
         texture->GetDesc(&texture_desc);
         texture_desc.Usage = D3D11_USAGE_DEFAULT;
         texture_desc.CPUAccessFlags = 0;
         texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS; // Just do all of them
         device_data.debug_draw_texture = nullptr;
         HRESULT hr = native_device->CreateTexture2D(&texture_desc, nullptr, &device_data.debug_draw_texture); //TODOFT: figure out error, happens sometimes. And make thread safe!
         ASSERT_ONCE(SUCCEEDED(hr));

         // Back it up as it gets immediately overwritten or re-used later
         if (device_data.debug_draw_texture)
         {
            native_device_context->CopyResource(device_data.debug_draw_texture.get(), texture.get());
         }
      }
      else
      {
         ASSERT_ONCE("Draw Debug: Target Texture is not 2D");
      }
   }
}
#endif // DEVELOPMENT