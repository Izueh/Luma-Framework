#include <include/reshade.hpp>
#include <d3d11TokenizedProgramFormat.hpp>
#include "..\..\..\Core\utils\system.h"

union word_t
{
   float     f;
   int32_t   i;
   uint32_t  u;
   std::byte b[4];
};

struct GlobalCBInfo
{
   size_t  size                          = 0;  // size of the global cbuffer
   int32_t jitter_index                  = -1; // index of the jitter vector in the global cbuffer (this is unreliable since not all UE4 versions have it)
   int32_t view_to_clip_start_index      = -1; // start index of the clip to view matrix in the global cbuffer
   int32_t view_size_and_inv_size_index  = -1; // index of the view size and inverse size vector in the global cbuffer
   int32_t clip_to_prev_clip_start_index = -1;
};

struct TAAShaderInfo
{
   size_t declared_cbuffer_size;
   size_t max_texture_register = 16;
   int32_t global_buffer_register_index = -1;
   int32_t clip_to_prev_clip_start_index = -1;
   int32_t source_texture_register       = -1;
   int32_t depth_texture_register       = -1;
   int32_t velocity_texture_register    = -1;
   bool found_all = false;
};

static uint32_t* FindLargestCBufferDeclaration(const uint32_t* code_u32, const size_t size_u32)
{
   uint32_t  offset                  = 0;
   size_t    max_cbuffer_size        = 0;
   size_t    instruction_count       = 0;
   bool      found_first_dcl_cbuffer = false;
   uint32_t* max_cbuffer_declaration = nullptr;
   while (offset < size_u32)
   {
      if (instruction_count > 16 && !found_first_dcl_cbuffer)
         break; // bail out if we reached too far without finding any cbuffer declarations
      const uint32_t token  = code_u32[offset];
      const uint32_t opcode = DECODE_D3D10_SB_OPCODE_TYPE(token);
      uint32_t       len    = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(token);
      len                   = len == 0 ? 1 : len;

      if (opcode == D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER)
      {
         found_first_dcl_cbuffer = true;
         // operand 0 is the cbuffer index
         const uint32_t* operand_start = code_u32 + offset + 1;
         const uint32_t  buffer_size   = operand_start[2];
         if (buffer_size > max_cbuffer_size)
         {
            max_cbuffer_size        = buffer_size;
            max_cbuffer_declaration = const_cast<uint32_t*>(code_u32 + offset);
         }
      }
      else
      {
         if (found_first_dcl_cbuffer)
            break; // we scanned all cbuffer declarations
      }
      instruction_count++;
      offset += len;
   }
   return max_cbuffer_declaration;
}

