#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <d3d11.h>

#include "../hooks/scope.hpp"
#include "./shader_types.hpp"

namespace Shader
{
   // A game shader object by its pointer (on D3D11 a pipeline is a single shader)
   struct Pipeline
   {
      uint64_t handle = 0;

      bool operator==(const Pipeline&) const = default;
   };

   struct ShaderDesc
   {
      const void* code = nullptr;
      size_t code_size = 0;
   };

   // A shader's stage and bytecode
   struct PipelineSubobject
   {
      D3D11_SHADER_VERSION_TYPE type = SHADER_VERSION_TYPE_UNKNOWN;
      uint32_t count = 0;
      void* data = nullptr; // A ShaderDesc
   };

   inline bool IsShaderType(D3D11_SHADER_VERSION_TYPE type)
   {
      switch (type)
      {
      case D3D11_SHVER_VERTEX_SHADER:
      case D3D11_SHVER_HULL_SHADER:
      case D3D11_SHVER_DOMAIN_SHADER:
      case D3D11_SHVER_GEOMETRY_SHADER:
      case D3D11_SHVER_PIXEL_SHADER:
      case D3D11_SHVER_COMPUTE_SHADER:
         return true;
      default:
         return false;
      }
   }

   inline PipelineSubobject* ClonePipelineSubobjects(uint32_t subobject_count, const PipelineSubobject* subobjects)
   {
      auto* new_subobjects = new PipelineSubobject[subobject_count];
      memcpy(new_subobjects, subobjects, sizeof(PipelineSubobject) * subobject_count);
      for (uint32_t i = 0; i < subobject_count; ++i)
      {
         const auto& subobject = subobjects[i];
         if (!IsShaderType(subobject.type) || !subobject.data)
         {
            new_subobjects[i].data = nullptr;
            continue;
         }
         auto* old_desc = static_cast<const ShaderDesc*>(subobject.data);
         auto* new_desc = static_cast<ShaderDesc*>(malloc(sizeof(ShaderDesc)));
         *new_desc = *old_desc;
         if (old_desc->code_size != 0u)
         {
            void* code_copy = malloc(old_desc->code_size);
            memcpy(code_copy, old_desc->code, old_desc->code_size);
            new_desc->code = code_copy;
         }
         new_subobjects[i].data = new_desc;
      }
      return new_subobjects;
   }

   inline void DestroyPipelineSubojects(PipelineSubobject* subojects, uint32_t subobject_count)
   {
      for (uint32_t i = 0; i < subobject_count; ++i)
      {
         auto& suboject = subojects[i];
         if (IsShaderType(suboject.type) && suboject.data)
         {
            auto* desc = static_cast<ShaderDesc*>(suboject.data);
            free(const_cast<void*>(desc->code));
            desc->code = nullptr;
         }
         free(suboject.data);
         suboject.data = nullptr;
      }
      delete[] subojects;
   }

   // Creates the shader of the (single) shader subobject. Never seen by Luma's own hooks.
   inline bool CreatePipeline(ID3D11Device* device, uint32_t subobject_count, const PipelineSubobject* subobjects, Pipeline* pipeline)
   {
      *pipeline = {};
      for (uint32_t i = 0; i < subobject_count; ++i)
      {
         const PipelineSubobject& subobject = subobjects[i];
         if (!IsShaderType(subobject.type) || !subobject.data)
            continue;
         const auto* desc = static_cast<const ShaderDesc*>(subobject.data);
         Luma::Hooks::Scope::Guard scope;
         HRESULT result = E_INVALIDARG;
         ID3D11DeviceChild* shader = nullptr;
         switch (subobject.type)
         {
         case D3D11_SHVER_VERTEX_SHADER: result = device->CreateVertexShader(desc->code, desc->code_size, nullptr, reinterpret_cast<ID3D11VertexShader**>(&shader)); break;
         case D3D11_SHVER_HULL_SHADER: result = device->CreateHullShader(desc->code, desc->code_size, nullptr, reinterpret_cast<ID3D11HullShader**>(&shader)); break;
         case D3D11_SHVER_DOMAIN_SHADER: result = device->CreateDomainShader(desc->code, desc->code_size, nullptr, reinterpret_cast<ID3D11DomainShader**>(&shader)); break;
         case D3D11_SHVER_GEOMETRY_SHADER: result = device->CreateGeometryShader(desc->code, desc->code_size, nullptr, reinterpret_cast<ID3D11GeometryShader**>(&shader)); break;
         case D3D11_SHVER_PIXEL_SHADER: result = device->CreatePixelShader(desc->code, desc->code_size, nullptr, reinterpret_cast<ID3D11PixelShader**>(&shader)); break;
         case D3D11_SHVER_COMPUTE_SHADER: result = device->CreateComputeShader(desc->code, desc->code_size, nullptr, reinterpret_cast<ID3D11ComputeShader**>(&shader)); break;
         default: break;
         }
         if (FAILED(result) || !shader)
            return false;
         pipeline->handle = reinterpret_cast<uint64_t>(shader);
         return true;
      }
      return false;
   }

   inline void DestroyPipeline(Pipeline pipeline)
   {
      if (!pipeline.handle)
         return;
      Luma::Hooks::Scope::Guard scope;
      reinterpret_cast<IUnknown*>(pipeline.handle)->Release();
   }
}
