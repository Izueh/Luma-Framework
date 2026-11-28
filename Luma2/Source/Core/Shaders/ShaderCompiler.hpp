#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <guiddef.h>

#include "Shaders/Shaders.hpp"

// FXC (D3DCompiler_47.dll) for DXBC targets ("ps_5_0", "ps_5_1") and DXC (dxcompiler.dll + dxil.dll) for DXIL targets
// ("ps_6_0".."lib_6_8"), both loaded on first use. Files and their #include lines are named from the Shaders root
// ("Includes/Color/Color.hlsl") and found through `roots`, the first holding them winning. FXC is used by one thread at a
// time (it isn't thread safe): each FXC call takes the Lock for its duration.
namespace Luma {
namespace Shaders {
namespace Compiler {
// Held while FXC, or an interface it returned (a reflector), is in use. Recursive: a caller that uses the reflector `Reflect`
// gave it holds one across that use, and releases the reflector before it ends. Nothing else is locked while it's held.
class Lock
{
public:
   Lock();
   ~Lock();
   Lock(const Lock&) = delete;
   Lock& operator=(const Lock&) = delete;
};

struct DxcStatus
{
   bool loaded = false;
   bool signer_loaded = false;
   std::string path;
   uint64_t version = 0;
};

struct Define
{
   std::string name;
   std::string value;

   friend bool operator==(const Define&, const Define&) = default;
};

struct Preprocessed
{
   std::string text;
   std::string messages;
   bool failed = false;
};

struct Compiled
{
   Bytecode bytecode;
   // Errors, or warnings when it succeeded
   std::string messages;
   bool failed = false;
};

bool IsDxilTarget(std::string_view target);
bool IsSignedDxil(std::span<const uint8_t> bytecode);
// Validates and signs a DXIL container with dxil.dll (IDxcValidator)
std::expected<Bytecode, std::string> SignDxil(std::span<const uint8_t> bytecode);
DxcStatus GetDxcStatus();

Preprocessed Preprocess(std::span<const std::filesystem::path> roots, std::string_view file, std::span<const Define> defines, std::string_view target = {});
Compiled Compile(std::span<const std::filesystem::path> roots, std::string_view file, std::span<const Define> defines, std::string_view target,
   std::string_view entry);

// D3DReflect: `iid` is the API's reflection interface. False if FXC isn't there or the bytecode has no reflection data.
bool Reflect(std::span<const uint8_t> bytecode, REFIID iid, void** reflector);
// IDxcUtils::CreateReflection for DXIL bytecode (`ID3D12ShaderReflection`)
bool ReflectDxil(std::span<const uint8_t> bytecode, REFIID iid, void** reflector);
std::optional<std::string> Disassemble(std::span<const uint8_t> bytecode);

struct Binding
{
   std::string name;
   uint32_t slot = 0;
   uint32_t space = 0;
};

// The cbuffers DXBC or DXIL bytecode declares (the ones it reads: the compiler drops the others), by name. Empty when stripped.
std::vector<Binding> ReflectConstantBufferBindings(std::span<const uint8_t> bytecode);
// Its textures (shader resource views of a texture): name, register and space
std::vector<Binding> ReflectTextureBindings(std::span<const uint8_t> bytecode);

#if DEVELOPMENT
// A cbuffer's layout, to show its contents typed
struct Variable
{
   enum class Kind : uint8_t
   {
      Float,
      Int,
      Uint,
      Bool,
      Other, // Shown as raw words
   };

   std::string name; // Struct members flattened ("a.b")
   std::string type; // "float4"
   uint32_t offset = 0;
   uint32_t size = 0;
   Kind kind = Kind::Other;
   uint32_t rows = 1;
   uint32_t columns = 1;
   uint32_t elements = 0; // Of an array, 0 otherwise
   bool column_major = false; // A matrix stored by columns, each in its own register
};

struct ConstantBuffer
{
   std::string name;
   uint32_t slot = 0;
   uint32_t size = 0;
   std::vector<Variable> variables;
};

// The cbuffers of DXBC bytecode, from its reflection data: empty when it was stripped
std::vector<ConstantBuffer> ReflectConstantBuffers(std::span<const uint8_t> bytecode);
#endif
} // namespace Compiler
} // namespace Shaders
} // namespace Luma
