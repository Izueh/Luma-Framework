#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

#include "Common/ShaderStage.hpp"
#include "Shaders/Shaders.hpp"

// Luma's shader objects on D3D11 (variants, Luma's shaders in Resources::Database), built from published compiled shaders, read
// lock-free at any bind, replaced at the present boundary.
namespace Luma {
namespace Shaders {
namespace D3D11 {
// Inside a Luma scope. Null if the device refuses the bytecode.
Microsoft::WRL::ComPtr<ID3D11DeviceChild> CreateShader(ID3D11Device* device, ShaderStage stage, std::span<const uint8_t> bytecode);
// `shader` (null unbinds) on the stage, through the stage's SetShader
void Bind(ID3D11DeviceContext* context, ShaderStage stage, ID3D11DeviceChild* shader);

// What a bind reads: the object and the compiled shader it was made from (its requests), replaced together
struct Live
{
   Microsoft::WRL::ComPtr<ID3D11DeviceChild> object;
   std::shared_ptr<const CompiledShader> compiled;
};

struct Published
{
   std::atomic<const Live*> current = nullptr;
   std::unique_ptr<Live> live;
   // What was published last, made into `live` or refused by the device
   std::shared_ptr<const CompiledShader> compiled;
};

// What was replaced while a bind on another thread may still be passing it to the API, which then keeps it alive itself.
// Released two presents after its replacement.
struct Retired
{
   std::vector<std::pair<std::unique_ptr<Live>, uint64_t>> lives;
};

// Under the owner's lock: publishes the object of `compiled` (null for none). Nothing is created for the bytecode the current
// object was built from. False, with nothing published, if the device refused it.
bool Publish(ID3D11Device* device, ShaderStage stage, Published& published, std::shared_ptr<const CompiledShader> compiled, Retired& retired);
// Under the owner's lock, at a present
void ReleaseRetired(Retired& retired);
// Under the owner's lock: the device is released
void Release(Published& published);
} // namespace D3D11
} // namespace Shaders
} // namespace Luma
