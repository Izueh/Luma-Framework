#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Common/ShaderStage.hpp"

// Reading compiled shaders without the compiler or reflection: the container ("DXBC", for SM6's DXIL too) is a header and a
// list of chunks, each a four character code, a size and its contents. Any thread, no allocation but the results.
namespace Luma {
namespace Shaders {
namespace Container {
// The chunk of this four character code ("SHEX", "RTS0", "DXIL"...), empty if the container doesn't have it or isn't one
std::span<const uint8_t> GetChunk(std::span<const uint8_t> bytecode, std::string_view fourcc);
bool HasChunk(std::span<const uint8_t> bytecode, std::string_view fourcc);

struct Program
{
   uint32_t kind = 0; // D3D12_SHADER_VERSION_TYPE: 0 pixel, 1 vertex, 2 geometry, 3 hull, 4 domain, 5 compute, 6+ libraries, mesh...
   uint32_t major = 0;
   uint32_t minor = 0;
   bool dxil = false;
};

// From the program chunk: SHEX (SM5), SHDR (SM4) or DXIL (SM6)
std::optional<Program> GetProgram(std::span<const uint8_t> bytecode);
// "ps_5_0", "vs_6_6", "lib_6_3"; empty if there's no program chunk or the kind is unknown
std::string GetTarget(std::span<const uint8_t> bytecode);
// The pipeline stage of a program kind, none for libraries, mesh and amplification shaders
std::optional<ShaderStage> GetStage(const Program& program);

// SM4/SM5 programs (SHEX or SHDR): the instructions after the version and length tokens, what a game's bytecode patch gets.
// Empty without one.
std::span<const uint8_t> GetProgramCode(std::span<const uint8_t> bytecode);
// The container with its program's instructions replaced by `code` (a multiple of 4 bytes): chunk sizes and offsets moved, and
// the checksum computed again, or the API refuses it. Empty if the container has no SM4/SM5 program.
std::vector<uint8_t> ReplaceProgramCode(std::span<const uint8_t> bytecode, std::span<const uint8_t> code);
// The container's checksum (DXBC's own MD5 variant) written in its header
void Sign(std::span<uint8_t> bytecode);

// The root signature compiled into the shader (an "RTS0" chunk, D3D12), empty without one
std::span<const uint8_t> GetRootSignature(std::span<const uint8_t> bytecode);

// An element of an input, output or patch constant signature
struct Element
{
   std::string semantic; // Upper case: the compiler keeps the source's case, linkage ignores it
   uint32_t index = 0;
   uint32_t stream = 0; // Geometry shader outputs
   uint32_t system_value = 0; // D3D_NAME
   uint32_t component_type = 0; // D3D_REGISTER_COMPONENT_TYPE
   uint32_t reg = 0; // UINT32_MAX for depth, coverage and stencil reference outputs
   uint8_t mask = 0; // The components declared
   uint8_t used = 0; // Inputs: the components read. Outputs: the ones never written.
   uint32_t min_precision = 0; // D3D_MIN_PRECISION, SM5.1 and later
};

struct Signature
{
   std::vector<Element> inputs; // ISG1 or ISGN
   std::vector<Element> outputs; // OSG1, OSG5 or OSGN
   std::vector<Element> patch_constants; // PSG1 or PCSG: hull shader outputs, domain shader inputs
};

// Each list empty if the shader has no such chunk (compute shaders, a pixel shader writing nothing)
Signature GetSignature(std::span<const uint8_t> bytecode);
// "TEXCOORD0", "SV_POSITION0"
std::string GetName(const Element& element);

enum class BindingType : uint8_t
{
   ConstantBuffer,
   ShaderResource,
   UnorderedAccess,
   Sampler,
};

// Registers the shader declares: one each in SM5.0 and earlier (space 0), a range in a space in SM5.1 and SM6
struct Binding
{
   BindingType type = BindingType::ConstantBuffer;
   uint32_t space = 0;
   uint32_t base = 0;
   uint32_t count = 1; // UINT32_MAX for an unbounded array

   bool Covers(BindingType type_, uint32_t space_, uint32_t reg) const;
};

struct Bindings
{
   std::vector<Binding> bindings;
   // The declarations were read: from the program (SM4, SM5) or the pipeline state validation chunk (SM6). Otherwise nothing is
   // known, not even that the shader binds nothing.
   bool found = false;
   // SM6.6 bindless: ResourceDescriptorHeap[] and SamplerDescriptorHeap[], which declare no register
   bool resource_heap_indexed = false;
   bool sampler_heap_indexed = false;

   bool Declares(BindingType type, uint32_t space, uint32_t reg) const;
   bool UsesSpace(uint32_t space) const;
   bool HasUnbounded() const;
};

// Without reflection: the declarations in SHEX or SHDR, PSV0's resource list for DXIL, SFI0's feature flags for heap indexing
Bindings GetBindings(std::span<const uint8_t> bytecode);
} // namespace Container
} // namespace Shaders
} // namespace Luma
