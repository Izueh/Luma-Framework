#pragma once

#include <array>
#include <cstdint>
#include <span>

#include <d3d11.h>

#include "Common/Api.hpp"
#include "Hooks/Functions.hpp"

namespace Luma {
namespace Game {
enum class DrawResult : uint8_t
{
   None, // The game's call runs, with what the callback changed
   Skip, // The game's call doesn't run
   Replaced, // The callback drew in its place (RunOriginal draws the game's call with other bindings): the game's call doesn't run
};

namespace D3D11 {
// The arguments `function` doesn't take are zero
struct DrawCall
{
   Hooks::D3D11::Function function;
   UINT vertex_or_index_count = 0;
   UINT instance_count = 0;
   UINT start_vertex_or_index = 0;
   INT base_vertex = 0;
   UINT start_instance = 0;
   ID3D11Buffer* arguments = nullptr; // Indirect draws
   UINT arguments_offset = 0;
};

struct DispatchCall
{
   UINT x = 0;
   UINT y = 0;
   UINT z = 0;
   ID3D11Buffer* arguments = nullptr; // DispatchIndirect
   UINT arguments_offset = 0;
};

struct Call
{
   ID3D11DeviceContext* context = nullptr;
   const DrawCall* draw = nullptr; // Null for a dispatch
   const DispatchCall* dispatch = nullptr; // Null for a draw
   // The game's shader bound per stage (ShaderStage order), 0 where none is or for one Luma didn't see created
   std::array<uint32_t, 6> hashes = {};
   // Draw::GetStageBit bits
   uint8_t variant_stages = 0;
};

using DrawCallback = DrawResult (*)(const Call& call);

// Matched on any stage, one callback per hash. Its own calls aren't hooked or traced, so it restores what later draws would see.
// Luma binds the variants' requests after the callbacks, only when they all return None (1.0).
void OnDraw(std::span<const uint32_t> hashes, DrawCallback callback);
// Runs first: the other callbacks run only if it returns None
void OnVariantDraw(DrawCallback callback);
// Draws and dispatches with no callback by hash
void OnDrawOrDispatch(DrawCallback callback);

// With the bindings now on the context
void RunOriginal(const Call& call);
// Until the game binds another shader on `stage`. Nothing on a stage without a variant.
void UseOriginal(const Call& call, ShaderStage stage);
// With the resources the shader requests. From a draw callback, Core then binds no variant's requests for that call.
void BindLumaShader(ID3D11DeviceContext* context, uint64_t name_hash, const LumaCustomData& custom = {});
} // namespace D3D11
} // namespace Game
} // namespace Luma
