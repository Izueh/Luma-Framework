#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "Shaders/ShaderContainer.hpp"
#include "Shaders/Shaders.hpp"

#if DEVELOPMENT || TEST
namespace Luma {
namespace Shaders {
namespace Invariants {
struct D3D12Rules
{
   uint32_t space = 0;
   uint32_t settings_slot = 0;
   uint32_t data_slot = 0;
   bool reads_luma = false;
   bool static_samplers = false;
   std::span<const uint32_t> texture_slots;
   const Container::Bindings* original = nullptr; // Null skips space 0 comparison
};

struct Findings
{
   std::string errors; // Non-empty rejects the variant
   std::string warnings; // Extra space 0 registers; game's root signature may still bind them
};

Findings CheckD3D12(std::span<const uint8_t> bytecode, const D3D12Rules& rules);
// Any thread; returns errors (empty if valid or not D3D12)
std::string CheckVariant(uint32_t hash, std::string_view source, const CompiledShader& compiled);
uint32_t GetRejectedCount();
} // namespace Invariants
} // namespace Shaders
} // namespace Luma
#endif
