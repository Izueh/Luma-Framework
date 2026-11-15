#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <DDSTextureLoader11.h>

#include "Shaders/Container.hpp"
#include "Shaders/Variants.hpp"
#include "System/Memory.hpp"

using namespace Luma;
using Microsoft::WRL::ComPtr;

namespace {
std::vector<uint8_t> Compile(std::string_view source, const char* target)
{
   ComPtr<ID3DBlob> blob;
   REQUIRE(SUCCEEDED(D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, "main", target, 0, 0, &blob, nullptr)));
   const auto* bytes = static_cast<const uint8_t*>(blob->GetBufferPointer());
   return {bytes, bytes + blob->GetBufferSize()};
}

ComPtr<ID3D11Device> CreateWarpDevice()
{
   ComPtr<ID3D11Device> device;
   REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr)));
   return device;
}
} // namespace

TEST_CASE("Patching: a container signed again has FXC's checksum")
{
   std::vector<uint8_t> bytecode = Compile("float4 main(float4 color : COLOR) : SV_Target { return color * 2; }", "ps_5_0");
   const std::vector<uint8_t> original = bytecode;
   std::memset(bytecode.data() + 4, 0, 16);
   Shaders::Container::Sign(bytecode);
   CHECK(bytecode == original);
}

TEST_CASE("Patching: a program's instructions replaced, and the device takes the container")
{
   const std::vector<uint8_t> bytecode = Compile("float4 main(float4 color : COLOR) : SV_Target { return color * 2; }", "ps_5_0");
   const std::span<const uint8_t> code = Shaders::Container::GetProgramCode(bytecode);
   REQUIRE(!code.empty());
   CHECK(code.size() % 4 == 0);
   // The same instructions give the same container
   CHECK(Shaders::Container::ReplaceProgramCode(bytecode, code) == bytecode);

   // A second "ret" (D3D10_SB_OPCODE_RET, one token) after the program's: chunk sizes, offsets and the checksum follow
   std::vector<uint8_t> longer(code.begin(), code.end());
   constexpr uint32_t RET = 0x0100003E;
   longer.insert(longer.end(), reinterpret_cast<const uint8_t*>(&RET), reinterpret_cast<const uint8_t*>(&RET) + 4);
   const std::vector<uint8_t> patched = Shaders::Container::ReplaceProgramCode(bytecode, longer);
   REQUIRE(patched.size() == bytecode.size() + 4);
   CHECK(Shaders::Container::GetProgramCode(patched).size() == code.size() + 4);
   CHECK(Shaders::Container::GetTarget(patched) == "ps_5_0");
   const ComPtr<ID3D11Device> device = CreateWarpDevice();
   ComPtr<ID3D11PixelShader> shader;
   CHECK(SUCCEEDED(device->CreatePixelShader(patched.data(), patched.size(), nullptr, &shader)));

   // Not a whole number of tokens
   CHECK(Shaders::Container::ReplaceProgramCode(bytecode, std::span(longer).first(longer.size() - 2)).empty());
   const std::array<uint8_t, 8> garbage = {1, 2, 3, 4, 5, 6, 7, 8};
   CHECK(Shaders::Container::GetProgramCode(garbage).empty());
}

TEST_CASE("Patching: memory patterns, with wildcards")
{
   const std::array<std::byte, 8> memory = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{1}, std::byte{2}, std::byte{4}, std::byte{1}, std::byte{2}};
   const std::array<System::Memory::BytePattern, 3> pattern = {1u, 2u, System::Memory::ANY};
   CHECK(System::Memory::ScanForPattern(memory, pattern).size() == 2);
   CHECK(System::Memory::ScanForPattern(memory, pattern, true) == std::vector<const std::byte*>{memory.data()});
   const std::array<std::byte, 2> exact = {std::byte{2}, std::byte{4}};
   CHECK(System::Memory::ScanForPattern(memory, std::span<const std::byte>(exact)) == std::vector<const std::byte*>{memory.data() + 4});
   const std::array<std::byte, 9> longer = {};
   CHECK(System::Memory::ScanForPattern(memory, std::span<const std::byte>(longer)).empty());
}

