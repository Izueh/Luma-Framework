#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <d3d11shader.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include "Luma.hpp"
#include "Shaders/Cache.hpp"
#include "Shaders/Compiler.hpp"
#include "Shaders/Container.hpp"
#include "Shaders/D3D12/D3D12RootSignatures.hpp"
#include "Resources/Bindings.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Resources/ResourceDatabase.hpp"
#include "Shaders/Database.hpp"
#include "Shaders/Defines.hpp"
#include "Shaders/Files.hpp"
#include "Shaders/Signatures.hpp"
#include "Shaders/Variants.hpp"
#include "Utils/Hash.hpp"

#pragma comment(lib, "d3dcompiler.lib")

using namespace Luma;
using Shaders::Files::File;
using Shaders::Files::Kind;

namespace {
constexpr std::array<uint32_t, 2> OUTPUT = {0xA8EB118F, 0x922A71D1};
const std::array<Shaders::Files::Redirect, 1> REDIRECTS = {{{"Output_HDR", OUTPUT}}};

std::span<const uint8_t> GetBytes(ID3DBlob* blob)
{
   return {static_cast<const uint8_t*>(blob->GetBufferPointer()), blob->GetBufferSize()};
}
} // namespace

TEST_CASE("Shaders: files are what their place and name say, as in 1.0")
{
   const std::optional<File> single = Shaders::Files::Parse("Motion_Blur_PS_0xB0F56393.ps_5_0.hlsl", REDIRECTS);
   REQUIRE(single);
   CHECK(single->kind == Kind::Replacement);
   CHECK(single->target == "ps_5_0");
   CHECK(single->hashes == std::vector<Shaders::Files::Hash>{{0xB0F56393, "B0F56393"}});

   // Every hash of the name, the ones it redirects to the game's list
   const std::optional<File> multiple = Shaders::Files::Parse("Dev/Name_0x11111111_0x22222222.cs_5_0.hlsl", REDIRECTS);
   REQUIRE(multiple);
   CHECK(multiple->target == "cs_5_0");
   CHECK(multiple->hashes == std::vector<Shaders::Files::Hash>{{0x11111111, "11111111"}, {0x22222222, "22222222"}});
   const std::optional<File> redirected = Shaders::Files::Parse("Output_HDR_0x########.ps_5_0.hlsl", REDIRECTS);
   REQUIRE(redirected);
   CHECK(redirected->hashes == std::vector<Shaders::Files::Hash>{{0xA8EB118F, "A8EB118F"}, {0x922A71D1, "922A71D1"}});
   CHECK(Shaders::Files::Parse("Other_0x########.ps_5_0.hlsl", REDIRECTS)->hashes.empty());

   // A short hash reads as std::stoul does, its define keeps the characters after it
   const std::optional<File> short_hash = Shaders::Files::Parse("Bloom_VS_0x6667BA2.vs_5_0.hlsl", REDIRECTS);
   REQUIRE(short_hash);
   CHECK(short_hash->hashes == std::vector<Shaders::Files::Hash>{{0x06667BA2, "6667BA2."}});

   // Luma's own passes by name, under Luma/ or as a game's "Luma_" file
   const std::optional<File> custom = Shaders::Files::Parse("Luma/Copy/Luma_Copy_PS.hlsl", REDIRECTS);
   REQUIRE(custom);
   CHECK(custom->kind == Kind::Custom);
   CHECK(custom->name == "Luma_Copy_PS");
   CHECK(Shaders::Files::Parse("Luma_MotionVec_UE4_Decode.hlsl", REDIRECTS)->kind == Kind::Custom);

   // Never compiled on their own, or not shaders
   CHECK_FALSE(Shaders::Files::Parse("Includes/Color/Color.hlsl", REDIRECTS));
   CHECK_FALSE(Shaders::Files::Parse("Game/Common.hlsl", REDIRECTS));
   CHECK_FALSE(Shaders::Files::Parse("Notes.hlsl", REDIRECTS));
   CHECK_FALSE(Shaders::Files::Parse("Name_0x12345678.ps_5_0.cso", REDIRECTS));
   CHECK_FALSE(Shaders::Files::Parse("Name_0x12345678.hlsl", REDIRECTS));
}

TEST_CASE("Shaders: cache keys come from the preprocessed code, target and entry, not from file names")
{
   const std::string code = "float4 main() : SV_Target { return 1 ; }\n";
   const uint64_t key = Shaders::Cache::GetKey("#line 1 \"C:\\\\Games\\\\FF7R\\\\Output.hlsl\"\n" + code, "ps_5_0", "main");
   CHECK(key == Shaders::Cache::GetKey("#line 1 \"D:\\\\Other\\\\Output.hlsl\"\n" + code, "ps_5_0", "main"));
   CHECK(key == Shaders::Cache::GetKey(code, "ps_5_0", "main"));
   CHECK(key != Shaders::Cache::GetKey("float4 main() : SV_Target { return 0 ; }\n", "ps_5_0", "main"));
   CHECK(key != Shaders::Cache::GetKey(code, "ps_5_1", "main"));
   CHECK(key != Shaders::Cache::GetKey(code, "ps_5_0", "other"));
}

