#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <shared_mutex>
#include <streambuf>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "shaders.h"

#include "dxp/RecipeReport.hpp"

namespace Patch
{

   struct DxpPatchedShaderData
   {
      std::vector<uint8_t> code;
      dxp::RecipeReport report;
   };

   struct PatchContext
   {
      std::unordered_map<uint32_t, std::shared_ptr<DxpPatchedShaderData>> dxp_patched_shaders;
      std::unordered_set<uint32_t> dxp_processed_shaders;
      mutable std::shared_mutex mutex;

      bool HasPatch(uint32_t shader_hash) const
      {
         const std::shared_lock lock(mutex);
         return dxp_patched_shaders.contains(shader_hash);
      }

      bool IsProcessed(uint32_t shader_hash) const
      {
         const std::shared_lock lock(mutex);
         return dxp_processed_shaders.contains(shader_hash);
      }

      bool SetProcessed(uint32_t shader_hash)
      {
         const std::unique_lock lock(mutex);
         return dxp_processed_shaders.emplace(shader_hash).second;
      }

      std::shared_ptr<DxpPatchedShaderData> GetShaderData(uint32_t shader_hash) const
      {
         const std::shared_lock lock(mutex);
         auto it = dxp_patched_shaders.find(shader_hash);
         if (it != dxp_patched_shaders.end() && it->second && !it->second->code.empty())
         {
            return it->second; // Increments ref-count safely inside the lock
         }
         return nullptr;
      }

      void StorePatched(uint32_t shader_hash, std::vector<uint8_t>&& code, dxp::RecipeReport&& report)
      {
         // Create the fully populated object first to minimize time spent inside the unique_lock
         auto new_shader_data = std::make_shared<DxpPatchedShaderData>(DxpPatchedShaderData{
            .code = std::move(code),
            .report = std::move(report)
         });

         const std::unique_lock lock(mutex);
         // Overwrites or inserts in a single, fast operation
         dxp_patched_shaders.insert_or_assign(shader_hash, std::move(new_shader_data));
      }

      template <typename TVisitor>
      void ForEachPatchedShader(TVisitor&& visitor) const
      {
         const std::shared_lock lock(mutex);
         for (const auto& [shader_hash, patched_shader] : dxp_patched_shaders)
         {
            if (!patched_shader || patched_shader->code.empty())
            {
                  continue;
            }

            std::forward<TVisitor>(visitor)(shader_hash, *patched_shader);
         }
      }
   };

} // namespace Patch