TEST_CASE("Patching: patches are variants below files, updated a few hashes at a time")
{
   using namespace Shaders::Variants;
   constexpr uint32_t FILE_AND_PATCH = 0xFEED0001;
   constexpr uint32_t PATCH_ONLY = 0xFEED0002;
   const auto compiled = [](uint8_t byte) {
      auto shader = std::make_shared<Shaders::CompiledShader>();
      shader->bytecode = {byte, byte, byte, byte};
      return std::shared_ptr<const Shaders::CompiledShader>(std::move(shader));
   };
   const std::vector<Variant> before = GetVariants(FILE_AND_PATCH);
   Publish(Source::File, {{.hash = FILE_AND_PATCH, .source = Source::File, .priority = FILE_PRIORITY, .compiled = compiled(1)}});
   const uint64_t generation = GetGeneration();
   Update(Source::Patch,
      {{.hash = FILE_AND_PATCH, .priority = PATCH_PRIORITY, .compiled = compiled(2)}, {.hash = PATCH_ONLY, .priority = PATCH_PRIORITY, .compiled = compiled(3)}}, {});
   CHECK(GetGeneration() != generation);
   std::vector<Variant> variants = GetVariants(FILE_AND_PATCH);
   REQUIRE(variants.size() == 2);
   // A file wins over a patch (1.0)
   CHECK(variants[0].source == Source::File);
   CHECK(variants[1].source == Source::Patch);
   REQUIRE(GetCompiledShader(PATCH_ONLY, Source::Patch));

   // The same bytes again keep the compiled shader they had: nothing is recreated
   const std::shared_ptr<const Shaders::CompiledShader> kept = GetCompiledShader(PATCH_ONLY, Source::Patch);
   Update(Source::Patch, {{.hash = PATCH_ONLY, .priority = PATCH_PRIORITY, .compiled = compiled(3)}}, {});
   CHECK(GetCompiledShader(PATCH_ONLY, Source::Patch) == kept);

   // Removing one hash's patch leaves the others and the files
   const std::array<uint32_t, 1> removed = {PATCH_ONLY};
   Update(Source::Patch, {}, removed);
   CHECK(!HasVariant(PATCH_ONLY));
   CHECK(GetVariants(FILE_AND_PATCH).size() == 2);
   const std::array<uint32_t, 1> all = {FILE_AND_PATCH};
   Update(Source::Patch, {}, all);
   Publish(Source::File, {});
   CHECK(GetVariants(FILE_AND_PATCH).size() == before.size());
}

#ifdef LUMA_RESOURCES_DIR
TEST_CASE("Patching: the FAST noise texture loads as 1.0 made it")
{
   const std::filesystem::path path = std::filesystem::path(LUMA_RESOURCES_DIR) / L"Global" / L"Textures" / L"fast_noise.dds";
   const ComPtr<ID3D11Device> device = CreateWarpDevice();
   ComPtr<ID3D11Resource> resource;
   ComPtr<ID3D11ShaderResourceView> view;
   REQUIRE(SUCCEEDED(DirectX::CreateDDSTextureFromFile(device.Get(), path.c_str(), &resource, &view)));
   ComPtr<ID3D11Texture2D> texture;
   REQUIRE(SUCCEEDED(resource.As(&texture)));
   D3D11_TEXTURE2D_DESC desc = {};
   texture->GetDesc(&desc);
   // 1.0's LoadTexture2DArray: one slice per PNG, RGBA8, one mip
   CHECK(desc.Width == 128);
   CHECK(desc.Height == 128);
   CHECK(desc.ArraySize == 64);
   CHECK(desc.MipLevels == 1);
   CHECK(desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM);
}
#endif
