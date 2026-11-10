#pragma once
#if !OFFLINE_PATCHER
#include "..\..\..\Core\includes\shader_patching.h"
#else
#include "..\..\..\External/WDK/includes/d3d11TokenizedProgramFormat.hpp"
#endif

/*
      if (type != D3D11_SHVER_VERTEX_SHADER)
      {
         if (shader_hashes_ClothPreTransformed.Contains(shader_hash, ShaderStages::Vertex))
            return nullptr;
         
         DXBCHeader* dxbc_header = (DXBCHeader*)&shader_object[0];
         int32_t viewport_cb_index = -1;
         for (uint32_t i = 0; i < dxbc_header->chunk_count; ++i)
         {
            if (strncmp((const char*)&shader_object[dxbc_header->chunk_offsets[i]], "RDEF", 4) == 0)
            {
               uint32_t rdef_offset = dxbc_header->chunk_offsets[i];
               const std::byte* rdef = &shader_object[dxbc_header->chunk_offsets[i]];
               RDEFHeader* rdef_header = (RDEFHeader*)rdef;
               uint32_t rdef_header_offset = rdef_offset + 8;
               uint32_t inserted_bytes = 0;
               ConstantBufferDesc* constant_buffers = (ConstantBufferDesc*)&shader_object[rdef_header_offset + rdef_header->constant_buffer_offset];
               ResourceBindingDesc* resource_bindings = (ResourceBindingDesc*)&shader_object[rdef_header_offset + rdef_header->resource_binding_offset];
               
               for (uint32_t j = 0; j < rdef_header->constant_buffer_count; ++j)
               {
                  const char* name = (char*)&shader_object[rdef_header_offset + constant_buffers[j].name_offset];
                  if (strcmp(name, "Viewport") == 0)
                  {
                     viewport_cb_index = j;
                     break;
                  }
               }
               
               if (viewport_cb_index == -1)
               {
                  return nullptr;
               }
               break;
            }
         }
         
         // For luma function, start pos is the first optoken
         uint32_t pos = 0;
         int32_t dcl_cb_insert_pos = -1;
         int32_t minus_cam_insert_pos = -1;
         uint32_t world_pos_reg_slot = 0;
         std::vector<std::byte> shader_code((const std::byte*)code, ((const std::byte*)code) + size);
         D3D10_SB_OPCODE_TYPE prev_opcode_type = D3D10_SB_NUM_OPCODES;
         std::byte* shex = shader_code.data();
         size_t shader_size = size;
         size_t insert_size = 0;
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
            
            if (opcode_type == D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER)
            {
               if (*(uint32_t*)(shex + pos + 8) == viewport_cb_index)
               {
                  dcl_cb_insert_pos = pos + len * 4;
               }
            }
            else if (opcode_type == D3D10_SB_OPCODE_DP4 && len == 8)
            {
               D3D10_SB_OPERAND_TYPE first_type = DECODE_D3D10_SB_OPERAND_TYPE(*(uint32_t*)(shex + pos + 4));
               D3D10_SB_OPERAND_NUM_COMPONENTS first_components = DECODE_D3D10_SB_OPERAND_NUM_COMPONENTS(*(uint32_t*)(shex + pos + 4));
               if ((first_type == D3D10_SB_OPERAND_TYPE_TEMP || first_type == D3D10_SB_OPERAND_TYPE_OUTPUT) && first_components == D3D10_SB_OPERAND_4_COMPONENT)
               {
                  D3D10_SB_OPERAND_TYPE second_type = DECODE_D3D10_SB_OPERAND_TYPE(*(uint32_t*)(shex + pos + 12));
                  D3D10_SB_OPERAND_NUM_COMPONENTS second_components = DECODE_D3D10_SB_OPERAND_NUM_COMPONENTS(*(uint32_t*)(shex + pos + 12));
                  if (second_type == D3D10_SB_OPERAND_TYPE_TEMP && second_components == D3D10_SB_OPERAND_4_COMPONENT)
                  {
                     D3D10_SB_OPERAND_TYPE third_type = DECODE_D3D10_SB_OPERAND_TYPE(*(uint32_t*)(shex + pos + 20));
                     D3D10_SB_OPERAND_NUM_COMPONENTS third_components = DECODE_D3D10_SB_OPERAND_NUM_COMPONENTS(*(uint32_t*)(shex + pos + 20));
                     if (third_type == D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER && third_components == D3D10_SB_OPERAND_4_COMPONENT)
                     {
                        if (*(uint32_t*)(shex + pos + 24) == viewport_cb_index)
                        {
                           if (*(uint32_t*)(shex + pos + 28) == 20)
                           {
                              Luma::Utils::Log::Write(Luma::Utils::Log::Level::Info, "Found motion vector shader.");
                              minus_cam_insert_pos = pos;
                              world_pos_reg_slot = *(uint32_t*)(shex + pos + 16);
                              *(uint32_t*)(shex + pos + 24) = luma_data_cbuffer_index;
                              *(uint32_t*)(shex + pos + 28) = 7;
                           }
                           else if (*(uint32_t*)(shex + pos + 28) == 21)
                           {
                              *(uint32_t*)(shex + pos + 24) = luma_data_cbuffer_index;
                              *(uint32_t*)(shex + pos + 28) = 8;
                           }
                           else if (*(uint32_t*)(shex + pos + 28) == 23)
                           {
                              *(uint32_t*)(shex + pos + 24) = luma_data_cbuffer_index;
                              *(uint32_t*)(shex + pos + 28) = 10;
                           }
                        }
                     }
                  }
               }
            }
            
            if (pos + len * 4 >= size)
            {
               break;
            }
            
            prev_opcode_type = opcode_type;
            pos += len * 4;
         }
         
         if (dcl_cb_insert_pos == -1)
            return nullptr;
         
         if (minus_cam_insert_pos == -1)
            return nullptr;
         
         {
            std::vector<uint32_t> shader_patch = {
               0x04000059, 0x00208E46, luma_data_cbuffer_index, 0x00000010
            };
            
            shader_code.insert(shader_code.begin() + dcl_cb_insert_pos, (std::byte*)&shader_patch[0], (std::byte*)(&shader_patch[0] + shader_patch.size()));
            insert_size += shader_patch.size() * sizeof(uint32_t);
         }
         
         {
            std::vector<uint32_t> shader_patch = {
               0x09000000,
               0x00100072, world_pos_reg_slot,
               0x00100246, world_pos_reg_slot,
               0x80208246, 0x00000041, 0x0000000B, 0x0000000B,
            };
            
            shader_code.insert(shader_code.begin() + minus_cam_insert_pos + insert_size, (std::byte*)&shader_patch[0], (std::byte*)(&shader_patch[0] + shader_patch.size()));
            insert_size += shader_patch.size() * sizeof(uint32_t);
         }
         
         auto shader_code_ptr = std::make_unique<std::byte[]>(shader_code.size());
         std::memcpy(shader_code_ptr.get(), shader_code.data(), shader_code.size());
         size = shader_code.size();
         
         {
            const std::unique_lock lock(materials_mutex);
            shader_hashes_Materials.vertex_shaders.emplace(uint32_t(shader_hash));
         }
         
         return shader_code_ptr;
      }
      
      
#if 0
      {
         std::unique_ptr<std::byte[]> new_code = nullptr;
         
         using namespace System;
         static const std::vector<System::BytePattern> pattern = {
            0x36, 0x00, 0x00, 0x05, 0x82, 0x00, 0x10, 0x00, ANY, ANY, ANY, ANY, 0x01, 0x40, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F,  //mov r1.w, l(1.000000)
            0x11, 0x00, 0x00, 0x08, ANY, ANY, ANY, ANY, ANY, ANY, ANY, ANY, 0x46, 0x0E, 0x10, 0x00, ANY, ANY, ANY, ANY, 0x46, 0x8E, 0x20, 0x00, ANY, ANY, ANY, ANY, 0x14, 0x00, 0x00, 0x00, //dp4 r0.x, r1.xyzw, cb0[20].xyzw
            0x11, 0x00, 0x00, 0x08, ANY, ANY, ANY, ANY, ANY, ANY, ANY, ANY, 0x46, 0x0E, 0x10, 0x00, ANY, ANY, ANY, ANY, 0x46, 0x8E, 0x20, 0x00, ANY, ANY, ANY, ANY, 0x15, 0x00, 0x00, 0x00, //dp4 r0.y, r1.xyzw, cb0[21].xyzw
            0x11, 0x00, 0x00, 0x08, ANY, ANY, ANY, ANY, ANY, ANY, ANY, ANY, 0x46, 0x0E, 0x10, 0x00, ANY, ANY, ANY, ANY, 0x46, 0x8E, 0x20, 0x00, ANY, ANY, ANY, ANY, 0x17, 0x00, 0x00, 0x00  //dp4 o4.z, r1.xyzw, cb0[23].xyzw
         };
         
         static const std::vector<System::BytePattern> dcl_cb_pattern = {
            0x59, 0x00, 0x00, 0x04, 0x46, 0x8E, 0x20, 0x00, ANY, ANY, ANY, ANY, ANY, ANY, ANY, ANY, //dcl_constantbuffer cb0[181], immediateIndexed
         };
         
         word_t instruction_operand_register; // we don't know register so use wildcard byte pattern
         
         std::vector<uint8_t> appended_patch = {
            0x00, 0x00, 0x00, 0x09, // length(9)
            0x72, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, //add r0.xyz
            0x46, 0x02, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, //r0.xyzx
            0x46, 0x82, 0x20, 0x80, 0x41, 0x00, 0x00, 0x00, //-cb__index__.xyzx
            0x0B, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, //11[11]
         };
         
         std::vector<uint8_t> appended_patch_cb = {
            0x59, 0x00, 0x00, 0x04, 0x46, 0x8E, 0x20, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, //dcl_constantbuffer cb11[16], immediateIndexed
         };
         
         std::vector<std::byte*> matches;
         matches = ScanMemoryForPattern(reinterpret_cast<const std::byte*>(code), size, pattern , true);
         
         if (!matches.empty())
         {
            {
               //const std::unique_lock lock(materials_mutex);
               shader_hashes_Materials.vertex_shaders.emplace(uint32_t(shader_hash));
            }
            
            Luma::Utils::Log::Write(Luma::Utils::Log::Level::Info, "Found motion vector shader.");
            std::byte* match_address = matches[0];
            instruction_operand_register.u = *reinterpret_cast<uint32_t*>(match_address+8);
            for (int i = 0; i < 4; i++)
            {
               appended_patch[8 + i]  = static_cast<uint8_t>(instruction_operand_register.b[i]);
               appended_patch[16 + i] = static_cast<uint8_t>(instruction_operand_register.b[i]);
            }
            
            new_code = std::make_unique<std::byte[]>(size + appended_patch.size() + appended_patch_cb.size());
            
         
            std::vector<std::byte*> matches_cb;
            matches_cb = ScanMemoryForPattern(reinterpret_cast<const std::byte*>(code), size, dcl_cb_pattern , true);
            
            if (!matches_cb.empty())
            {
               size_t insert_pos_cb = matches_cb[0] - code;
               // Copy everything before pattern
               std::memcpy(new_code.get(), code, insert_pos_cb);
               // Insert the patch
               std::memcpy(new_code.get() + insert_pos_cb, appended_patch_cb.data(), appended_patch_cb.size());
               
               size_t new_base = appended_patch_cb.size() + insert_pos_cb;
               
               size_t insert_pos = match_address - code;
               // Copy everything from pattern cb to pattern
               std::memcpy(new_code.get() + new_base, code + insert_pos_cb, insert_pos - insert_pos_cb);
               // Insert the patch
               std::memcpy(new_code.get() + appended_patch_cb.size() + insert_pos, appended_patch.data(), appended_patch.size());
               // Copy the rest (including the return instruction)
               std::memcpy(new_code.get() + appended_patch_cb.size() + insert_pos + appended_patch.size(), code + insert_pos, size - insert_pos);
               
               static const uint8_t cb_07_bytes[8] = {
                  0x0B, 0x00, 0x00, 0x00,   // cb11
                  0x07, 0x00, 0x00, 0x00    // [7]
               };
               static const uint8_t cb_08_bytes[8] = {
                  0x0B, 0x00, 0x00, 0x00,   // cb11
                  0x08, 0x00, 0x00, 0x00    // [8]
               };
               static const uint8_t cb_10_bytes[8] = {
                  0x0B, 0x00, 0x00, 0x00,   // cb11
                  0x0A, 0x00, 0x00, 0x00    // [10]
               };
               
               std::memcpy(new_code.get() + appended_patch_cb.size() + insert_pos + appended_patch.size() + 44, cb_07_bytes, 8);
               std::memcpy(new_code.get() + appended_patch_cb.size() + insert_pos + appended_patch.size() + 76, cb_08_bytes, 8);
               std::memcpy(new_code.get() + appended_patch_cb.size() + insert_pos + appended_patch.size() + 108, cb_10_bytes, 8);
               
               size += appended_patch.size() + appended_patch_cb.size();
            }
         }
         
         return new_code;
#endif
*/