static bool IsUE4TAACandidate(const std::byte* code, size_t size, TAAShaderInfo& taa_shader_info)
{
   // detects if the shader is a UE4 TAA Pixel Shader
   // first we should check resource declarations for textures:
   // taa usually has two color textures (the current frame and the history frame)
   // depth texture
   // velocity texture (unorm RG texture)
   // they should all have return type float on all components
   // iterate over bytecode, texture declarations are usually near the top so we should look until we find the first dcl_resource
   // they are likely consecutive so we iterate until we find a non dcl_resource opcode
   // dcl_resource tN, resourceType, returnType(s) this is the assembly signature
   // after we should look for decode velocity instructions to confirm

   const uint32_t* code_u32                        = reinterpret_cast<const uint32_t*>(code);
   const size_t    size_u32                        = size / sizeof(uint32_t);
   bool            found_non_texture_declaration   = false;
   size_t          offset                          = 0;
   size_t          detected_2d_texture_float_count = 0;
   size_t          detected_3d_texture_float_count = 0; // can be dithering texture, should hopefully always be just one or none
   size_t          instruction_count               = 0;
   int32_t         max_texture_register            = -1;
   while (offset < size_u32)
   {
      if (instruction_count > 16)
         return false; // bail out if we reached too far without finding any texture declarations
      const uint32_t token  = code_u32[offset];
      const uint32_t opcode = DECODE_D3D10_SB_OPCODE_TYPE(token);
      uint32_t       len    = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(token);
      len                   = len == 0 ? 1 : len;

      if (opcode == D3D10_SB_OPCODE_DCL_RESOURCE)
      {
         break;
      }
      else
      {
         offset += len;
         instruction_count++;
      }
   }
   while (offset < size_u32 && !found_non_texture_declaration)
   {
      // code_u32[offset] is the current instruction
      // code_u32[offset + 1] is the first operand
      // code_u32[offset + 2] operand index
      // code_u32[offset + 3] is the resource return type
      const uint32_t token         = code_u32[offset];
      const uint32_t opcode        = DECODE_D3D10_SB_OPCODE_TYPE(token);
      uint32_t       len           = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(token);
      const uint32_t resource_type = DECODE_D3D10_SB_RESOURCE_DIMENSION(token);
      len                          = len == 0 ? 1 : len;

      if (opcode == D3D10_SB_OPCODE_DCL_RESOURCE)
      {
         // check resource type and return type
         const uint32_t resource_return_type_token = code_u32[offset + 3];
         const uint32_t register_index = code_u32[offset + 2]; //asume immediate32
         bool           all_float_return =
            DECODE_D3D10_SB_RESOURCE_RETURN_TYPE(resource_return_type_token, D3D10_SB_4_COMPONENT_X) == D3D10_SB_RETURN_TYPE_FLOAT &&
            DECODE_D3D10_SB_RESOURCE_RETURN_TYPE(resource_return_type_token, D3D10_SB_4_COMPONENT_Y) == D3D10_SB_RETURN_TYPE_FLOAT &&
            DECODE_D3D10_SB_RESOURCE_RETURN_TYPE(resource_return_type_token, D3D10_SB_4_COMPONENT_Z) == D3D10_SB_RETURN_TYPE_FLOAT &&
            DECODE_D3D10_SB_RESOURCE_RETURN_TYPE(resource_return_type_token, D3D10_SB_4_COMPONENT_W) == D3D10_SB_RETURN_TYPE_FLOAT;

         max_texture_register = std::max<int32_t>(max_texture_register, static_cast<int32_t>(register_index));
         // velocity texture is usually a 2D texture with unorm RG return type
         if (resource_type == D3D10_SB_RESOURCE_DIMENSION_TEXTURE2D && all_float_return)
         {
            detected_2d_texture_float_count++;
         }
         else if (resource_type == D3D10_SB_RESOURCE_DIMENSION_TEXTURE3D && all_float_return)
         {
            detected_3d_texture_float_count++;
         }
         offset += len;
      }
      else
      {
         found_non_texture_declaration = true;
      }
   }

   if (detected_2d_texture_float_count < 4 || detected_3d_texture_float_count > 1)
      return false;

   taa_shader_info.max_texture_register       = static_cast<int32_t>(max_texture_register);
   // now look for velocity decode instructions
   // usually velocity is decoded with a sequence like:
   // float2 DecodeVelocityFromTexture(float2 In)
   // {
   // #if 1
   //     return (In - (32767.0f / 65535.0f)) / (0.499f * 0.5f);
   // #else // MAD layout to help compiler. This is what UE/FF7 used but it's an unnecessary approximation
   //     const float InvDiv = 1.0f / (0.499f * 0.5f);
   //     return In * InvDiv - 32767.0f / 65535.0f * InvDiv;
   // #endif
   // }
   // in hlsl
   // in assembly it looks something like:
   // add r5.yz, r5.yyzy, l(0.000000, -0.499992, -0.499992, 0.000000) this one seems to vary
   // mul r5.yz, r5.yyzy, l(0.000000, 4.008016, 4.008016, 0.000000)
   // in some games this operation is made with a mad instruction instead of add+mul
   // mad r1.yz, r1.zzyz, l(0.000000, 4.008016, 4.008016, 0.000000), l(0.000000, -2.003978, -2.003978, 0.000000)
   // we should look for the immediate values using ScanMemoryForPattern
   // then look backwards for an add/mul? probably not needed

   word_t mul_1;
   mul_1.f = 4.00801611f;
   word_t mul_2;
   mul_2.f                                  = 0.000000f;
   std::vector<std::byte> mul_pattern_bytes = {
      std::byte{mul_1.b[0]}, std::byte{mul_1.b[1]}, std::byte{mul_1.b[2]}, std::byte{mul_1.b[3]},
      std::byte{mul_1.b[0]}, std::byte{mul_1.b[1]}, std::byte{mul_1.b[2]}, std::byte{mul_1.b[3]}};
   std::vector<std::byte*> mul_hits = System::ScanMemoryForPattern(code, size, mul_pattern_bytes);
   if (!mul_hits.empty())
      return true;

   mul_pattern_bytes = {
      std::byte{mul_1.b[0]}, std::byte{mul_1.b[1]}, std::byte{mul_1.b[2]}, std::byte{mul_1.b[3]},
      std::byte{mul_2.b[0]}, std::byte{mul_2.b[1]}, std::byte{mul_2.b[2]}, std::byte{mul_2.b[3]},
      std::byte{mul_1.b[0]}, std::byte{mul_1.b[1]}, std::byte{mul_1.b[2]}, std::byte{mul_1.b[3]}};
   mul_hits = System::ScanMemoryForPattern(code, size, mul_pattern_bytes);
   if (!mul_hits.empty())
      return true;

   mul_pattern_bytes = {
      std::byte{mul_1.b[0]}, std::byte{mul_1.b[1]}, std::byte{mul_1.b[2]}, std::byte{mul_1.b[3]},
      std::byte{mul_2.b[0]}, std::byte{mul_2.b[1]}, std::byte{mul_2.b[2]}, std::byte{mul_2.b[3]},
      std::byte{mul_2.b[0]}, std::byte{mul_2.b[1]}, std::byte{mul_2.b[2]}, std::byte{mul_2.b[3]},
      std::byte{mul_1.b[0]}, std::byte{mul_1.b[1]}, std::byte{mul_1.b[2]}, std::byte{mul_1.b[3]}};
   mul_hits = System::ScanMemoryForPattern(code, size, mul_pattern_bytes);
   if (!mul_hits.empty())
      return true;

   return false;
}

