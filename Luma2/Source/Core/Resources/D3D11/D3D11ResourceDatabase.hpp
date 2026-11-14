#pragma once

#include <cstdint>

#include <d3d11.h>

#include "Common/ShaderStage.hpp"
#include "Resources/ResourceDatabase.hpp"
#include "Shaders/D3D11/D3D11Shaders.hpp"
#include "Shaders/Shaders.hpp"

// Resources::Database's objects on each D3D11 device, owned by the device's state and released when the game releases the
// device. Binding them is the "fulfil" step: a compiled shader's requests are bound where it runs.
namespace Luma {
namespace Resources {
namespace Database {
namespace D3D11 {
// An object for every entry: buffers, and the shaders published so far
void CreateDeviceObjects(ID3D11Device* device);
// Presenting thread, at the device's present: objects for the shaders published since, replaced ones released when unused
void UpdateShaderObjects(ID3D11Device* device);
// The game released the device: every object is released
void ReleaseDeviceObjects(ID3D11Device* device);

// Any thread: a shader entry's object and compiled shader, null until it compiled. Valid until the second present after a reload
// replaces it: for a pass, not to keep.
const Shaders::D3D11::Live* GetShader(ID3D11Device* device, uint64_t name_hash);

// The requests of `compiled`, a shader running on `stage` of the context: each entry uploaded where its contents changed for this context
// (LumaData with `custom`), and bound at the register it declared. Nothing while Luma is off. Whether anything was bound.
bool BindRequested(ID3D11DeviceContext* context, ShaderStage stage, const Shaders::CompiledShader& compiled, const CustomData& custom);
// What the context's buffers hold is unknown after a command list boundary (a deferred context's next list starts empty, an
// executed list may have uploaded its own): they're uploaded again
void ForgetUploads(ID3D11DeviceContext* context);
} // namespace D3D11
} // namespace Database
} // namespace Resources
} // namespace Luma
