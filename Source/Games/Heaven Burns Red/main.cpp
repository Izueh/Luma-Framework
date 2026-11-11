#define GAME_HEAVEN_BURNS_RED 1

#define DISABLE_AUTO_DEBUGGER 1
#define DISABLE_FOCUS_LOSS_SUPPRESSION 1
#define AVOID_INPUT_LOSS 1
#define DISABLE_SWAPCHAIN_FLIP_MODEL 1
#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1
#define ALLOW_SHADERS_DUMPING_WITH_NAME 1
#define CUSTOM_MSAA_RESOLVE 0

#include "..\..\Core\core.hpp"
#include "..\..\Core\includes\shader_patching.h"

struct SHEXHeader
{
   char chunk_name[4]; // 'SHEX'
   uint32_t chunk_size;
   uint8_t version;
   uint16_t type;
   uint32_t dword_count;
};


bool PatchPixelShader(std::vector<std::byte>& shader_code)
{
   DXBCHeader* dxbc_header = (DXBCHeader*)&shader_code[0];
   
   bool is_alpha_tested = false;

   for (uint32_t i = 0; i < dxbc_header->chunk_count; ++i)
   {
      if (strncmp((const char*)&shader_code[dxbc_header->chunk_offsets[i]], "SHEX", 4) == 0)
      {
         std::byte* shex = &shader_code[dxbc_header->chunk_offsets[i]];
         SHEXHeader* shex_header = (SHEXHeader*)shex;
         
         uint32_t pos = 16;
         
         D3D10_SB_OPCODE_TYPE prev_opcode_type = D3D10_SB_NUM_OPCODES;
         uint32_t prev_opcode_len = 0;
         
         const uint32_t nop_opcode = ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1) | D3D10_SB_OPCODE_NOP;
         
         uint32_t temp_register[3];
         uint32_t insert_pos = 0;
         
         for (;;)
         {
            D3D10_SB_OPCODE_TYPE opcode_type = DECODE_D3D10_SB_OPCODE_TYPE(*(uint32_t*)(shex + pos));
            uint32_t len;
            if (opcode_type != D3D10_SB_OPCODE_CUSTOMDATA)
            {
               len = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(*(uint32_t*)(shex + pos));
            }
            else
            {
               len = *(uint32_t*)(shex + pos + 4);
            }
            
            // discard_nz XX
            if (opcode_type == D3D10_SB_OPCODE_DISCARD && prev_opcode_type == D3D10_SB_OPCODE_LT)
            {
               D3D10_SB_OPCODE_TYPE next_opcode_type = DECODE_D3D10_SB_OPCODE_TYPE(*(uint32_t*)(shex + pos + len * 4));
               // mov
               if (next_opcode_type == D3D10_SB_OPCODE_MOV)
               {
                  uint32_t opcode_pos = pos + len * 4;
                  D3D10_SB_OPERAND_TYPE operand = DECODE_D3D10_SB_OPERAND_TYPE(*(uint32_t*)(shex + opcode_pos + 4));
                  D3D10_SB_OPERAND_NUM_COMPONENTS num_components = DECODE_D3D10_SB_OPERAND_NUM_COMPONENTS(*(uint32_t*)(shex + opcode_pos + 4));
                  uint32_t slot = *(uint32_t*)(shex + opcode_pos + 8);
                  // o0.xyzw
                  if (operand == D3D10_SB_OPERAND_TYPE_OUTPUT && num_components == D3D10_SB_OPERAND_4_COMPONENT && slot == 0)
                  {
                     D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE selection_mode = DECODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(*(uint32_t*)(shex + opcode_pos + 12));
                     D3D10_SB_OPERAND_TYPE next_operand = DECODE_D3D10_SB_OPERAND_TYPE(*(uint32_t*)(shex + opcode_pos + 12));
                     // rX.xyzw
                     if (next_operand == D3D10_SB_OPERAND_TYPE_TEMP && selection_mode == D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE)
                     {
                        temp_register[0] = *(uint32_t*)(shex + opcode_pos + 16);
                        temp_register[1] = *(uint32_t*)(shex + pos - prev_opcode_len * 4 + 12);
                        temp_register[2] = *(uint32_t*)(shex + pos - prev_opcode_len * 4 + 16);
                        uint32_t next_len = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(*(uint32_t*)(shex + opcode_pos));
                        insert_pos = opcode_pos + next_len * 4;
                        
                        for (uint32_t x = 0; x < len; x++)
                        {
                           *(uint32_t*)(shex + pos + x * 4) = nop_opcode;
                        }
                        
                        for (uint32_t x = 0; x < prev_opcode_len; x++)
                        {
                           *(uint32_t*)(shex + pos - prev_opcode_len * 4 + x * 4) = nop_opcode;
                        }
                        
                        is_alpha_tested = true;
                        break;
                     }
                  }
               }
            }
            else if (opcode_type == D3D10_SB_OPCODE_MOV)
            {
               D3D10_SB_OPERAND_TYPE operand = DECODE_D3D10_SB_OPERAND_TYPE(*(uint32_t*)(shex + pos + 4));
               D3D10_SB_OPERAND_NUM_COMPONENTS num_components = DECODE_D3D10_SB_OPERAND_NUM_COMPONENTS(*(uint32_t*)(shex + pos + 4));
               uint32_t slot = *(uint32_t*)(shex + pos + 8);
               // o0.xyzw
               if (operand == D3D10_SB_OPERAND_TYPE_OUTPUT && num_components == D3D10_SB_OPERAND_4_COMPONENT && slot == 0)
               {
                  // this op + ret
                  if (pos + len * 4 + 4 >= shex_header->chunk_size + 8)
                  {
                     break;
                  }
                  uint32_t next_opcode_pos = pos + len * 4;
                  D3D10_SB_OPCODE_TYPE next_opcode_type = DECODE_D3D10_SB_OPCODE_TYPE(*(uint32_t*)(shex + next_opcode_pos));
                  uint32_t next_len = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(*(uint32_t*)(shex + next_opcode_pos));
                  
                  uint32_t next_next_opcode_pos = next_opcode_pos + next_len * 4;
                  D3D10_SB_OPCODE_TYPE next_next_opcode_type = DECODE_D3D10_SB_OPCODE_TYPE(*(uint32_t*)(shex + next_next_opcode_pos));
                  uint32_t next_next_len = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(*(uint32_t*)(shex + next_next_opcode_pos));
                  
                  if (next_opcode_type == D3D10_SB_OPCODE_LT && next_next_opcode_type == D3D10_SB_OPCODE_DISCARD)
                  {
                     temp_register[0] = *(uint32_t*)(shex + pos + 16);
                     temp_register[1] = *(uint32_t*)(shex + next_opcode_pos + 12);
                     temp_register[2] = *(uint32_t*)(shex + next_opcode_pos + 16);
                     insert_pos = next_next_opcode_pos;
                     
                     for (uint32_t x = 0; x < next_next_len; x++)
                     {
                        *(uint32_t*)(shex + next_next_opcode_pos + x * 4) = nop_opcode;
                     }
                     
                     for (uint32_t x = 0; x < next_len; x++)
                     {
                        *(uint32_t*)(shex + next_opcode_pos + x * 4) = nop_opcode;
                     }
                     
                     is_alpha_tested = true;
                     break;
                  }
               }
            }
            
            if (pos + len * 4 >= shex_header->chunk_size + 8)
            {
               break;
            }
            
            prev_opcode_type = opcode_type;
            prev_opcode_len = len;
            pos += len * 4;
         }
         
         if (is_alpha_tested)
         {
            // From: Anti-aliased Alpha Test: The Esoteric Alpha To Coverage by Ben Golus
            std::vector<uint32_t> shader_patch = {
               0x0500007A, 0x00100012, temp_register[0], 0x0010003A, temp_register[0], // deriv_rtx_coarse r0.x, r0.w
               0x0500007C, 0x00100022, temp_register[0], 0x0010003A, temp_register[0], // deriv_rty_coarse r0.y, r0.w
               0x09000000, 0x00100012, temp_register[0], 0x8010001A, 0x00000081, temp_register[0], 0x8010000A, 0x00000081, temp_register[0], // add r0.x, |r0.y|, |r0.x|
               0x07000034, 0x00100012, temp_register[0], 0x0010000A, temp_register[0], 0x00004001, 0x38D1B717, // max r0.x, r0.x, l(0.000100)
               0x0700000E, 0x00100012, temp_register[0], temp_register[1], temp_register[2], 0x0010000A, temp_register[0], // div r0.x, (alpha - alpha_ref), r0.x
               0x07000000, 0x00102082, 0x00000000, 0x0010000A, temp_register[0], 0x00004001, 0x3F000000, // add o0.w, r0.x, l(0.500000)
            };
               
            shader_code.insert(shader_code.begin() + dxbc_header->chunk_offsets[i] + insert_pos, (std::byte*)&shader_patch[0], (std::byte*)(&shader_patch[0] + shader_patch.size()));
            shex = &shader_code[dxbc_header->chunk_offsets[i]];
            shex_header = (SHEXHeader*)shex;
            shex_header->chunk_size += shader_patch.size() * sizeof(uint32_t);
            shex_header->dword_count += shader_patch.size();
            dxbc_header = (DXBCHeader*)&shader_code[0];
            for (uint32_t j = i + 1; j < dxbc_header->chunk_count; ++j)
            {
               dxbc_header->chunk_offsets[j] += shader_patch.size() * sizeof(uint32_t);
            }
         }
      }
   }
   if (is_alpha_tested)
   {
      dxbc_header->file_size = shader_code.size();
      Hash::MD5::Digest md5_digest = CalcDXBCHash(shader_code.data(), shader_code.size());
      std::memcpy(&dxbc_header->hash, &md5_digest.data, DXBCHeader::hash_size);
   }
   
   return is_alpha_tested;
}

