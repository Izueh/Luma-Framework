#pragma once

#include <array>
#include <cstdint>
#include <span>

#include <d3d11.h>
#include <wrl/client.h>

#include "Common/ShaderStage.hpp"
#include "Shaders/Database.hpp"
#include "Shaders/Shaders.hpp"
#include "Shaders/Stages.hpp"
#include "Shaders/Variants.hpp"

// The variant objects of each replaced game shader hash and device, one per source, swapped in at bind on every stage. Owned by
// the device's state and released with the device; objects of the same hash share them.
namespace Luma {
namespace Shaders {
namespace Variants {
namespace D3D11 {
// What the game bound per stage on a context, recorded by the one shader bind handler: the shader group of the bound-state cache,
// what draw callbacks are found by, and what the variants running request
struct BoundShaders
{
   // Null for no shader, or one created before Luma tracked it
   std::array<const Database::Shader*, 6> shaders = {};
   // Where a variant runs: the game's shader it stands for (UseOriginal binds it back), its compiled shader (its requests) and source
   std::array<Microsoft::WRL::ComPtr<ID3D11DeviceChild>, 6> originals;
   std::array<const CompiledShader*, 6> compiled = {};
   std::array<Source, 6> sources = {};
   StageSet variant_stages;
   StageSet callback_stages; // The shader has a draw callback
};

// The device's variants, empty until its shaders are created
void CreateVariantStore(ID3D11Device* device);
// After Database::D3D11::TrackShader: the object finds its hash's variants at bind. A geometry shader made with stream output is
// never replaced (`stream_output`).
void AttachVariant(ID3D11Device* device, ID3D11DeviceChild* object, bool stream_output);
// The game binds `shader` (null unbinds): its variant goes in its place while Luma is enabled and the variants are loaded, and
// the context's record follows
void SwapInVariant(ID3D11DeviceContext* context, ShaderStage stage, ID3D11DeviceChild*& shader);
// The game's shader back on `stage` in place of its variant, until the game binds another (1.0's UsePipelineVariant(Original))
void UseOriginal(ID3D11DeviceContext* context, ShaderStage stage);
// The context's state went back to the defaults (ClearState, or a command list finished or executed without restoring it)
void ForgetBoundShaders(ID3D11DeviceContext* context);
// The context's record, null for a context without private data
const BoundShaders* GetBoundShaders(ID3D11DeviceContext* context);
// Before a draw or dispatch's events (1.0's OnDrawOrDispatchCustomPipeline): the resources the patches running on `stages` request.
// A patch whose resource the device doesn't have (a texture that didn't load) gives way to the game's shader for this call. The
// stages where something was bound.
StageSet BindPatchResources(ID3D11DeviceContext* context, StageSet stages);
// Before a draw or dispatch: the resources the variants running on `stages` request, bound where they declared them. The stages
// where something was bound.
StageSet BindRequestedResources(ID3D11DeviceContext* context, StageSet stages);

// Presenting thread, at the device's present: objects for what was published, replaced ones released when unused
void PublishVariants(ID3D11Device* device);
// The game released the device: every variant object is released
void ReleaseVariants(ID3D11Device* device);
} // namespace D3D11
} // namespace Variants
} // namespace Shaders
} // namespace Luma