static bool FindShaderInfo(const std::byte* code, size_t size, TAAShaderInfo& taa_shader_info)
{
   // The strategy here is to identify the largest cbuffer (likely global cbuffer) and look for 4 consecutive float4 loads from it
   // with indices that make a range of a 4x4 matrix (the mask xywx can be a hint too)
   // below is an example from an Unreal Engine 4 TAA shader
   // mul r3.xyz, r0.wwww, cb1[119].xywx
   // mad r3.xyz, r0.zzzz, cb1[118].xywx, r3.xyzx
   // mad r3.xyz, r2.xxxx, cb1[120].xywx, r3.xyzx
   // add r3.xyz, r3.xyzx, cb1[121].xywx
   // one way to do it is to scan for all operand tokens that are cbuffer loads, then group them by cbuffer index and look for 4 consecutive indices
   // can use the pointer address to check if the operands are in instructions close to each other, we can use some heuristics with an average instruction size
   // and look for 4 indices that are a max of x instructions apart

   const uint32_t* code_u32                = reinterpret_cast<const uint32_t*>(code);
   const uint32_t  size_u32                = size / sizeof(uint32_t);
   uint32_t*       max_cbuffer_declaration = FindLargestCBufferDeclaration(code_u32, size_u32);

   if (max_cbuffer_declaration == nullptr)
      return false;
   // cb1[121].xywx
   word_t cbuffer_operand_pattern_tok;
   cbuffer_operand_pattern_tok.u =
      ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
      ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
      ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(0, 1, 3, 0) |
      ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER) |
      ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_2D) |
      ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) |
      ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(1, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);

   word_t cbuffer_operand_register;
   cbuffer_operand_register.u = max_cbuffer_declaration[2];

   std::vector<std::byte> cbuffer_operand_pattern = {
      std::byte{cbuffer_operand_pattern_tok.b[0]},
      std::byte{cbuffer_operand_pattern_tok.b[1]},
      std::byte{cbuffer_operand_pattern_tok.b[2]},
      std::byte{cbuffer_operand_pattern_tok.b[3]},
      std::byte{cbuffer_operand_register.b[0]},
      std::byte{cbuffer_operand_register.b[1]},
      std::byte{cbuffer_operand_register.b[2]},
      std::byte{cbuffer_operand_register.b[3]},
   };

   std::vector<std::byte*> cbuffer_operand_hits = System::ScanMemoryForPattern(code, size, cbuffer_operand_pattern);
   if (cbuffer_operand_hits.size() < 4) // try xxyw instead of xywx
   {
      cbuffer_operand_pattern_tok.u =
         ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
         ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
         ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(0, 0, 1, 3) |
         ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER) |
         ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_2D) |
         ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) |
         ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(1, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
      cbuffer_operand_pattern[0] = std::byte{cbuffer_operand_pattern_tok.b[0]};
      cbuffer_operand_pattern[1] = std::byte{cbuffer_operand_pattern_tok.b[1]};
      cbuffer_operand_pattern[2] = std::byte{cbuffer_operand_pattern_tok.b[2]};
      cbuffer_operand_pattern[3] = std::byte{cbuffer_operand_pattern_tok.b[3]};
      cbuffer_operand_hits = System::ScanMemoryForPattern(code, size, cbuffer_operand_pattern);
      if (cbuffer_operand_hits.size() < 4)
         return false; // not enough hits
   }

   // iterate over hits and store the index (word after the register) and the instruction offset (to group by proximity)

   std::set<uint32_t> indices;
   for (std::byte* hit : cbuffer_operand_hits)
   {
      size_t hit_offset = static_cast<size_t>(hit - code);
      if (hit_offset + 8 > size)
         continue; // out of bounds
      const uint32_t* hit_token = reinterpret_cast<const uint32_t*>(hit);
      uint32_t        index     = hit_token[2];
      indices.insert(index);
   }

   if (indices.size() < 4)
      return false; // not enough unique indices

   // copy to array and look for 4 consecutive indices
   std::vector<uint32_t> index_array(indices.begin(), indices.end());
   // std::sort(index_array.code_u32(), index_array.end()); // should already be sorted in a set

   // we need to read to find all potential candidates for PrevClipToClip matrix
   // so we look for 4 consecutive indices
   // if there are multiple candidates we pick the one with the highest average index (likely to be near the end of the cbuffer)

   // loop backwards
   int32_t best_start = UINT32_MAX;
   for (size_t i = index_array.size() - 1; i - 3 >= 0; i--)
   {
      if (index_array[i] - index_array[i - 3] == 3)
      {
         best_start = static_cast<int32_t>(index_array[i - 3]);
         break;
      }
   }

   taa_shader_info.clip_to_prev_clip_start_index = best_start;
   taa_shader_info.global_buffer_register_index   = cbuffer_operand_register.u;
   taa_shader_info.declared_cbuffer_size          = max_cbuffer_declaration[3];
   
   return true;
}