void PatchCharacterPixelShader(std::vector<std::byte>& shader_code)
{
   DXBCHeader* dxbc_header = (DXBCHeader*)&shader_code[0];

   for (uint32_t i = 0; i < dxbc_header->chunk_count; ++i)
   {
      if (strncmp((const char*)&shader_code[dxbc_header->chunk_offsets[i]], "SHEX", 4) == 0)
      {
         std::byte* shex = &shader_code[dxbc_header->chunk_offsets[i]];
         SHEXHeader* shex_header = (SHEXHeader*)shex;
         
         uint32_t pos = 16;
         
         for (;;)
         {
            D3D10_SB_OPCODE_TYPE opcode_type = DECODE_D3D10_SB_OPCODE_TYPE(*(uint32_t*)(shex + pos));
            uint32_t len;
            if (opcode_type != D3D10_SB_OPCODE_CUSTOMDATA)
            {
               len = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(*(uint32_t*)(shex + pos));
            }
            else
            {
               len = *(uint32_t*)(shex + pos + 4);
            }
            
            if (opcode_type == D3D10_SB_OPCODE_DCL_OUTPUT)
            {
               break;
            }
            
            if (opcode_type == D3D10_SB_OPCODE_DCL_INPUT_PS)
            {
               uint32_t opcode = *(uint32_t*)(shex + pos);
               D3D10_SB_INTERPOLATION_MODE interpolation_mode = DECODE_D3D10_SB_INPUT_INTERPOLATION_MODE(opcode);
               switch (interpolation_mode)
               {
                  case D3D10_SB_INTERPOLATION_LINEAR:
                     interpolation_mode = D3D10_SB_INTERPOLATION_LINEAR_SAMPLE;
                     break;
                  case D3D10_SB_INTERPOLATION_LINEAR_NOPERSPECTIVE:
                     interpolation_mode = D3D10_SB_INTERPOLATION_LINEAR_NOPERSPECTIVE_SAMPLE;
                     break;
                  default:
                     break;
               }
               
               opcode &= ~D3D10_SB_INPUT_INTERPOLATION_MODE_MASK;
               opcode |= ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(interpolation_mode);
               
               *(uint32_t*)(shex + pos) = opcode;
            }
            
            if (pos + len * 4 >= shex_header->chunk_size + 8)
            {
               break;
            }

            pos += len * 4;
         }
      }
   }

   dxbc_header->file_size = shader_code.size();
   Hash::MD5::Digest md5_digest = CalcDXBCHash(shader_code.data(), shader_code.size());
   std::memcpy(&dxbc_header->hash, &md5_digest.data, DXBCHeader::hash_size);
}

