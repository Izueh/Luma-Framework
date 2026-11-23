#include "Shaders/D3D11/D3D11Database.hpp"

#include "Hooks/ObjectLifetime.hpp"

namespace Luma {
namespace Shaders {
namespace Database {
namespace D3D11 {
namespace {
// In the private data of each of the game's shader objects
struct Object
{
   Shader* shader;

   explicit Object(Shader* shader_) : shader(shader_) { shader->objects++; }

   ~Object() { shader->objects--; }

   Object(const Object&) = delete;
   Object& operator=(const Object&) = delete;
};
} // namespace

uint32_t TrackShader(ShaderStage stage, std::span<const uint8_t> bytecode, ID3D11DeviceChild* object)
{
   Shader& shader = Track(stage, bytecode);
   Hooks::Lifetime::SetState<Object>(object, &shader);
   return shader.hash;
}

const Shader* FindShader(ID3D11DeviceChild* object)
{
   const Object* state = Hooks::Lifetime::GetState<Object>(object);
   return state ? state->shader : nullptr;
}
} // namespace D3D11
} // namespace Database
} // namespace Shaders
} // namespace Luma