TEST_CASE("Shaders: define settings compile to 0 to 9")
{
   CHECK(Shaders::Defines::GetText(true) == "1");
   CHECK(Shaders::Defines::GetText(false) == "0");
   CHECK(Shaders::Defines::GetText(int32_t(4)) == "4");
}

TEST_CASE("Shaders: dev settings from the comments of CBuffers.hlsl")
{
   const std::vector<Resources::LumaCBuffers::DevSetting> settings = Resources::LumaCBuffers::ParseDevSettings(
      "    float DevSetting01; // 0.5, 0, 1.3, Custom Name\r\n"
      "    float DevSetting02; // 0, 0, 1\n"
      "    float DevSetting03;\n"
      "    float Other; // 4, 5, 6, Not One\n");
   REQUIRE(settings.size() == 3);
   CHECK(settings[0].default_value == 0.5f);
   CHECK(settings[0].min == 0.f);
   CHECK(settings[0].max == 1.3f);
   CHECK(settings[0].name == "Custom Name");
   CHECK(settings[1].max == 1.f);
   CHECK(settings[1].name.empty());
   CHECK(settings[2].default_value == 0.f);
   CHECK(settings[2].max == 1.f);

   std::string eleven;
   for (int i = 0; i < 11; i++)
      eleven += "float DevSetting; // 1\n";
   CHECK(Resources::LumaCBuffers::ParseDevSettings(eleven).size() == 10);
}

TEST_CASE("Shaders: the target of DXBC bytecode")
{
   const std::string_view source = "float4 main() : SV_Target { return 1; }";
   const std::string_view vertex = "float4 main() : SV_Position { return 1; }";
   Microsoft::WRL::ComPtr<ID3DBlob> vertex_blob;
   REQUIRE(SUCCEEDED(D3DCompile(vertex.data(), vertex.size(), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &vertex_blob, nullptr)));
   CHECK(Shaders::Container::GetTarget(GetBytes(vertex_blob.Get())) == "vs_4_0");
   Microsoft::WRL::ComPtr<ID3DBlob> pixel;
   REQUIRE(SUCCEEDED(D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &pixel, nullptr)));
   CHECK(Shaders::Container::GetTarget(GetBytes(pixel.Get())) == "ps_5_0");
   const std::string_view compute = "RWBuffer<float> output : register(u0); [numthreads(1, 1, 1)] void main() { output[0] = 1; }";
   Microsoft::WRL::ComPtr<ID3DBlob> compute_blob;
   REQUIRE(SUCCEEDED(D3DCompile(compute.data(), compute.size(), nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, &compute_blob, nullptr)));
   CHECK(Shaders::Container::GetTarget(GetBytes(compute_blob.Get())) == "cs_5_0");
   const std::array<uint8_t, 8> garbage = {1, 2, 3, 4, 5, 6, 7, 8};
   CHECK(Shaders::Container::GetTarget(garbage).empty());
}

namespace {
Shaders::Container::Signature CompileSignature(std::string_view source, const char* target)
{
   Microsoft::WRL::ComPtr<ID3DBlob> blob;
   REQUIRE(SUCCEEDED(D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, "main", target, 0, 0, &blob, nullptr)));
   return Shaders::Container::GetSignature(GetBytes(blob.Get()));
}
} // namespace

TEST_CASE("Shaders: signatures from the container, without reflection")
{
   const Shaders::Container::Signature vertex = CompileSignature(
      "void main(float4 v0 : ATTRIBUTE0, out float4 o0 : TEXCOORD0, out float4 o1 : SV_Position) { o0 = v0; o1 = v0; }", "vs_5_0");
   REQUIRE(vertex.inputs.size() == 1);
   CHECK(vertex.inputs[0].semantic == "ATTRIBUTE");
   REQUIRE(vertex.outputs.size() == 2);
   CHECK(vertex.outputs[0].semantic == "TEXCOORD");
   CHECK(vertex.outputs[0].reg == 0);
   CHECK(vertex.outputs[1].semantic == "SV_POSITION");
   CHECK(vertex.outputs[1].reg == 1);
   CHECK(vertex.outputs[1].mask == 0xF);
   // SM5.1 writes the same chunks
   CHECK(CompileSignature("float4 main(float4 p : SV_Position) : SV_Target { return p; }", "ps_5_1").inputs.size() == 1);
}