struct RDEFHeader
{
   char chunk_name[4]; // 'RDEF'
   uint32_t chunk_size;
   uint32_t constant_buffer_count;
   uint32_t constant_buffer_offset;
   uint32_t resource_binding_count;
   uint32_t resource_binding_offset;
   uint8_t version_minor;
   uint8_t version_major;
   uint16_t program_type;
   uint32_t flags;
   uint32_t creator_string_offset;
};

struct ConstantBufferDesc
{
   uint32_t name_offset;
   uint32_t variable_count;
   uint32_t variable_desc_offset;
   uint32_t size;
   uint32_t flags;
   uint32_t cbuffer_type;
};

struct ResourceBindingDesc
{
   uint32_t name_offset;
   uint32_t input_type;
   uint32_t resource_return_type;
   uint32_t view_dimension;
   uint32_t sample_count;
   uint32_t bind_point;
   uint32_t bind_count;
   uint32_t flags;
};

struct VariableDesc
{
   uint32_t name_offset;
   uint32_t data_offset;
   uint32_t size;
   uint32_t flags;
   uint32_t type_offset;
   uint32_t default_value_offset;
   uint32_t start_texture;
   uint32_t texture_size;
   uint32_t start_sampler;
   uint32_t sampler_size;
};

struct VariableTypeDesc
{
   uint16_t variable_class;
   uint16_t variable_type;
   uint16_t row_count;
   uint16_t column_count;
   uint16_t element_count;
   uint16_t member_count;
   uint32_t member_offset;
   uint8_t reserved[16];
   uint32_t name_offset;
};