namespace
{
   static const int msaa_values[] = { 2, 4, 8 };
   int msaa_index = 0;
   int enable_alpha_to_coverage = 0;
   int enable_character_supersampling = 0;
   
   ShaderHashesList shader_hashes_skinning = {};
   ShaderHashesList shader_hashes_non_skinning = {};
}

struct CachedRenderTargetResource
{
   ComPtr<ID3D11Texture2D> texture = nullptr;
   ComPtr<ID3D11RenderTargetView> rtv = nullptr;
   ComPtr<ID3D11ShaderResourceView> srv = nullptr;
   D3D11_TEXTURE2D_DESC desc{};
};

struct GameDeviceHeavenBurnsRed final : public GameDeviceData
{
   std::unordered_map<ID3D11BlendState*, ComPtr<ID3D11BlendState>> alpha_blend_states;
   std::unordered_map<uint32_t, ComPtr<ID3D11PixelShader>> modified_atoc_pixel_shaders;
   std::unordered_map<uint32_t, ComPtr<ID3D11PixelShader>> modified_character_pixel_shaders;
   std::unordered_map<uint32_t, std::vector<std::byte>> pixel_shader_code;
   
   std::unordered_set<ID3D11Buffer*> skinned_vertex_buffers;

   ComPtr<ID3D11Resource> tracked_color_resource;
   ComPtr<ID3D11RenderTargetView> tracked_color_rtv;
   ComPtr<ID3D11Resource> tracked_depth_resource;
   ComPtr<ID3D11DepthStencilView> tracked_depth_dsv;
   