TEST_CASE("Shaders: a replacement that moves an input to another register doesn't link like the original")
{
   using Shaders::Signatures::Compare;
   // The game's motion blur pixel shader reads SV_Position after an unused TEXCOORD0 (register 1)
   const Shaders::Container::Signature original =
      CompileSignature("float4 main(float4 t : TEXCOORD0, float4 p : SV_Position) : SV_Target { return p.x; }", "ps_5_0");
   const Shaders::Container::Signature dropped = CompileSignature("float4 main(float4 p : SV_Position) : SV_Target { return p.x; }", "ps_5_1");
   const std::vector<std::string> differences = Compare(ShaderStage::Pixel, original, dropped);
   REQUIRE(differences.size() == 1);
   CHECK(differences[0] == "input SV_POSITION0 is in register 0, the original's in 1");
   // Keeping the unused input fixes it, whatever the case of the semantic
   CHECK(Compare(ShaderStage::Pixel, original,
      CompileSignature("float4 main(float4 t : texcoord0, float4 p : SV_POSITION) : SV_Target { return p.x; }", "ps_5_1"))
         .empty());
   // Front facing comes from the rasterizer, not the vertex shader
   CHECK(Compare(ShaderStage::Pixel, original,
      CompileSignature("float4 main(float4 t : TEXCOORD0, float4 p : SV_Position, bool f : SV_IsFrontFace) : SV_Target { return f ? p.x : 0; }", "ps_5_1"))
         .empty());
   CHECK(Compare(ShaderStage::Pixel, original,
            CompileSignature("float4 main(float4 t : TEXCOORD0, float4 p : SV_Position, float4 c : COLOR0) : SV_Target { return p.x + c; }", "ps_5_1")) ==
      std::vector<std::string>{"input COLOR0 isn't one of the original's"});
   // A replacement may declare more components than the original, as long as it reads only the original's
   const Shaders::Container::Signature narrow =
      CompileSignature("float4 main(float2 t : TEXCOORD0, float4 p : SV_Position) : SV_Target { return t.x + p.x; }", "ps_5_0");
   CHECK(Compare(ShaderStage::Pixel, narrow,
      CompileSignature("float4 main(float4 t : TEXCOORD0, float4 p : SV_Position) : SV_Target { return t.y + p.x; }", "ps_5_1"))
         .empty());
   CHECK(Compare(ShaderStage::Pixel, narrow,
            CompileSignature("float4 main(float4 t : TEXCOORD0, float4 p : SV_Position) : SV_Target { return t.z + p.x; }", "ps_5_1")) ==
      std::vector<std::string>{"input TEXCOORD0 reads .z, the original declares .xy"});
   // Another render target
   CHECK(Compare(ShaderStage::Pixel, original,
            CompileSignature("void main(float4 t : TEXCOORD0, float4 p : SV_Position, out float4 a : SV_Target0, out float4 b : SV_Target1) { a = p.x; b = 0; }", "ps_5_1")) ==
      std::vector<std::string>{"output SV_TARGET1 isn't written by the original"});

   // Vertex shader outputs: what the next stage reads must stay where it was
   const Shaders::Container::Signature vertex = CompileSignature(
      "void main(float4 v0 : ATTRIBUTE0, out float4 o0 : TEXCOORD0, out float4 o1 : SV_Position) { o0 = v0; o1 = v0; }", "vs_5_0");
   CHECK(Compare(ShaderStage::Vertex, vertex,
            CompileSignature("void main(float4 v0 : ATTRIBUTE0, float2 v1 : ATTRIBUTE1, out float4 o0 : TEXCOORD0, out float4 o1 : SV_Position) { o0 = v0 + v1.x; o1 = v0; }", "vs_5_1")) ==
      std::vector<std::string>{"input ATTRIBUTE1 isn't one of the original's"});
   CHECK(Compare(ShaderStage::Vertex, vertex,
            CompileSignature("void main(float4 v0 : ATTRIBUTE0, out float4 o1 : SV_Position, out float4 o0 : TEXCOORD0) { o0 = v0; o1 = v0; }", "vs_5_1"))
            .size() == 2);
}

