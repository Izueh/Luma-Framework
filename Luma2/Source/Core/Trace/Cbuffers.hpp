#pragma once

#if DEVELOPMENT
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// A constant buffer's contents as typed fields, for the trace detail pane and luma_read_cbuffer
namespace Luma {
namespace Trace {
namespace Cbuffers {
struct Field
{
   std::string name;
   std::string type;
   uint32_t offset = 0;
   uint32_t size = 0; // Bytes
   std::string value;
};

struct Description
{
   // Where the layout came from: "reflection" (the game's shader), "replacement" (Luma's replacement of that hash declares the
   // cbuffer), "raw" (neither: float4 rows)
   std::string source;
   std::string name; // The cbuffer's, empty for raw rows
   std::vector<Field> fields;
};

// `data` bound at `slot` of the shader `hash`
Description Describe(uint32_t hash, uint32_t slot, std::span<const std::byte> data);
} // namespace Cbuffers
} // namespace Trace
} // namespace Luma
#endif