   // Immediate context only
   bool is_current_rtv_ms = false;
   bool has_3d_scene_drawn = false;
   CachedRenderTargetResource cached_source_color;
};

class HeavenBurnsRed final : public Game
{
   static GameDeviceHeavenBurnsRed& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceHeavenBurnsRed*>(device_data.game);
   }

   static const GameDeviceHeavenBurnsRed& GetGameDeviceData(const DeviceData& device_data)
   {
      return *static_cast<const GameDeviceHeavenBurnsRed*>(device_data.game);
   }
   
public:
   void OnInit(bool async) override
   {
#if CUSTOM_MSAA_RESOLVE
      native_shaders_definitions.emplace(CompileTimeStringHash("MSAA Filter VS"), 
         ShaderDefinition{"Luma_MSAAResolveFilter", D3D11_SHVER_VERTEX_SHADER, nullptr, nullptr, 
            {{"VERTEXSHADER", "1"}}});
      
      native_shaders_definitions.emplace(CompileTimeStringHash("MSAA Filter PS"), 
         ShaderDefinition{"Luma_MSAAResolveFilter", D3D11_SHVER_PIXEL_SHADER, nullptr, nullptr, 
            {{"MSAA_SAMPLE", "8"}}});
      #endif
   }
   
   void OnLoad(std::filesystem::path& file_path, bool failed) override
   {
      if (!failed)
      {
         LoadConfigs();
      }
   }
   
   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceHeavenBurnsRed;
   }
   
   // The game's 2x MSAA output-sized targets get the user's MSAA sample count
   void PreCreateTexture2D(ID3D11Device* device, D3D11_TEXTURE2D_DESC1& desc, const D3D11_SUBRESOURCE_DATA*& initial_data) override
   {
      if (desc.SampleDesc.Count != 2)
         return;

      if (desc.Width == desc.Height)
         return;

      DeviceData* device_data = GetDeviceData(device);
      if (!device_data)
         return;

      if (desc.Width != device_data->output_resolution.x || desc.Height != device_data->output_resolution.y)
         return;

      desc.SampleDesc.Count = msaa_values[msaa_index];
   }

   void PostCreateTexture2D(ID3D11Device* device, const D3D11_TEXTURE2D_DESC1& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture2D* texture) override
   {
      if (desc.SampleDesc.Count <= 1)
         return;

      DeviceData* device_data = GetDeviceData(device);
      if (!device_data || !device_data->game)
         return;

      if (desc.Width != device_data->output_resolution.x || desc.Height != device_data->output_resolution.y)
         return;

      auto& game_device_data = GetGameDeviceData(*device_data);

      if ((desc.BindFlags & D3D11_BIND_DEPTH_STENCIL) != 0)
      {
         game_device_data.tracked_depth_resource = texture;
         game_device_data.tracked_depth_dsv = nullptr;
      }
      else if ((desc.BindFlags & D3D11_BIND_RENDER_TARGET) != 0)
      {
         game_device_data.tracked_color_resource = texture;
         game_device_data.tracked_color_rtv = nullptr;
      }
   }

   void OnResourceDestroyed(ID3D11Device* device, const ID3D11Resource* resource) override
   {
      DeviceData* device_data = GetDeviceData(device);
      if (!device_data || !device_data->game)
         return;
      auto& game_device_data = GetGameDeviceData(*device_data);

      if (game_device_data.tracked_color_resource.get() == resource)
      {
         game_device_data.tracked_color_resource = nullptr;
         game_device_data.tracked_color_rtv = nullptr;
      }
      else if (game_device_data.tracked_depth_resource.get() == resource)
      {
         game_device_data.tracked_depth_resource = nullptr;
         game_device_data.tracked_depth_dsv = nullptr;
      }
   }

   void PostCreateRenderTargetView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_RENDER_TARGET_VIEW_DESC1* desc, ID3D11RenderTargetView* view) override
   {
      DeviceData* device_data = GetDeviceData(device);
      if (!device_data || !device_data->game)
         return;
      auto& game_device_data = GetGameDeviceData(*device_data);
      if (game_device_data.tracked_color_resource.get() == resource)
      {
         game_device_data.tracked_color_rtv = view;
      }
   }

   void PostCreateDepthStencilView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc, ID3D11DepthStencilView* view) override
   {
      DeviceData* device_data = GetDeviceData(device);
      if (!device_data || !device_data->game)
         return;
      auto& game_device_data = GetGameDeviceData(*device_data);
      if (game_device_data.tracked_depth_resource.get() == resource)
      {
         game_device_data.tracked_depth_dsv = view;
      }
   }

   void PreCreatePixelShader(ID3D11Device* native_device, const void*& bytecode, SIZE_T& length) override
   {
      DeviceData* device_data = GetDeviceData(native_device);
      if (!device_data || !device_data->game || !bytecode)
         return;
      auto& game_device_data = GetGameDeviceData(*device_data);
      std::vector<std::byte> shader_code((const std::byte*)bytecode, ((const std::byte*)bytecode) + length);

      uint32_t hash = Shader::BinToHash((const uint8_t*)bytecode, length);

      std::vector<std::byte> code;
      code.resize(length);
      memcpy(&code[0], bytecode, length);
      game_device_data.pixel_shader_code[hash] = std::move(code);

      // Better build the shader list here than check per draw
      bool is_alpha_tested_shader = PatchPixelShader(shader_code);

      if (!is_alpha_tested_shader)
         return;

      com_ptr<ID3D11PixelShader> patched_shader;
      native_device->CreatePixelShader(shader_code.data(), shader_code.size(), nullptr, &patched_shader);

      game_device_data.modified_atoc_pixel_shaders[hash] = patched_shader.get();
   }

   void PreCreateSamplerState(ID3D11Device* device, D3D11_SAMPLER_DESC& desc) override
   {
      if (desc.MaxAnisotropy <= 1)
         return;

      // The game already uses aniso samplers for most of the texture samplings, but for lightmap it's 3x and texture 16x/10x/4x
      desc.MaxAnisotropy = D3D11_REQ_MAXANISOTROPY;
   }

   static void OnBindRenderTargetsAndDepthStencil(
      ID3D11DeviceContext* context,
      uint32_t count,
      ID3D11RenderTargetView* const* rtvs)
   {
      CommandListData* cmd_list_data = GetCommandListData(context);
      if (!cmd_list_data)
         return;
      auto& game_device_data = GetGameDeviceData(*cmd_list_data->device_data);

      game_device_data.is_current_rtv_ms =
         count == 1 &&
         rtvs && rtvs[0] != nullptr &&
         game_device_data.tracked_color_rtv != nullptr &&
         rtvs[0] == game_device_data.tracked_color_rtv.get();
   }

   bool PreOMSetRenderTargets(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView** views, ID3D11DepthStencilView*& depth_stencil) override
   {
      OnBindRenderTargetsAndDepthStencil(context, count, views);
      return false;
   }

   bool PreOMSetRenderTargetsAndUnorderedAccessViews(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView** views, ID3D11DepthStencilView*& depth_stencil, UINT uav_start, UINT uav_count, ID3D11UnorderedAccessView** uavs, const UINT* initial_counts) override
   {
      // ReShade only reported the render targets when they changed
      if (count != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL)
         OnBindRenderTargetsAndDepthStencil(context, count, views);
      return false;
   }
   