TEST_CASE("Shaders: the registers and spaces a shader declares, without reflection")
{
   using Shaders::Container::BindingType;
   // SM5.0: one register each, space 0
   Microsoft::WRL::ComPtr<ID3DBlob> blob;
   const std::string_view sm50 = "Texture2D t : register(t3); SamplerState s : register(s1); cbuffer c : register(b2) { float4 v; }"
                                 "float4 main(float4 p : SV_Position) : SV_Target { return t.Sample(s, p.xy) * v; }";
   REQUIRE(SUCCEEDED(D3DCompile(sm50.data(), sm50.size(), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &blob, nullptr)));
   const Shaders::Container::Bindings old = Shaders::Container::GetBindings(GetBytes(blob.Get()));
   CHECK(old.found);
   CHECK(old.Declares(BindingType::ShaderResource, 0, 3));
   CHECK(old.Declares(BindingType::Sampler, 0, 1));
   CHECK(old.Declares(BindingType::ConstantBuffer, 0, 2));
   CHECK_FALSE(old.Declares(BindingType::ShaderResource, 0, 2));
   CHECK_FALSE(old.UsesSpace(1));

   // SM5.1: ranges in spaces, unbounded arrays
   const std::string_view sm51 = "Texture2D t[4] : register(t2, space1); Texture2D u[] : register(t0, space7); RWTexture2D<float4> w : register(u1, space3);"
                                 "cbuffer c : register(b5, space100) { float4 v; } SamplerState s : register(s0, space2);"
                                 "float4 main(float4 p : SV_Position) : SV_Target { w[uint2(0, 0)] = v; return t[uint(p.x) % 4].Sample(s, p.xy) + u[uint(p.y)].Sample(s, p.xy); }";
   blob.Reset();
   REQUIRE(SUCCEEDED(D3DCompile(sm51.data(), sm51.size(), nullptr, nullptr, nullptr, "main", "ps_5_1", D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES, 0, &blob, nullptr)));
   const Shaders::Container::Bindings bindings = Shaders::Container::GetBindings(GetBytes(blob.Get()));
   CHECK(bindings.found);
   CHECK(bindings.Declares(BindingType::ShaderResource, 1, 2));
   CHECK(bindings.Declares(BindingType::ShaderResource, 1, 5));
   CHECK_FALSE(bindings.Declares(BindingType::ShaderResource, 1, 6));
   CHECK_FALSE(bindings.Declares(BindingType::ShaderResource, 0, 2));
   CHECK(bindings.Declares(BindingType::ShaderResource, 7, 100000));
   CHECK(bindings.HasUnbounded());
   CHECK(bindings.Declares(BindingType::UnorderedAccess, 3, 1));
   CHECK(bindings.Declares(BindingType::ConstantBuffer, 100, 5));
   CHECK(bindings.Declares(BindingType::Sampler, 2, 0));
   CHECK(bindings.UsesSpace(100));
   CHECK_FALSE(bindings.resource_heap_indexed);

   // SM6 (DXIL): PSV0's resource list, SFI0's heap indexing flag
   std::vector<uint8_t> psv;
   const auto put = [](std::vector<uint8_t>& bytes, uint32_t value) {
      const auto* data = reinterpret_cast<const uint8_t*>(&value);
      bytes.insert(bytes.end(), data, data + 4);
   };
   put(psv, 8); // Runtime info's size, then the info
   put(psv, 0);
   put(psv, 0);
   put(psv, 2); // Resources, each 24 bytes (PSVResourceBindInfo1)
   put(psv, 24);
   for (const std::array<uint32_t, 6> resource : {std::array<uint32_t, 6>{3, 4, 0, UINT32_MAX, 0, 0}, std::array<uint32_t, 6>{2, 0, 1, 1, 0, 0}})
   {
      for (const uint32_t value : resource)
         put(psv, value);
   }
   std::vector<uint8_t> features;
   put(features, 0x2000000);
   put(features, 0);
   std::vector<uint8_t> container = {'D', 'X', 'B', 'C'};
   container.resize(20);
   put(container, 1);
   const uint32_t size = 32 + 8 + 8 + uint32_t(psv.size()) + 8 + uint32_t(features.size());
   put(container, size);
   put(container, 2);
   put(container, 40);
   put(container, 48 + uint32_t(psv.size()));
   container.insert(container.end(), {'P', 'S', 'V', '0'});
   put(container, uint32_t(psv.size()));
   container.insert(container.end(), psv.begin(), psv.end());
   container.insert(container.end(), {'S', 'F', 'I', '0'});
   put(container, uint32_t(features.size()));
   container.insert(container.end(), features.begin(), features.end());
   const Shaders::Container::Bindings dxil = Shaders::Container::GetBindings(container);
   CHECK(dxil.found);
   CHECK(dxil.Declares(BindingType::ShaderResource, 4, 12345));
   CHECK(dxil.Declares(BindingType::ConstantBuffer, 0, 1));
   CHECK_FALSE(dxil.Declares(BindingType::ConstantBuffer, 0, 0));
   CHECK(dxil.resource_heap_indexed);
   CHECK_FALSE(dxil.sampler_heap_indexed);
}

TEST_CASE("Shaders: a D3D12 root signature using anything in Luma's space isn't extended")
{
   REQUIRE(LoadLibraryW(L"d3d12.dll"));
   const auto serialize = reinterpret_cast<PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE>(
      GetProcAddress(GetModuleHandleW(L"d3d12.dll"), "D3D12SerializeVersionedRootSignature"));
   REQUIRE(serialize);
   const auto make = [&](uint32_t range_space, uint32_t sampler_space) {
      const D3D12_DESCRIPTOR_RANGE1 range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, range_space, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 0};
      D3D12_ROOT_PARAMETER1 parameter = {};
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameter.DescriptorTable = {1, &range};
      D3D12_STATIC_SAMPLER_DESC sampler = {};
      sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
      sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      sampler.MaxLOD = D3D12_FLOAT32_MAX;
      sampler.RegisterSpace = sampler_space;
      D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {.Version = D3D_ROOT_SIGNATURE_VERSION_1_1};
      desc.Desc_1_1 = {1, &parameter, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE};
      Microsoft::WRL::ComPtr<ID3DBlob> blob;
      REQUIRE(SUCCEEDED(serialize(&desc, &blob, nullptr)));
      return std::vector<uint8_t>(GetBytes(blob.Get()).begin(), GetBytes(blob.Get()).end());
   };
   // Luma's default space is 100: any register there is a conflict (Development asserts once), any other space is the game's
   CHECK_FALSE(Shaders::RootSignatures::D3D12::Extend(make(1, 0)).blob.empty());
   CHECK(Shaders::RootSignatures::D3D12::Extend(make(100, 0)).blob.empty());
   CHECK(Shaders::RootSignatures::D3D12::Extend(make(0, 100)).blob.empty());
}