struct DXBCSignatureEntry
{
   uint32_t name_offset;
   uint32_t semantic_index;
   uint32_t value_type;
   uint32_t component_type;
   uint32_t reg;
   uint8_t component_mask;
   uint8_t read_write_mask;
};

struct SHEXHeader
{
   char chunk_name[4]; // 'SHEX'
   uint32_t chunk_size;
   uint8_t version;
   uint16_t type;
   uint32_t dword_count;
};

struct OutputWriteEntry
{
   uint32_t index = 255;
   uint32_t index_offset;
   uint32_t instruction_length;
   uint32_t instruction_offset;
};

bool PatchVertexShader(std::vector<std::byte>& shader_code)
{
   DXBCHeader* dxbc_header = (DXBCHeader*)&shader_code[0];
   int32_t viewport_cb_index = -1;
   
   bool modified = false;

   for (uint32_t i = 0; i < dxbc_header->chunk_count; ++i)
   {
      if (strncmp((const char*)&shader_code[dxbc_header->chunk_offsets[i]], "RDEF", 4) == 0)
      {
         uint32_t rdef_offset = dxbc_header->chunk_offsets[i];
         const std::byte* rdef = &shader_code[dxbc_header->chunk_offsets[i]];
         RDEFHeader* rdef_header = (RDEFHeader*)rdef;
         uint32_t rdef_header_offset = rdef_offset + 8;
         uint32_t inserted_bytes = 0;
         ConstantBufferDesc* constant_buffers = (ConstantBufferDesc*)&shader_code[rdef_header_offset + rdef_header->constant_buffer_offset];
         ResourceBindingDesc* resource_bindings = (ResourceBindingDesc*)&shader_code[rdef_header_offset + rdef_header->resource_binding_offset];
               
         for (uint32_t j = 0; j < rdef_header->constant_buffer_count; ++j)
         {
            const char* name = (char*)&shader_code[rdef_header_offset + constant_buffers[j].name_offset];
            if (strcmp(name, "Viewport") == 0)
            {
               viewport_cb_index = j;
               break;
            }
         }
               
         if (viewport_cb_index == -1)
         {
            return false;
         }
      }
      else if (strncmp((const char*)&shader_code[dxbc_header->chunk_offsets[i]], "SHEX", 4) == 0)
      {
         std::byte* shex = &shader_code[dxbc_header->chunk_offsets[i]];
         SHEXHeader* shex_header = (SHEXHeader*)shex;
         
         uint32_t pos = 16;
         int32_t dcl_cb_insert_pos = -1;
         int32_t minus_cam_insert_pos = -1;
         uint32_t world_pos_reg_slot = 0;
         size_t insert_size = 0;
         
         D3D10_SB_OPCODE_TYPE prev_opcode_type = D3D10_SB_NUM_OPCODES;
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
            
            if (opcode_type == D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER)
            {
               if (*(uint32_t*)(shex + pos + 8) == viewport_cb_index)
               {
                  //std::cout << "VP cb found!" << std::endl;
                  dcl_cb_insert_pos = pos + len * 4;
               }
            }
            else if (opcode_type == D3D10_SB_OPCODE_DP4 && len == 8)
            {
               //std::cout << "Found DP4!" << std::endl;
               
               D3D10_SB_OPERAND_TYPE first_type = DECODE_D3D10_SB_OPERAND_TYPE(*(uint32_t*)(shex + pos + 4));
               D3D10_SB_OPERAND_NUM_COMPONENTS first_components = DECODE_D3D10_SB_OPERAND_NUM_COMPONENTS(*(uint32_t*)(shex + pos + 4));
               if ((first_type == D3D10_SB_OPERAND_TYPE_TEMP || first_type == D3D10_SB_OPERAND_TYPE_OUTPUT) && first_components == D3D10_SB_OPERAND_4_COMPONENT)
               {
                  //std::cout << "DP4 Match first!" << std::endl;
                  
                  D3D10_SB_OPERAND_TYPE second_type = DECODE_D3D10_SB_OPERAND_TYPE(*(uint32_t*)(shex + pos + 12));
                  D3D10_SB_OPERAND_NUM_COMPONENTS second_components = DECODE_D3D10_SB_OPERAND_NUM_COMPONENTS(*(uint32_t*)(shex + pos + 12));
                  if (second_type == D3D10_SB_OPERAND_TYPE_TEMP && second_components == D3D10_SB_OPERAND_4_COMPONENT)
                  {
                     //std::cout << "DP4 Match second!" << std::endl;
                     
                     D3D10_SB_OPERAND_TYPE third_type = DECODE_D3D10_SB_OPERAND_TYPE(*(uint32_t*)(shex + pos + 20));
                     D3D10_SB_OPERAND_NUM_COMPONENTS third_components = DECODE_D3D10_SB_OPERAND_NUM_COMPONENTS(*(uint32_t*)(shex + pos + 20));
                     if (third_type == D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER && third_components == D3D10_SB_OPERAND_4_COMPONENT)
                     {
                        //std::cout << "DP4 Match third!" << std::endl;
                        
                        if (*(uint32_t*)(shex + pos + 24) == viewport_cb_index)
                        {
                           //std::cout << "DP4 Match viewport index!" << std::endl;
                           
                           if (*(uint32_t*)(shex + pos + 28) == 20)
                           {
                              //std::cout << "DP4 Match viewport cb slot 20!" << std::endl;
                              //Luma::Utils::Log::Write(Luma::Utils::Log::Level::Info, "Found motion vector shader.");
                              minus_cam_insert_pos = pos;
                              world_pos_reg_slot = *(uint32_t*)(shex + pos + 16);
                              *(uint32_t*)(shex + pos + 24) = 11;
                              *(uint32_t*)(shex + pos + 28) = 7;
                           }
                           else if (*(uint32_t*)(shex + pos + 28) == 21)
                           {
                              //std::cout << "DP4 Match viewport cb slot 21!" << std::endl;
                              *(uint32_t*)(shex + pos + 24) = 11;
                              *(uint32_t*)(shex + pos + 28) = 8;
                           }
                           else if (*(uint32_t*)(shex + pos + 28) == 23)
                           {
                              //std::cout << "DP4 Match viewport cb slot 23!" << std::endl;
                              *(uint32_t*)(shex + pos + 24) = 11;
                              *(uint32_t*)(shex + pos + 28) = 10;
                           }
                        }
                     }
                  }
               }
            }
            
            if (pos + len * 4 >= shex_header->chunk_size + 8)
            {
               break;
            }
            
            prev_opcode_type = opcode_type;
            pos += len * 4;
         }
         
         if (dcl_cb_insert_pos == -1)
            return false;
         
         if (minus_cam_insert_pos == -1)
            return false;
         
         //std::cout << "Patching code!" << std::endl;
         
         {
            std::vector<uint32_t> shader_patch = {
               0x04000059, 0x00208E46, 11, 0x0000000C
            };
            
            shader_code.insert(shader_code.begin() + dxbc_header->chunk_offsets[i] + dcl_cb_insert_pos, (std::byte*)&shader_patch[0], (std::byte*)(&shader_patch[0] + shader_patch.size()));
            insert_size += shader_patch.size() * sizeof(uint32_t);
            
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
         
         {
            std::vector<uint32_t> shader_patch = {
               0x09000000,
               0x00100072, world_pos_reg_slot,
               0x00100246, world_pos_reg_slot,
               0x80208246, 0x00000041, 0x0000000B, 0x0000000B,
            };
            
            shader_code.insert(shader_code.begin() + dxbc_header->chunk_offsets[i] + minus_cam_insert_pos + insert_size, (std::byte*)&shader_patch[0], (std::byte*)(&shader_patch[0] + shader_patch.size()));
            insert_size += shader_patch.size() * sizeof(uint32_t);
            
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
         
         modified = true;
      }
   }
   
   dxbc_header->file_size = shader_code.size();
   Hash::MD5::Digest md5_digest = CalcDXBCHash(shader_code.data(), shader_code.size());
   std::memcpy(&dxbc_header->hash, &md5_digest.data, DXBCHeader::hash_size);
   
   return modified;
}

void PatchPixelShader(std::vector<std::byte>& shader_code)
{
}

void PatchComputeShader(std::vector<std::byte>& shader_code)
{
}

uint16_t GetShaderProgramType(std::vector<std::byte>& shader_code)
{
   DXBCHeader* dxbc_header = (DXBCHeader*)&shader_code[0];

   for (uint32_t i = 0; i < dxbc_header->chunk_count; ++i)
   {
      if (strncmp((const char*)&shader_code[dxbc_header->chunk_offsets[i]], "SHEX", 4) == 0)
      {
         std::byte* shex = &shader_code[dxbc_header->chunk_offsets[i]];
         uint16_t type = *(uint16_t*)(shex + 10);
         return type;
      }
   }
   return 0xFFF0;
}