#if CUSTOM_MSAA_RESOLVE
   bool PreResolveSubresource(ID3D11DeviceContext* native_device_context, ID3D11Resource* dest, UINT dest_subresource, ID3D11Resource* source, UINT source_subresource, DXGI_FORMAT format) override
   {
      if (source == nullptr)
         return false;

      if (dest == nullptr)
         return false;

      CommandListData* cmd_list_data = GetCommandListData(native_device_context);
      if (!cmd_list_data)
         return false;
      auto& device_data = *cmd_list_data->device_data;
      auto& game_device_data = GetGameDeviceData(device_data);

      if (!game_device_data.has_3d_scene_drawn)
      {
#if 0
         Luma::Utils::Log::Write(Luma::Utils::Log::Level::Verbose, "Fail to detect 3d scene.");
#endif
         return false;
      }
      
      ComPtr<ID3D11Resource> src_tex;
      HRESULT hr_src = source->QueryInterface(src_tex.put());
      if (FAILED(hr_src))
      {
#if DEVELOPMENT
         Luma::Utils::Log::Write(Luma::Utils::Log::Level::Verbose, "Fail to resolve incompatible source.");
#endif
         return false;
      }
      
      if (game_device_data.cached_source_color.texture.get() == src_tex.get())
      {
         if (test_index == 14)
         {
            HRESULT hr = S_OK;
            ID3D11Device* native_device = device_data.native_device;

            ComPtr<ID3D11RenderTargetView> render_target_view;
            {
               ComPtr<ID3D11Resource> dest_tex;
               HRESULT hr_dest = dest->QueryInterface(dest_tex.put());
               if (FAILED(hr_dest))
               {
#if DEVELOPMENT
                  Luma::Utils::Log::Write(Luma::Utils::Log::Level::Verbose, "Fail to resolve incompatible dest.");
#endif
                  return false;
               }

               D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
               rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
               rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
               rtv_desc.Texture2D.MipSlice = 0;

               hr = native_device->CreateRenderTargetView(dest_tex.get(), &rtv_desc, render_target_view.put());
               if (FAILED(hr))
               {
#if DEVELOPMENT
                  Luma::Utils::Log::Write(Luma::Utils::Log::Level::Verbose, "Fail to create dest render target.");
#endif
                  return false;
               }
            }

            DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
            draw_state_stack.Cache(native_device_context, device_data.uav_max_count);

   #if 0
            Luma::Utils::Log::Write(Luma::Utils::Log::Level::Verbose, "Drawing custom resolve.");
   #endif

            // Set the new resources/states:
            constexpr FLOAT blend_factor_alpha[4] = { 1.f, 1.f, 1.f, 1.f };
            constexpr FLOAT blend_factor[4] = { 1.f, 1.f, 1.f, 0.f }; // TODO: this makes no sense as the blend state is unlikely to use it, use write mask instead
            native_device_context->OMSetBlendState(device_data.default_blend_state.get(), false ? blend_factor_alpha : blend_factor, 0xFFFFFFFF);
            native_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            native_device_context->RSSetScissorRects(0, nullptr);
            D3D11_VIEWPORT viewport;
            viewport.TopLeftX = 0;
            viewport.TopLeftY = 0;
            viewport.Width = game_device_data.cached_source_color.desc.Width;
            viewport.Height = game_device_data.cached_source_color.desc.Height;
            viewport.MinDepth = 0;
            viewport.MaxDepth = 1;
            native_device_context->RSSetViewports(1, &viewport);
            native_device_context->OMSetRenderTargets(1, render_target_view.get_addressof(), nullptr);
            native_device_context->PSSetShaderResources(0, 1, game_device_data.cached_source_color.srv.get_addressof());
            native_device_context->OMSetDepthStencilState(nullptr, 0);
            ID3D11VertexShader* vs = device_data.native_vertex_shaders[CompileTimeStringHash("MSAA Filter VS")].get();
            ID3D11PixelShader* ps = device_data.native_pixel_shaders[CompileTimeStringHash("MSAA Filter PS")].get();
            native_device_context->VSSetShader(vs, nullptr, 0);
            native_device_context->PSSetShader(ps, nullptr, 0);
            native_device_context->IASetInputLayout(nullptr);
            native_device_context->RSSetState(nullptr);

            // Finally draw:
            native_device_context->Draw(3, 0);

            draw_state_stack.Restore(native_device_context);
            return true;
         }
      }
      else
      {
#if 0
         Luma::Utils::Log::Write(Luma::Utils::Log::Level::Verbose, "Resolve source isn't cached.");
#endif
      }
      return false;
   }