TEST_CASE("Shaders: an extended D3D12 root signature has Luma's cbuffers, then its texture table when there's room")
{
   REQUIRE(LoadLibraryW(L"d3d12.dll"));
   const auto serialize = reinterpret_cast<PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE>(
      GetProcAddress(GetModuleHandleW(L"d3d12.dll"), "D3D12SerializeVersionedRootSignature"));
   const auto deserializer = reinterpret_cast<PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER>(
      GetProcAddress(GetModuleHandleW(L"d3d12.dll"), "D3D12CreateVersionedRootSignatureDeserializer"));
   REQUIRE(serialize);
   REQUIRE(deserializer);
   // `constants` DWORDs of root constants, at version 1.0 or 1.1
   const auto make = [&](uint32_t constants, D3D_ROOT_SIGNATURE_VERSION version) {
      D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {.Version = version};
      D3D12_ROOT_PARAMETER parameter = {};
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
      parameter.Constants = {0, 0, constants};
      D3D12_ROOT_PARAMETER1 parameter1 = {};
      parameter1.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
      parameter1.Constants = {0, 0, constants};
      if (version == D3D_ROOT_SIGNATURE_VERSION_1_0)
         desc.Desc_1_0 = {1, &parameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
      else
         desc.Desc_1_1 = {1, &parameter1, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
      Microsoft::WRL::ComPtr<ID3DBlob> blob;
      REQUIRE(SUCCEEDED(serialize(&desc, &blob, nullptr)));
      return std::vector<uint8_t>(GetBytes(blob.Get()).begin(), GetBytes(blob.Get()).end());
   };
   for (const D3D_ROOT_SIGNATURE_VERSION version : {D3D_ROOT_SIGNATURE_VERSION_1_0, D3D_ROOT_SIGNATURE_VERSION_1_1})
   {
      const Shaders::RootSignatures::D3D12::Extended extended = Shaders::RootSignatures::D3D12::Extend(make(8, version));
      REQUIRE_FALSE(extended.blob.empty());
      CHECK(extended.luma_parameter == 1);
      CHECK(extended.table);
      Microsoft::WRL::ComPtr<ID3D12VersionedRootSignatureDeserializer> reader;
      REQUIRE(SUCCEEDED(deserializer(extended.blob.data(), extended.blob.size(), IID_PPV_ARGS(&reader))));
      const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* read = nullptr;
      REQUIRE(SUCCEEDED(reader->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_1, &read)));
      REQUIRE(read->Desc_1_1.NumParameters == 4);
      const D3D12_ROOT_PARAMETER1& table = read->Desc_1_1.pParameters[1 + Shaders::RootSignatures::D3D12::LUMA_TABLE_PARAMETER];
      REQUIRE(table.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE);
      REQUIRE(table.DescriptorTable.NumDescriptorRanges == 1);
      const D3D12_DESCRIPTOR_RANGE1& range = table.DescriptorTable.pDescriptorRanges[0];
      CHECK(range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
      CHECK(range.BaseShaderRegister == Resources::Bindings::D3D12_FIRST_TEXTURE_REGISTER);
      CHECK(range.NumDescriptors == Resources::Bindings::D3D12_TEXTURE_REGISTERS);
      CHECK(range.RegisterSpace == Resources::Bindings::GetOptions().d3d12_space);
   }
   // 60 DWORDs leave room for the two root CBVs only
   const Shaders::RootSignatures::D3D12::Extended full = Shaders::RootSignatures::D3D12::Extend(make(60, D3D_ROOT_SIGNATURE_VERSION_1_1));
   CHECK_FALSE(full.blob.empty());
   CHECK_FALSE(full.table);
}

TEST_CASE("Shaders: a Luma file's declaration lines are its programs")
{
   using Shaders::Files::Declaration;
   const std::vector<Declaration> single = Shaders::Files::ParseDeclarations("// Luma: ps_5_0\nfloat4 main() : SV_Target { return 1; }\n", "Luma_Copy_PS");
   REQUIRE(single.size() == 1);
   CHECK(single[0] == Declaration{"Luma_Copy_PS", "ps_5_0", "main", {}});
   const std::vector<Declaration> several = Shaders::Files::ParseDeclarations(
      "  // Luma: cs_5_0 Luma_DrawPurple_CS COLOR=float4(1.0,0.0,1.0,1.0)\r\n// Luma: ps_5_0 entry=sharpen_ps MS=1 A_SAT=1\r\n// Not a declaration\r\n", "Luma_File");
   REQUIRE(several.size() == 2);
   CHECK(several[0] == Declaration{"Luma_DrawPurple_CS", "cs_5_0", "main", {{"COLOR", "float4(1.0,0.0,1.0,1.0)"}}});
   CHECK(several[1] == Declaration{"Luma_File", "ps_5_0", "sharpen_ps", {{"MS", "1"}, {"A_SAT", "1"}}});
   // Includes and samples declare nothing: they aren't compiled on their own
   CHECK(Shaders::Files::ParseDeclarations("float4 main() : SV_Target { return 1; }", "Luma_SampleCS.cs_5_0").empty());
}

TEST_CASE("Resources: Core's entries are read-only for a game")
{
   using Resources::Database::Get;
   using Resources::Database::Owner;
   using Resources::Database::Register;
   using DatabaseKind = Resources::Database::Kind;
   const uint64_t core_first = Utils::Hash::Name("TestCoreFirst");
   const std::optional<uint32_t> core = Register({.name_hash = core_first, .name = "TestCoreFirst", .kind = DatabaseKind::ConstantBuffer, .owner = Owner::Core});
   REQUIRE(core);
   CHECK_FALSE(Register({.name_hash = core_first, .name = "TestCoreFirst", .kind = DatabaseKind::ConstantBuffer, .owner = Owner::Game}));
   CHECK(Get(*core).owner == Owner::Core);

   // Registered by the game first, the name is still Core's
   const uint64_t game_first = Utils::Hash::Name("TestGameFirst");
   const std::optional<uint32_t> game = Register({.name_hash = game_first, .name = "TestGameFirst", .kind = DatabaseKind::Shader, .owner = Owner::Game});
   REQUIRE(game);
   CHECK(Register({.name_hash = game_first, .name = "TestGameFirst", .kind = DatabaseKind::ConstantBuffer, .owner = Owner::Core, .size = 16}) == game);
   CHECK(Get(*game).owner == Owner::Core);
   CHECK(Get(*game).kind == DatabaseKind::ConstantBuffer);
}

// The stand-in game's GameDeviceData (Tests compiles Core like a game)
struct Luma::Game::GameDeviceData
{
   float value = 2.f;
};

namespace {
void UpdateTestInstanceData(CB::LumaInstanceData& data, const Game::GameDeviceData& game_device_data, const Frame::Resolutions& resolutions)
{
   data.GameData.Value = game_device_data.value;
   data.GameData.RenderWidth = resolutions.render.x;
}

template <typename T, size_t Size>
T Read(const std::array<std::byte, Size>& bytes)
{
   static_assert(sizeof(T) <= Size);
   T value;
   std::memcpy(&value, bytes.data(), sizeof(T));
   return value;
}

struct Member
{
   const char* name;
   size_t offset;
};

// Every member of an HLSL struct type, by name, is where C++ put it
void CheckMembers(ID3D11ShaderReflectionType* type, std::span<const Member> members)
{
   D3D11_SHADER_TYPE_DESC desc = {};
   REQUIRE(SUCCEEDED(type->GetDesc(&desc)));
   CHECK(desc.Members == members.size());
   for (const Member& member : members)
   {
      INFO(member.name);
      D3D11_SHADER_TYPE_DESC member_desc = {};
      REQUIRE(SUCCEEDED(type->GetMemberTypeByName(member.name)->GetDesc(&member_desc)));
      CHECK(member_desc.Offset == member.offset);
   }
}

// The type of the one variable of a cbuffer, whose size is checked
ID3D11ShaderReflectionType* GetStructType(ID3D11ShaderReflection* reflection, const char* cbuffer, uint32_t size)
{
   ID3D11ShaderReflectionConstantBuffer* buffer = reflection->GetConstantBufferByName(cbuffer);
   D3D11_SHADER_BUFFER_DESC desc = {};
   REQUIRE(SUCCEEDED(buffer->GetDesc(&desc)));
   CHECK(desc.Size == size);
   return buffer->GetVariableByIndex(0)->GetType();
}
} // namespace

#define MEMBER(Struct, Name) \
   Member { #Name, offsetof(Struct, Name) }

TEST_CASE("Resources: the C++ cbuffer structs are laid out as FXC packs the shaders'")
{
   // The structs as a Luma shader includes them (with CPU_DEVELOPMENT, as Shaders::Defines passes it): the stand-in game's parts,
   // then CBuffers.hlsl, from the repository
   const std::filesystem::path tests = std::filesystem::path(__FILE__).parent_path();
   const std::filesystem::path temporary = std::filesystem::temp_directory_path() / std::format("luma2_cbuffers_test_{}", GetCurrentProcessId());
   std::filesystem::create_directories(temporary);
   {
      std::ofstream file(temporary / "CBufferLayout.hlsl", std::ios::binary);
      file << "#include \"Game/GameCBuffers.hlsl\"\n#include \"Includes/Core/CBuffers.hlsl\"\n"
              "cbuffer LumaSettings : register(b13) { CB::LumaGlobalSettings LumaSettings : packoffset(c0); }\n"
              "cbuffer LumaData : register(b12) { CB::LumaInstanceData LumaData : packoffset(c0); }\n"
              "float4 main() : SV_Target { return LumaSettings.GameSettings.Last + LumaData.GameData.Value; }\n";
   }
   const std::array<std::filesystem::path, 3> roots = {temporary, tests, tests / "../../Resources/Global/Shaders"};
   std::vector<Shaders::Compiler::Define> defines;
#if DEVELOPMENT
   defines.push_back({"CPU_DEVELOPMENT", "1"});
#endif
   const Shaders::Compiler::Compiled compiled = Shaders::Compiler::Compile(roots, "CBufferLayout.hlsl", defines, "ps_5_0", "main");
   std::error_code error;
   std::filesystem::remove_all(temporary, error);
   INFO(compiled.messages);
   REQUIRE_FALSE(compiled.failed);
   Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
   REQUIRE(SUCCEEDED(D3DReflect(compiled.bytecode.data(), compiled.bytecode.size(), IID_PPV_ARGS(&reflection))));

   using Settings = CB::LumaGlobalSettings;
   ID3D11ShaderReflectionType* settings = GetStructType(reflection.Get(), "LumaSettings", Resources::LumaCBuffers::SETTINGS_SIZE);
   CheckMembers(settings, std::to_array({MEMBER(Settings, SwapchainSize), MEMBER(Settings, SwapchainInvSize), MEMBER(Settings, RenderSize), MEMBER(Settings, RenderInvSize), MEMBER(Settings, DisplayMode), MEMBER(Settings, PeakWhiteNits), MEMBER(Settings, GamePaperWhiteNits), MEMBER(Settings, UIPaperWhiteNits), MEMBER(Settings, SRType), MEMBER(Settings, FrameIndex),
#if DEVELOPMENT
                             MEMBER(Settings, DevSetting01), MEMBER(Settings, DevSetting02), MEMBER(Settings, DevSetting03), MEMBER(Settings, DevSetting04), MEMBER(Settings, DevSetting05), MEMBER(Settings, DevSetting06), MEMBER(Settings, DevSetting07), MEMBER(Settings, DevSetting08), MEMBER(Settings, DevSetting09), MEMBER(Settings, DevSetting10),
#else
                             MEMBER(Settings, Padding1),
#endif
                             MEMBER(Settings, GameSettings)}));
#if DEVELOPMENT
   CHECK(offsetof(Settings, GameSettings) == 96);
#else
   CHECK(offsetof(Settings, GameSettings) == 64);
#endif
   using GameSettings = CB::LumaGameSettings;
   CheckMembers(settings->GetMemberTypeByName("GameSettings"),
      std::to_array({MEMBER(GameSettings, Strength), MEMBER(GameSettings, Mode), MEMBER(GameSettings, Center), MEMBER(GameSettings, Tint), MEMBER(GameSettings, Last)}));

   using Data = CB::LumaInstanceData;
   ID3D11ShaderReflectionType* data = GetStructType(reflection.Get(), "LumaData", Resources::LumaCBuffers::DATA_SIZE);
   CheckMembers(data, std::to_array({MEMBER(Data, CustomData1), MEMBER(Data, CustomData2), MEMBER(Data, CustomData3), MEMBER(Data, CustomData4), MEMBER(Data, RenderResolutionScale), MEMBER(Data, PreviousRenderResolutionScale), MEMBER(Data, RenderScaleActive), MEMBER(Data, Padding1), MEMBER(Data, GameData)}));
   CHECK(offsetof(Data, GameData) == 48);
   using GameData = CB::LumaGameData;
   CheckMembers(data->GetMemberTypeByName("GameData"), std::to_array({MEMBER(GameData, Resolution), MEMBER(GameData, Size), MEMBER(GameData, Value), MEMBER(GameData, RenderWidth)}));
}

#undef MEMBER

TEST_CASE("Resources: LumaSettings uploads cb_luma_global_settings as it was published")
{
   using namespace Resources::LumaCBuffers;
   const CB::LumaGlobalSettings saved = cb_luma_global_settings;
   cb_luma_global_settings.GameSettings.Strength = 0.25f;
   cb_luma_global_settings.GameSettings.Tint = {1.f, 2.f, 3.f, 4.f};
   const uint64_t stamp = GetSettingsStamp();
   Game::MarkLumaSettingsDirty();
   CHECK(GetSettingsStamp() != stamp);
   // Not published yet
   cb_luma_global_settings.GameSettings.Strength = 0.75f;

   std::array<std::byte, SETTINGS_SIZE> bytes;
   WriteSettings(bytes, {}, {});
   CB::LumaGlobalSettings written = Read<CB::LumaGlobalSettings>(bytes);
   CHECK(written.GameSettings.Strength == 0.25f);
   CHECK(written.GameSettings.Tint == Math::float4{1.f, 2.f, 3.f, 4.f});
   CHECK(written.DisplayMode == saved.DisplayMode);
   // The device's sizes: the screen's, for a device Luma doesn't know
   CHECK(written.SwapchainSize.x == float(GetSystemMetrics(SM_CXSCREEN)));
   CHECK(written.SwapchainInvSize.x == 1.f / written.SwapchainSize.x);

   MarkSettingsDirty();
   WriteSettings(bytes, {}, {});
   written = Read<CB::LumaGlobalSettings>(bytes);
   CHECK(written.GameSettings.Strength == 0.75f);

   cb_luma_global_settings = saved;
   MarkSettingsDirty();
}

TEST_CASE("Resources: LumaData is Luma's part, the pass's values and the game's part from its writer")
{
   using namespace Resources::LumaCBuffers;
   SetOptions({.update_luma_instance_data = &UpdateTestInstanceData});
   const Game::GameDeviceDataType type = Game::GameDeviceDataType::Of<Game::GameDeviceData>();
   Game::GameDeviceData* game_device_data = type.create();
   REQUIRE(game_device_data);
   CHECK(game_device_data->value == 2.f);

   std::array<std::byte, DATA_SIZE> bytes;
   WriteData(bytes, {.custom_data_1 = 7, .custom_data_4 = 0.5f}, {.game_device_data = game_device_data});
   CB::LumaInstanceData written = Read<CB::LumaInstanceData>(bytes);
   CHECK(written.CustomData1 == 7);
   CHECK(written.CustomData4 == 0.5f);
   CHECK(written.RenderResolutionScale == Math::float2{1.f, 1.f});
   // The game's writer, with the device's data and resolutions (the screen's, for a device Luma doesn't know)
   CHECK(written.GameData.Value == 2.f);
   CHECK(written.GameData.RenderWidth == uint32_t(GetSystemMetrics(SM_CXSCREEN)));
   CHECK(written.GameData.Resolution == Math::float4{});

   // Without the device's data, the game's part stays zero
   WriteData(bytes, {}, {});
   written = Read<CB::LumaInstanceData>(bytes);
   CHECK(written.GameData.Value == 0.f);
   CHECK(written.GameData.RenderWidth == 0);

   type.destroy(game_device_data);
   SetOptions({});
}

TEST_CASE("Resources: a context uploads a stamped cbuffer again when the stamp moves")
{
   using Resources::Database::NeedsUpload;
   using Resources::Database::Upload;
   CHECK(NeedsUpload({}, 0));
   Upload uploaded;
   uploaded.stamp = 5;
   uploaded.done = true;
   CHECK_FALSE(NeedsUpload(uploaded, 5));
   CHECK(NeedsUpload(uploaded, 6));
   // Forgotten (a command list finished)
   uploaded.done = false;
   CHECK(NeedsUpload(uploaded, 5));
}

TEST_CASE("Resources: a context uploads a cbuffer without a stamp when what it writes differs from its last upload")
{
   using Resources::Database::ReplaceIfChanged;
   Resources::Database::Upload upload;
   std::vector<std::byte> written = {std::byte{1}, std::byte{2}};
   // The first write is uploaded, and becomes the last upload
   CHECK(ReplaceIfChanged(upload, written));
   CHECK(upload.done);
   CHECK(upload.bytes == std::vector<std::byte>{std::byte{1}, std::byte{2}});
   // The same bytes aren't
   written = {std::byte{1}, std::byte{2}};
   CHECK_FALSE(ReplaceIfChanged(upload, written));
   // Other bytes are (another pass's custom values, another resolution)
   written = {std::byte{1}, std::byte{3}};
   CHECK(ReplaceIfChanged(upload, written));
   CHECK(upload.bytes == std::vector<std::byte>{std::byte{1}, std::byte{3}});
   // Forgotten, the same bytes are uploaded again
   upload.done = false;
   written = upload.bytes;
   CHECK(ReplaceIfChanged(upload, written));
}

TEST_CASE("Shaders: a hash's variants, by source and priority")
{
   using namespace Shaders::Variants;
   constexpr uint32_t HASH = 0x7E57AB1E;
   auto compiled = std::make_shared<Shaders::CompiledShader>();
   compiled->bytecode = {1, 2, 3};
   Publish(Source::File, {{.hash = HASH, .file = "Test_0x7E57AB1E.ps_5_0.hlsl", .compiled = compiled}});
   CHECK(HasVariant(HASH));
   CHECK(GetCompiledShader(HASH, Source::File) == compiled);
   CHECK_FALSE(GetCompiledShader(HASH, Source::Patch));
   REQUIRE(GetVariants(HASH).size() == 1);
   CHECK(GetVariants(HASH)[0].source == Source::File);

   // A failed build keeps the last version that compiled, equal bytes keep the same compiled shader
   Publish(Source::File, {{.hash = HASH, .compiled = nullptr, .failed = true}});
   CHECK(GetCompiledShader(HASH, Source::File) == compiled);
   auto same = std::make_shared<Shaders::CompiledShader>();
   same->bytecode = compiled->bytecode;
   Publish(Source::File, {{.hash = HASH, .compiled = same}});
   CHECK(GetCompiledShader(HASH, Source::File) == compiled);

   // Another source's publication keeps this one's; a higher priority comes first
   Publish(Source::Patch, {{.hash = HASH, .priority = FILE_PRIORITY + 1, .compiled = same}});
   REQUIRE(GetVariants(HASH).size() == 2);
   CHECK(GetVariants(HASH)[0].source == Source::Patch);
   CHECK(GetName(GetChoice(Source::Patch)) == "patch");

   Publish(Source::File, {});
   Publish(Source::Patch, {});
   CHECK_FALSE(HasVariant(HASH));
   CHECK(GetVariants(HASH).empty());
}

TEST_CASE("Compiler: FXC is used by one thread at a time")
{
   constexpr std::string_view source = "float4 main() : SV_Target { return 1; }";
   Microsoft::WRL::ComPtr<ID3DBlob> blob;
   REQUIRE(SUCCEEDED(D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &blob, nullptr)));
   const std::span<const uint8_t> bytecode(static_cast<const uint8_t*>(blob->GetBufferPointer()), blob->GetBufferSize());

   std::atomic<bool> finished = false;
   std::atomic<bool> disassembled = false;
   std::thread other;
   {
      const Shaders::Compiler::Lock lock;
      // The thread holding the lock can still call FXC: a caller holds it across its use of a reflector
      Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflector;
      CHECK(Shaders::Compiler::Reflect(bytecode, IID_PPV_ARGS(&reflector)));
      other = std::thread([&] {
         disassembled = Shaders::Compiler::Disassemble(bytecode).has_value();
         finished = true;
      });
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
      CHECK_FALSE(finished);
   }
   other.join();
   CHECK(finished);
   CHECK(disassembled);
}