static void FindJitterFromMVWrite(const std::byte* code, size_t size, GlobalCBInfo& global_cb_info)
{
   // When materials are written to the velocity texture, the jitter is usually removed before being written
   // We can look for the Encode Velocity to Texture operations (the inverse of Decode Velocity from Texture found in TAA shader)
   // usually looks like:
   // float2 EncodeVelocityToTexture(float2 In)
   // {
   //      // 0.499f is a value smaller than 0.5f to avoid using the full range to use the clear color (0,0) as special value
   //      // 0.5f to allow for a range of -2..2 instead of -1..1 for really fast motions for temporal AA.
   //      // Texure is R16G16 UNORM
   //      return In * (0.499f * 0.5f) + (32767.0f / 65535.0f);
   // }
   // the strategy will be the similar as for FindGlobalCBInfo: scan for immediate operands matching the constants used,
   // this will be the first part to determine if it is a shader that writes motion vectors
   // then look backwards for cbuffer load operands and extract the index used (jitter vector)
   // we can use heuristics to find the closest cbuffer load before the encode velocity operation
   // this is the pattern we are looking for:
   // add r0.xyzw, r0.xyzw, -cb0[122].xyzw
   // add r0.xy, -r0.zwzz, r0.xyxx
   // mad o0.xy, r0.xyxx, l(0.249500, 0.249500, 0.000000, 0.000000), l(0.499992, 0.499992, 0.000000, 0.000000)
   // mov o0.zw, l(0,0,0,0)
   // ret
   // we should determine the cbuffer register first, jitter should be in the global cbuffer which we can assume is the largest cbuffer
   // we can find the largest cbuffer by scanning for dcl_constant_buffer instructions

   // first scan for encode velocity immediate operands pattern
   word_t mul_1;
   mul_1.f = 0.249500006f;
   word_t mul_2;
   mul_2.f                                              = 0.f;
   std::vector<std::byte> encode_velocity_pattern_bytes = {
      std::byte{mul_1.b[0]}, std::byte{mul_1.b[1]}, std::byte{mul_1.b[2]}, std::byte{mul_1.b[3]},
      std::byte{mul_1.b[0]}, std::byte{mul_1.b[1]}, std::byte{mul_1.b[2]}, std::byte{mul_1.b[3]}};
   std::vector<std::byte*> encode_velocity_hits = System::ScanMemoryForPattern(code, size, encode_velocity_pattern_bytes);
   if (encode_velocity_hits.empty())
      return; // no hits found
   if (encode_velocity_hits.size() > 1)
      return; // too many hits, likely false positives

   // now find the largest cbuffer declaration to determine the register index to match
   const uint32_t* code_u32                = reinterpret_cast<const uint32_t*>(code);
   const uint32_t  size_u32                = size / sizeof(uint32_t);
   uint32_t*       max_cbuffer_declaration = FindLargestCBufferDeclaration(code_u32, size_u32);

   if (max_cbuffer_declaration == nullptr)
      return;

   uint32_t cbuffer_register_index = max_cbuffer_declaration[2];

   size_t offset = static_cast<size_t>(encode_velocity_hits[0] - code) / sizeof(uint32_t);
   offset--;
   while (offset > 0)
   {
      uint32_t token = code_u32[offset];
      if (token == cbuffer_register_index)
      {
         if (offset - 1 >= 0)
         {
            uint32_t operand_token   = code_u32[offset - 1];
            bool     is_cbuffer_load = DECODE_D3D10_SB_OPERAND_TYPE(operand_token) == D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER && DECODE_D3D10_SB_OPERAND_INDEX_DIMENSION(operand_token) == D3D10_SB_OPERAND_INDEX_2D && DECODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, operand_token) == D3D10_SB_OPERAND_INDEX_IMMEDIATE32 && DECODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(1, operand_token) == D3D10_SB_OPERAND_INDEX_IMMEDIATE32;

            if (is_cbuffer_load)
            {
               // found cbuffer load before encode velocity
               uint32_t index              = code_u32[offset + 1];
               global_cb_info.jitter_index = static_cast<int32_t>(index);
               return;
            }
         }
      }
      offset--;
   }
}