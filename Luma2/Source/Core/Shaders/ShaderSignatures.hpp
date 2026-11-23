#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Common/ShaderStage.hpp"
#include "Shaders/ShaderContainer.hpp"

// Whether a replacement links with the stages built for its original. The compiler sees one shader at a time, so one that
// moves an element to another register compiles fine: D3D12 refuses the pipelines with it, D3D11 reads the wrong data.
namespace Luma {
namespace Shaders {
namespace Signatures {
// What the stages around the original would see differently in the replacement, one line each. Empty if it fits.
std::vector<std::string> Compare(ShaderStage stage, const Container::Signature& original, const Container::Signature& replacement);
#if DEVELOPMENT
// Any thread: logs the differences as an error and asserts, once per replacement bytecode. Development builds only: the
// replacements a mod ships were checked while it was made, Publishing builds don't pay for it.
void Check(uint32_t hash, ShaderStage stage, const Container::Signature& original, std::span<const uint8_t> replacement, std::string_view file);
#endif
} // namespace Signatures
} // namespace Shaders
} // namespace Luma