#endif
   
   static ID3D11PixelShader* GetCharacterPixelShader(
      uint32_t pixel_shader_hash,
      ID3D11Device* native_device,
      GameDeviceHeavenBurnsRed& game_device_data)
   {
      auto shader_it = game_device_data.modified_character_pixel_shaders.find(pixel_shader_hash);
   
      if (shader_it != game_device_data.modified_character_pixel_shaders.end())
      {
         return shader_it->second.get();
      }
      else
      {
         const auto shader_code_it = game_device_data.pixel_shader_code.find(pixel_shader_hash);
         if (shader_code_it == game_device_data.pixel_shader_code.cend())
         {
            return nullptr;
         }

         std::vector<std::byte> shader_code = shader_code_it->second;
         
         PatchCharacterPixelShader(shader_code);
         
#if DEVELOPMENT
         Luma::Utils::Log::Write(Luma::Utils::Log::Level::Verbose, std::format("Character Pixel Shader Patched: 0x{:08X}", pixel_shader_hash).c_str());
#endif

         HRESULT hr = native_device->CreatePixelShader(shader_code.data(), shader_code.size(), nullptr, game_device_data.modified_character_pixel_shaders[pixel_shader_hash].put());
         if (FAILED(hr))
         {
            game_device_data.modified_character_pixel_shaders.erase(pixel_shader_hash);
            return nullptr;
         }
         return game_device_data.modified_character_pixel_shaders[pixel_shader_hash].get();
      }
   }
   
   DrawOrDispatchOverrideType OnDrawOrDispatch(
      ID3D11Device* native_device,
      ID3D11DeviceContext* native_device_context,
      CommandListData& cmd_list_data, DeviceData& device_data,
      ShaderStages stages,
      const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes,
      bool is_custom_pass, bool& updated_cbuffers,
      std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      if (enable_character_supersampling && (stages & ShaderStages::Compute) != 0)
      {
         bool is_skinning = false;
         while (true)
         {
            if (original_shader_hashes.Contains(shader_hashes_non_skinning))
               break;
            
            is_skinning = original_shader_hashes.Contains(shader_hashes_skinning);
            
            if (!is_skinning)
            {
               ComPtr<ID3D11ComputeShader> shader;
               native_device_context->CSGetShader(shader.put(), nullptr, nullptr);

               std::optional<std::string> optional_name = std::nullopt;//GetD3DNameW(shader.get());
               byte data[128] = {};
               UINT size = sizeof(data);
               if (shader.get()->GetPrivateData(WKPDID_D3DDebugObjectName, &size, data) == S_OK)
               {
                  if (size > 0)
                     optional_name = std::string{ data, data + size };
               }
               
               if (optional_name.has_value() && !optional_name->empty())
               {
                  if (optional_name->contains("Skinning"))
                  {
#if DEVELOPMENT
                     Luma::Utils::Log::Write(Luma::Utils::Log::Level::Verbose, std::format("Skinning Compute Shader: 0x{:08X}", original_shader_hashes.compute_shaders[0]).c_str());
#endif
                     is_skinning = true;
                     shader_hashes_skinning.compute_shaders.emplace(original_shader_hashes.compute_shaders[0]);
                  }
               }
            }
            
            if (!is_skinning)
            {
               shader_hashes_non_skinning.compute_shaders.emplace(original_shader_hashes.compute_shaders[0]);
            }
            break;
         }
            
         // it seems like almost all dynamic objects will be animated by skinning shaders
         if (is_skinning)
         {
            ComPtr<ID3D11UnorderedAccessView> uavs[5] = {};
            // batch skin supports max of 5
            native_device_context->CSGetUnorderedAccessViews(0, 5, reinterpret_cast<ID3D11UnorderedAccessView**>(uavs));

            for (const auto& uav : uavs)
            {
               if (!uav)
                  continue;

               ComPtr<ID3D11Resource> resource;
               uav->GetResource(resource.put());

               ComPtr<ID3D11Buffer> buffer;
               if (SUCCEEDED(resource->QueryInterface(buffer.put())))
                  game_device_data.skinned_vertex_buffers.insert(buffer.get());
            }
         }
         
         return DrawOrDispatchOverrideType::None;
      }
      
      if ((stages & ShaderStages::Pixel) == 0)
         return DrawOrDispatchOverrideType::None;
      
      if (original_shader_hashes.pixel_shaders.empty())
         return DrawOrDispatchOverrideType::None;
      
      if (game_device_data.is_current_rtv_ms)
      {
         if (!game_device_data.has_3d_scene_drawn)
         {
            game_device_data.has_3d_scene_drawn = true;

            if (game_device_data.tracked_color_rtv.get() != game_device_data.cached_source_color.rtv.get())
            {
               game_device_data.cached_source_color.rtv = game_device_data.tracked_color_rtv;

               if (game_device_data.tracked_color_resource &&
                   SUCCEEDED(game_device_data.tracked_color_resource->QueryInterface(game_device_data.cached_source_color.texture.put())))
               {
                  game_device_data.cached_source_color.texture->GetDesc(&game_device_data.cached_source_color.desc);

                  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
                  srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                  srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
                  // D3D11_TEX2DMS_SRV has no fields to set

                  const HRESULT hr = native_device->CreateShaderResourceView(game_device_data.cached_source_color.texture.get(), &srv_desc, game_device_data.cached_source_color.srv.put());
               }
            }
         }
         
         if (enable_alpha_to_coverage)
         {
            auto shader_it = game_device_data.modified_atoc_pixel_shaders.find(original_shader_hashes.pixel_shaders[0]);
         
            if (shader_it != game_device_data.modified_atoc_pixel_shaders.end())
            {
               ComPtr<ID3D11BlendState> blend_state;
               FLOAT blend_factor[4];
               UINT sample_mask;

               native_device_context->OMGetBlendState(blend_state.put(), blend_factor, &sample_mask);
               const auto blend_state_replacement = game_device_data.alpha_blend_states.find(blend_state.get());
               if (blend_state_replacement != game_device_data.alpha_blend_states.end())
               {
                  native_device_context->OMSetBlendState(blend_state_replacement->second.get(), blend_factor, sample_mask);
                  //Luma::Utils::Log::Write(Luma::Utils::Log::Level::Info, "Blend State: Replaced.");
               }
               else
               {
                  D3D11_BLEND_DESC desc;
                  blend_state->GetDesc(&desc);
                  desc.AlphaToCoverageEnable = true;
                  ComPtr<ID3D11BlendState> new_blend_state;
                  native_device->CreateBlendState(&desc, new_blend_state.put());
                  game_device_data.alpha_blend_states[blend_state.get()] = new_blend_state;
                  native_device_context->OMSetBlendState(new_blend_state.get(), blend_factor, sample_mask);
               }
               
               native_device_context->PSSetShader(shader_it->second.get(), nullptr, 0);
               (*original_draw_dispatch_func)();
               native_device_context->OMSetBlendState(blend_state.get(), blend_factor, sample_mask);
               
               return DrawOrDispatchOverrideType::Replaced;
            }
         }
         
         if (enable_character_supersampling)
         {
            auto shader_it = game_device_data.modified_character_pixel_shaders.find(original_shader_hashes.pixel_shaders[0]);
            if (shader_it != game_device_data.modified_character_pixel_shaders.end())
            {
               native_device_context->PSSetShader(shader_it->second.get(), nullptr, 0);
#if DEVELOPMENT
               if (test_index == 13)
               {
                  return DrawOrDispatchOverrideType::Skip;
               }
#endif
            }
            else
            {
               ComPtr<ID3D11Buffer> vertex_buffer;
               native_device_context->IAGetVertexBuffers(0, 1, vertex_buffer.put(), nullptr, nullptr);
               if (game_device_data.skinned_vertex_buffers.contains(vertex_buffer.get()))
               {
                  native_device_context->PSSetShader(GetCharacterPixelShader(original_shader_hashes.pixel_shaders[0], native_device, game_device_data), nullptr, 0);
               }
            }
            return DrawOrDispatchOverrideType::None;
         }
      }
      
      return DrawOrDispatchOverrideType::None;
   }
   
   void OnPresent(
      ID3D11Device* native_device,
      DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      
      game_device_data.skinned_vertex_buffers.clear();
      
      game_device_data.is_current_rtv_ms = false;
      game_device_data.has_3d_scene_drawn = false;
   }
   
   void LoadConfigs() override
   {
      Config::Get(NAME, "MSAA", msaa_index);
      Config::Get(NAME, "AlphaToCoverage", enable_alpha_to_coverage);
      Config::Get(NAME, "SuperSampling", enable_character_supersampling);
   }
   
   void DrawImGuiSettings(DeviceData& device_data) override
   {
      const char* labels[] = { "2x", "4x", "8x" };

      if (ImGui::SliderInt("MSAA", &msaa_index, 0, 2, labels[msaa_index]))
      {
         Config::Set(NAME, "MSAA", msaa_index);
      }
      
      const char* labels_toggle[] = { "Off", "On" };
      
      if (ImGui::SliderInt("Alpha To Coverage", &enable_alpha_to_coverage, 0, 1, labels_toggle[enable_alpha_to_coverage]))
      {
         Config::Set(NAME, "AlphaToCoverage", enable_alpha_to_coverage);
      }
      
      if (ImGui::SliderInt("Character Supersampling", &enable_character_supersampling, 0, 1, labels_toggle[enable_character_supersampling]))
      {
         Config::Set(NAME, "SuperSampling", enable_character_supersampling);
      }
   }
   
   void PrintImGuiAbout() override
   {
      ImGui::Text("Heaven Burns Red Luma mod - about and credits section", "");
   }
};
   
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Heaven Burns Red Luma mod");
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::Playable;
      Globals::VERSION = 1;
      
      swapchain_format_upgrade_type = TextureFormatUpgradesType::None;
      swapchain_upgrade_type = SwapchainUpgradeType::None;
      texture_format_upgrades_type = TextureFormatUpgradesType::None;
      force_disable_display_composition = true;
      
      enable_samplers_upgrade = false; // Exit hang

      SetGame<HeavenBurnsRed>();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);
   
   return TRUE;
}