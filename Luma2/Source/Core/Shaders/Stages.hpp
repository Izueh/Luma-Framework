#pragma once

#include <bitset>
#include <cstddef>
#include <initializer_list>

#include "Common/ShaderStage.hpp"

namespace Luma {
namespace Shaders {
inline constexpr size_t STAGE_COUNT = 6;

// A set of stages: the ones a call uses, where a variant runs, where Luma bound its buffers, which stages a state group covers.
// `bits` is in ShaderStage order: use the functions.
struct StageSet
{
   std::bitset<STAGE_COUNT> bits;

   constexpr StageSet() = default;

   constexpr StageSet(std::initializer_list<ShaderStage> stages)
   {
      for (const ShaderStage stage : stages)
         bits.set(size_t(stage));
   }

   constexpr bool Has(ShaderStage stage) const { return bits.test(size_t(stage)); }

   constexpr StageSet& Set(ShaderStage stage, bool present = true)
   {
      bits.set(size_t(stage), present);
      return *this;
   }

   constexpr StageSet& Remove(ShaderStage stage)
   {
      bits.reset(size_t(stage));
      return *this;
   }

   constexpr bool Any() const { return bits.any(); }

   constexpr StageSet& operator|=(const StageSet& other)
   {
      bits |= other.bits;
      return *this;
   }

   friend constexpr StageSet operator&(StageSet a, const StageSet& b)
   {
      a.bits &= b.bits;
      return a;
   }

   friend constexpr StageSet operator|(StageSet a, const StageSet& b)
   {
      a |= b;
      return a;
   }

   friend constexpr bool operator==(const StageSet&, const StageSet&) = default;
};

// What a draw and a dispatch use
inline constexpr StageSet GRAPHICS_STAGES = {ShaderStage::Vertex, ShaderStage::Hull, ShaderStage::Domain, ShaderStage::Geometry, ShaderStage::Pixel};
inline constexpr StageSet COMPUTE_STAGES = {ShaderStage::Compute};
} // namespace Shaders
} // namespace Luma
