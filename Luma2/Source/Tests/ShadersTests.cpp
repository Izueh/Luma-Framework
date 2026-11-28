#include <doctest/doctest.h>

#include <algorithm>
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
#include <dxgi1_4.h>
#include <wrl/client.h>

#include "Luma.hpp"
#include "Shaders/ShaderCache.hpp"
#include "Shaders/ShaderCompiler.hpp"
#include "Shaders/ShaderContainer.hpp"
#include "Shaders/ShaderStore.hpp"
#include "Shaders/D3D12/D3D12PipelineLibrary.hpp"
#include "Shaders/D3D12/D3D12RootSignatures.hpp"
#include "Resources/ResourceBindings.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Resources/ResourceDatabase.hpp"
#include "Shaders/ShaderDatabase.hpp"
#include "Shaders/ShaderDefines.hpp"
#include "Shaders/ShaderFiles.hpp"
#include "Shaders/ShaderInvariants.hpp"
#include "Shaders/ShaderSignatures.hpp"
#include "Shaders/Shaders.hpp"
#include "Shaders/ShaderVariants.hpp"
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
   CHECK(Shaders::Container::Declares(old, BindingType::ShaderResource, 0, 3));
   CHECK(Shaders::Container::Declares(old, BindingType::Sampler, 0, 1));
   CHECK(Shaders::Container::Declares(old, BindingType::ConstantBuffer, 0, 2));
   CHECK_FALSE(Shaders::Container::Declares(old, BindingType::ShaderResource, 0, 2));
   CHECK_FALSE(Shaders::Container::UsesSpace(old, 1));

   // SM5.1: ranges in spaces, unbounded arrays
   const std::string_view sm51 = "Texture2D t[4] : register(t2, space1); Texture2D u[] : register(t0, space7); RWTexture2D<float4> w : register(u1, space3);"
                                 "cbuffer c : register(b5, space100) { float4 v; } SamplerState s : register(s0, space2);"
                                 "float4 main(float4 p : SV_Position) : SV_Target { w[uint2(0, 0)] = v; return t[uint(p.x) % 4].Sample(s, p.xy) + u[uint(p.y)].Sample(s, p.xy); }";
   blob.Reset();
   REQUIRE(SUCCEEDED(D3DCompile(sm51.data(), sm51.size(), nullptr, nullptr, nullptr, "main", "ps_5_1", D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES, 0, &blob, nullptr)));
   const Shaders::Container::Bindings bindings = Shaders::Container::GetBindings(GetBytes(blob.Get()));
   CHECK(bindings.found);
   CHECK(Shaders::Container::Declares(bindings, BindingType::ShaderResource, 1, 2));
   CHECK(Shaders::Container::Declares(bindings, BindingType::ShaderResource, 1, 5));
   CHECK_FALSE(Shaders::Container::Declares(bindings, BindingType::ShaderResource, 1, 6));
   CHECK_FALSE(Shaders::Container::Declares(bindings, BindingType::ShaderResource, 0, 2));
   CHECK(Shaders::Container::Declares(bindings, BindingType::ShaderResource, 7, 100000));
   CHECK(Shaders::Container::HasUnbounded(bindings));
   CHECK(Shaders::Container::Declares(bindings, BindingType::UnorderedAccess, 3, 1));
   CHECK(Shaders::Container::Declares(bindings, BindingType::ConstantBuffer, 100, 5));
   CHECK(Shaders::Container::Declares(bindings, BindingType::Sampler, 2, 0));
   CHECK(Shaders::Container::UsesSpace(bindings, 100));
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
   CHECK(Shaders::Container::Declares(dxil, BindingType::ShaderResource, 4, 12345));
   CHECK(Shaders::Container::Declares(dxil, BindingType::ConstantBuffer, 0, 1));
   CHECK_FALSE(Shaders::Container::Declares(dxil, BindingType::ConstantBuffer, 0, 0));
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

TEST_CASE("Shaders: an extended D3D12 root signature has Luma's static samplers after the game's, at every version, when the mod asks")
{
   REQUIRE(LoadLibraryW(L"d3d12.dll"));

   struct StaticSamplers
   {
      StaticSamplers() { Resources::Bindings::SetOptions({.static_samplers = true}); }

      ~StaticSamplers() { Resources::Bindings::SetOptions({}); }
   } static_samplers;

   const auto serialize = reinterpret_cast<PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE>(
      GetProcAddress(GetModuleHandleW(L"d3d12.dll"), "D3D12SerializeVersionedRootSignature"));
   const auto deserializer = reinterpret_cast<PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER>(
      GetProcAddress(GetModuleHandleW(L"d3d12.dll"), "D3D12CreateVersionedRootSignatureDeserializer"));
   REQUIRE(serialize);
   REQUIRE(deserializer);
   const uint32_t space = Resources::Bindings::GetOptions().d3d12_space;
   // The game's own static sampler, s3 in space 0, stays first and as it was
   D3D12_STATIC_SAMPLER_DESC game_sampler = {};
   game_sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
   game_sampler.AddressU = game_sampler.AddressV = game_sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
   game_sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
   game_sampler.MaxLOD = D3D12_FLOAT32_MAX;
   game_sampler.ShaderRegister = 3;
   D3D12_STATIC_SAMPLER_DESC1 game_sampler1 = {};
   std::memcpy(&game_sampler1, &game_sampler, sizeof(game_sampler));
   const auto check = [&](const D3D12_STATIC_SAMPLER_DESC* samplers, UINT count) {
      REQUIRE(count == 1 + Resources::Bindings::D3D12_STATIC_SAMPLERS);
      CHECK(samplers[0].ShaderRegister == 3);
      CHECK(samplers[0].RegisterSpace == 0);
      CHECK(samplers[0].AddressU == D3D12_TEXTURE_ADDRESS_MODE_MIRROR);
      for (uint32_t i = 0; i < Resources::Bindings::D3D12_STATIC_SAMPLERS; i++)
      {
         CHECK(samplers[1 + i].ShaderRegister == i);
         CHECK(samplers[1 + i].RegisterSpace == space);
      }
      using Resources::Bindings::StaticSampler;
      CHECK(samplers[1 + uint32_t(StaticSampler::PointClamp)].Filter == D3D12_FILTER_MIN_MAG_MIP_POINT);
      CHECK(samplers[1 + uint32_t(StaticSampler::PointClamp)].AddressU == D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
      CHECK(samplers[1 + uint32_t(StaticSampler::LinearWrap)].AddressV == D3D12_TEXTURE_ADDRESS_MODE_WRAP);
      CHECK(samplers[1 + uint32_t(StaticSampler::AnisotropicWrap)].Filter == D3D12_FILTER_ANISOTROPIC);
      CHECK(samplers[1 + uint32_t(StaticSampler::AnisotropicWrap)].MaxAnisotropy == 16);
   };
   for (const D3D_ROOT_SIGNATURE_VERSION version : {D3D_ROOT_SIGNATURE_VERSION_1_0, D3D_ROOT_SIGNATURE_VERSION_1_1, D3D_ROOT_SIGNATURE_VERSION_1_2})
   {
      CAPTURE(int(version));
      D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {.Version = version};
      if (version == D3D_ROOT_SIGNATURE_VERSION_1_0)
         desc.Desc_1_0 = {0, nullptr, 1, &game_sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE};
      else if (version == D3D_ROOT_SIGNATURE_VERSION_1_1)
         desc.Desc_1_1 = {0, nullptr, 1, &game_sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE};
      else
         desc.Desc_1_2 = {0, nullptr, 1, &game_sampler1, D3D12_ROOT_SIGNATURE_FLAG_NONE};
      Microsoft::WRL::ComPtr<ID3DBlob> blob;
      if (FAILED(serialize(&desc, &blob, nullptr)))
      {
         // Version 1.2 needs a runtime that knows it (an Agility SDK or a recent Windows)
         MESSAGE("this d3d12.dll can't serialize root signature version ", int(version));
         continue;
      }
      const Shaders::RootSignatures::D3D12::Extended extended = Shaders::RootSignatures::D3D12::Extend(GetBytes(blob.Get()));
      REQUIRE_FALSE(extended.blob.empty());
      CHECK(extended.static_samplers);
      Microsoft::WRL::ComPtr<ID3D12VersionedRootSignatureDeserializer> reader;
      REQUIRE(SUCCEEDED(deserializer(extended.blob.data(), extended.blob.size(), IID_PPV_ARGS(&reader))));
      const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* read = reader->GetUnconvertedRootSignatureDesc();
      REQUIRE(read);
      CHECK(read->Version == version);
      if (version == D3D_ROOT_SIGNATURE_VERSION_1_0)
         check(read->Desc_1_0.pStaticSamplers, read->Desc_1_0.NumStaticSamplers);
      else if (version == D3D_ROOT_SIGNATURE_VERSION_1_1)
         check(read->Desc_1_1.pStaticSamplers, read->Desc_1_1.NumStaticSamplers);
      else
      {
         // DESC1 starts as DESC, then its flags
         std::vector<D3D12_STATIC_SAMPLER_DESC> samplers;
         for (UINT i = 0; i < read->Desc_1_2.NumStaticSamplers; i++)
         {
            CHECK(read->Desc_1_2.pStaticSamplers[i].Flags == D3D12_SAMPLER_FLAG_NONE);
            samplers.push_back(reinterpret_cast<const D3D12_STATIC_SAMPLER_DESC&>(read->Desc_1_2.pStaticSamplers[i]));
         }
         check(samplers.data(), UINT(samplers.size()));
      }
   }
   // Off (the default): none, and no defines for them
   {
      Resources::Bindings::SetOptions({});
      D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {.Version = D3D_ROOT_SIGNATURE_VERSION_1_1};
      desc.Desc_1_1 = {0, nullptr, 1, &game_sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE};
      Microsoft::WRL::ComPtr<ID3DBlob> blob;
      REQUIRE(SUCCEEDED(serialize(&desc, &blob, nullptr)));
      const Shaders::RootSignatures::D3D12::Extended extended = Shaders::RootSignatures::D3D12::Extend(GetBytes(blob.Get()));
      REQUIRE_FALSE(extended.blob.empty());
      CHECK_FALSE(extended.static_samplers);
      const std::vector<Shaders::Compiler::Define> defines = Resources::LumaCBuffers::GetDefines(Api::D3D12);
      CHECK(std::ranges::none_of(defines, [](const Shaders::Compiler::Define& define) { return define.name == Resources::Bindings::D3D12_STATIC_SAMPLER_DEFINES[0]; }));
      Resources::Bindings::SetOptions({.static_samplers = true});
   }
   // A game sampler in Luma's space is a conflict: not extended (Development asserts once)
   game_sampler.RegisterSpace = space;
   D3D12_VERSIONED_ROOT_SIGNATURE_DESC conflict = {.Version = D3D_ROOT_SIGNATURE_VERSION_1_1};
   conflict.Desc_1_1 = {0, nullptr, 1, &game_sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE};
   Microsoft::WRL::ComPtr<ID3DBlob> conflict_blob;
   REQUIRE(SUCCEEDED(serialize(&conflict, &conflict_blob, nullptr)));
   CHECK(Shaders::RootSignatures::D3D12::Extend(GetBytes(conflict_blob.Get())).blob.empty());
}

TEST_CASE("Shaders: a D3D12 format set recorded for a pipeline is found again for it, once")
{
   Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
   REQUIRE(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
   Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
   REQUIRE(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
   Microsoft::WRL::ComPtr<ID3D12Device> device;
   REQUIRE(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
   namespace Library = Shaders::PipelineLibrary::D3D12;
   Shaders::Database::D3D12::TargetFormats formats;
   formats.render_targets[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
   formats.depth = DXGI_FORMAT_D32_FLOAT;
   constexpr uint64_t PIPELINE = 0x1234567890ABCDEFull;
   Library::RecordFormats(PIPELINE, formats);
   Library::RecordFormats(PIPELINE, formats);
   const std::vector<Shaders::Database::D3D12::TargetFormats> found = Library::GetFormats(device.Get(), PIPELINE);
   REQUIRE(found.size() == 1);
   CHECK(found[0] == formats);
   CHECK(Library::GetFormats(device.Get(), PIPELINE + 1).empty());
   Library::ReleaseDeviceObjects(device.Get());
}

TEST_CASE("Shaders: D3D12 upgrades DXBC below SM5.1 to SM5.1 and keeps DXIL 6.x, D3D11 keeps DXBC and refuses DXIL")
{
   CHECK(Shaders::GetTarget("ps_4_0", Api::D3D12) == "ps_5_1");
   CHECK(Shaders::GetTarget("vs_4_1", Api::D3D12) == "vs_5_1");
   CHECK(Shaders::GetTarget("cs_5_0", Api::D3D12) == "cs_5_1");
   CHECK(Shaders::GetTarget("ps_5_1", Api::D3D12) == "ps_5_1");
   CHECK(Shaders::GetTarget("ps_6_0", Api::D3D12) == "ps_6_0");
   CHECK(Shaders::GetTarget("cs_6_6", Api::D3D12) == "cs_6_6");
   CHECK(Shaders::GetTarget("ms_6_5", Api::D3D12) == "ms_6_5");
   CHECK(Shaders::GetTarget("as_6_5", Api::D3D12) == "as_6_5");
   CHECK(Shaders::GetTarget("lib_6_6", Api::D3D12) == "lib_6_6");
   CHECK(Shaders::GetTarget("ps_4_0", Api::D3D11) == "ps_4_0");
   CHECK(Shaders::GetTarget("ps_5_0", Api::D3D11) == "ps_5_0");
   CHECK(Shaders::GetTarget("ps_6_0", Api::D3D11).empty());
   CHECK(Shaders::GetTarget("lib_6_6", Api::D3D11).empty());

   // Files::Parse supports 6-char and 7-char SM6 targets
   const std::optional<File> mesh = Shaders::Files::Parse("Nanite_MS_0x12345678.ms_6_5.hlsl", REDIRECTS);
   REQUIRE(mesh);
   CHECK(mesh->target == "ms_6_5");
   const std::optional<File> lib = Shaders::Files::Parse("RayGen_0x87654321.lib_6_6.hlsl", REDIRECTS);
   REQUIRE(lib);
   CHECK(lib->target == "lib_6_6");
}

#if DEVELOPMENT || TEST
namespace {
std::vector<uint8_t> CompileBytes(std::string_view source, const char* target)
{
   Microsoft::WRL::ComPtr<ID3DBlob> blob;
   Microsoft::WRL::ComPtr<ID3DBlob> errors;
   const HRESULT result = D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, "main", target, 0, 0, &blob, &errors);
   INFO((errors ? std::string_view(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : std::string_view()));
   REQUIRE(SUCCEEDED(result));
   return {GetBytes(blob.Get()).begin(), GetBytes(blob.Get()).end()};
}
} // namespace

TEST_CASE("Shaders: a D3D12 variant is SM5.1 when it reads Luma's, declares Luma's registers in its space and nothing else there")
{
   using Shaders::Invariants::CheckD3D12;
   using Shaders::Invariants::D3D12Rules;
   const D3D12Rules base = {.space = 100, .settings_slot = 13, .data_slot = 12, .static_samplers = true};

   // SM5.0: fine when it reads nothing of Luma's (a patch that added nothing), not when it does
   const std::vector<uint8_t> sm50 = CompileBytes("Texture2D t : register(t0); float4 main(float4 p : SV_Position) : SV_Target { return t.Load(int3(p.xy, 0)); }", "ps_5_0");
   CHECK(CheckD3D12(sm50, base).errors.empty());
   D3D12Rules reads = base;
   reads.reads_luma = true;
   CHECK_FALSE(CheckD3D12(sm50, reads).errors.empty());

   // Luma's cbuffers, its table and its static samplers in its space
   const std::vector<uint8_t> lumas = CompileBytes(
      "cbuffer LumaSettings : register(b13, space100) { float4 a; } cbuffer LumaData : register(b12, space100) { float4 b; }"
      "Texture2D noise : register(t127, space100); SamplerState linear_clamp : register(s1, space100); Texture2D game : register(t0);"
      "float4 main(float4 p : SV_Position) : SV_Target { return a + b + noise.Sample(linear_clamp, p.xy) + game.Load(int3(p.xy, 0)); }",
      "ps_5_1");
   const std::array<uint32_t, 1> noise = {127};
   reads.texture_slots = noise;
   CHECK(CheckD3D12(lumas, reads).errors == "");
   // Static samplers the mod didn't turn on aren't in the root signature
   D3D12Rules no_samplers = reads;
   no_samplers.static_samplers = false;
   CHECK(CheckD3D12(lumas, no_samplers).errors.find("s1 space100") != std::string::npos);
   // A texture it requests that it doesn't declare in the space
   const std::array<uint32_t, 1> elsewhere = {126};
   D3D12Rules missing = reads;
   missing.texture_slots = elsewhere;
   CHECK_FALSE(CheckD3D12(lumas, missing).errors.empty());
   // One of Luma's textures declared in space 0, as a recipe without `space: LUMA_SPACE` adds it
   const std::vector<uint8_t> space0 = CompileBytes(
      "Texture2D noise : register(t127); float4 main(float4 p : SV_Position) : SV_Target { return noise.Load(int3(p.xy, 0)); }", "ps_5_1");
   CHECK(CheckD3D12(space0, reads).errors.find("t127") != std::string::npos);

   // Anything else in Luma's space
   const std::vector<uint8_t> stranger = CompileBytes(
      "Texture2D t : register(t3, space100); SamplerState s : register(s7, space100);"
      "float4 main(float4 p : SV_Position) : SV_Target { return t.Sample(s, p.xy); }",
      "ps_5_1");
   const std::string errors = CheckD3D12(stranger, reads).errors;
   CHECK(errors.find("t3 space100") != std::string::npos);
   CHECK(errors.find("s7 space100") != std::string::npos);
}

TEST_CASE("Shaders: a D3D12 variant may declare space 0 registers its original doesn't, with a warning (FF7R's Motion Blur replacements)")
{
   using Shaders::Invariants::CheckD3D12;
   using Shaders::Invariants::D3D12Rules;
   // Motion Blur's original loads its inputs; the replacement samples them with s0, which the game's root signature binds for the stage
   const std::vector<uint8_t> original = CompileBytes(
      "Texture2D color : register(t0); Texture2D velocity : register(t1);"
      "float4 main(float4 p : SV_Position) : SV_Target { return color.Load(int3(p.xy, 0)) + velocity.Load(int3(p.xy, 0)); }",
      "ps_5_0");
   const std::vector<uint8_t> replacement = CompileBytes(
      "cbuffer LumaSettings : register(b13, space100) { float4 a; } cbuffer LumaData : register(b12, space100) { float4 b; }"
      "Texture2D color : register(t0); Texture2D velocity : register(t1); SamplerState linear_sampler : register(s0);"
      "float4 main(float4 p : SV_Position) : SV_Target { return a + b + color.Sample(linear_sampler, p.xy) + velocity.Load(int3(p.xy, 0)); }",
      "ps_5_1");
   const Shaders::Container::Bindings original_bindings = Shaders::Container::GetBindings(original);
   REQUIRE(original_bindings.found);
   const D3D12Rules rules = {.space = 100, .settings_slot = 13, .data_slot = 12, .reads_luma = true, .original = &original_bindings};
   const Shaders::Invariants::Findings findings = CheckD3D12(replacement, rules);
   CHECK(findings.errors.empty());
   CHECK(findings.warnings.find("s0 space0") != std::string::npos);
   // What the original declares draws no warning
   CHECK(CheckD3D12(original, {.space = 100, .original = &original_bindings}).warnings.empty());
}

#endif

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
void UpdateTestInstanceData(CB::LumaInstanceData& data, const Game::GameDeviceData& game_device_data, const FrameState::Resolutions& resolutions)
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
                             MEMBER(Settings, DisplayComposition), MEMBER(Settings, GameSettings)}));
#if DEVELOPMENT
   CHECK(offsetof(Settings, DisplayComposition) == 96);
   CHECK(offsetof(Settings, GameSettings) == 128);
#else
   CHECK(offsetof(Settings, DisplayComposition) == 64);
   CHECK(offsetof(Settings, GameSettings) == 96);
#endif
   using Composition = CB::DisplayCompositionSettings;
   CheckMembers(settings->GetMemberTypeByName("DisplayComposition"),
      std::to_array({MEMBER(Composition, PostProcessSpaceType), MEMBER(Composition, VanillaEncodingType), MEMBER(Composition, GammaCorrectionType), MEMBER(Composition, GamutMappingType), MEMBER(Composition, UIDrawType), MEMBER(Composition, EarlyDisplayEncoding), MEMBER(Composition, Stages), MEMBER(Composition, DebugFlags)}));
   using GameSettings = CB::LumaGameSettings;
   CheckMembers(settings->GetMemberTypeByName("GameSettings"),
      std::to_array({MEMBER(GameSettings, Strength), MEMBER(GameSettings, Mode), MEMBER(GameSettings, Center), MEMBER(GameSettings, Tint), MEMBER(GameSettings, Last)}));

   // Verify Luma_DisplayComposition.hlsl compiles cleanly with the runtime DisplayComposition cbuffer fields
   const std::array<std::filesystem::path, 2> composition_roots = {tests, tests / "../../Resources/Global/Shaders"};
   const std::vector<Shaders::Compiler::Define> composition_defines = {
      {"DEVELOPMENT", "1"},
      {"LUMA_SETTINGS_CB_INDEX", "b13"},
      {"LUMA_DATA_CB_INDEX", "b12"},
      {"LUMA_UI_DATA_CB_INDEX", "b11"},
      {"LUMA_COMPOSITION_CB_INDEX", "b10"},
   };
   const Shaders::Compiler::Compiled composition_compiled =
      Shaders::Compiler::Compile(composition_roots, "Luma/Display/Luma_DisplayComposition.hlsl", composition_defines, "ps_5_0", "main");
   INFO(composition_compiled.messages);
   CHECK_FALSE(composition_compiled.failed);

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
   Shaders::Store store(Api::D3D11);
   constexpr uint32_t HASH = 0x7E57AB1E;
   auto compiled = std::make_shared<Shaders::CompiledShader>();
   compiled->bytecode = {1, 2, 3};
   Publish(store, Source::File, {{.hash = HASH, .file = "Test_0x7E57AB1E.ps_5_0.hlsl", .compiled = compiled}});
   CHECK(HasVariant(store, HASH));
   CHECK(GetCompiledShader(store, HASH, Source::File) == compiled);
   CHECK_FALSE(GetCompiledShader(store, HASH, Source::Patch));
   REQUIRE(GetVariants(store, HASH).size() == 1);
   CHECK(GetVariants(store, HASH)[0].source == Source::File);

   // A failed build keeps the last version that compiled, equal bytes keep the same compiled shader
   Publish(store, Source::File, {{.hash = HASH, .compiled = nullptr, .failed = true}});
   CHECK(GetCompiledShader(store, HASH, Source::File) == compiled);
   auto same = std::make_shared<Shaders::CompiledShader>();
   same->bytecode = compiled->bytecode;
   Publish(store, Source::File, {{.hash = HASH, .compiled = same}});
   CHECK(GetCompiledShader(store, HASH, Source::File) == compiled);

   // Another source's publication keeps this one's; a higher priority comes first
   Publish(store, Source::Patch, {{.hash = HASH, .priority = FILE_PRIORITY + 1, .compiled = same}});
   REQUIRE(GetVariants(store, HASH).size() == 2);
   CHECK(GetVariants(store, HASH)[0].source == Source::Patch);
   CHECK(GetName(GetChoice(Source::Patch)) == "patch");

   Publish(store, Source::File, {});
   Publish(store, Source::Patch, {});
   CHECK_FALSE(HasVariant(store, HASH));
   CHECK(GetVariants(store, HASH).empty());
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

#ifdef LUMA_RESOURCES_DIR
TEST_CASE("DXIL: DXC preprocesses, compiles, signs, reflects and matches CBuffers.hlsl layout")
{
   const Shaders::Compiler::DxcStatus status = Shaders::Compiler::GetDxcStatus();
   REQUIRE(status.loaded);
   REQUIRE(status.signer_loaded);

   const std::filesystem::path temp_root = std::filesystem::temp_directory_path() / L"Luma2DxcTest";
   std::error_code ec;
   std::filesystem::create_directories(temp_root, ec);
   const std::filesystem::path shader_path = temp_root / L"Test_DXIL.hlsl";
   {
      std::ofstream out(shader_path, std::ios::binary | std::ios::trunc);
      out << "#include \"Includes/Core/Common.hlsl\"\n"
             "#include \"Includes/Core/Samplers.hlsl\"\n"
             "Texture2D<float4> fast_noise : register(t127, LUMA_TEXTURE_SPACE);\n"
             "float4 main(float4 pos : SV_Position) : SV_Target {\n"
             "   return float4(LumaSettings.SwapchainSize, LumaData.CustomData1, fast_noise.Load(int3(0, 0, 0)).x);\n"
             "}\n";
   }
   const std::array<std::filesystem::path, 2> roots = {temp_root, std::filesystem::path(LUMA_RESOURCES_DIR) / L"Global" / L"Shaders"};
   const std::vector<Shaders::Compiler::Define> defines = {
      {"_D3D12", "1"},
      {"DEVELOPMENT", "1"},
      {"LUMA_SETTINGS_CB_INDEX", "b13, space100"},
      {"LUMA_DATA_CB_INDEX", "b12, space100"},
      {"LUMA_TEXTURE_SPACE", "space100"},
      {"LUMA_POINT_CLAMP_SAMPLER_INDEX", "s0, space100"},
      {"LUMA_LINEAR_CLAMP_SAMPLER_INDEX", "s1, space100"},
      {"LUMA_LINEAR_WRAP_SAMPLER_INDEX", "s2, space100"},
      {"LUMA_ANISOTROPIC_WRAP_SAMPLER_INDEX", "s3, space100"},
      {"LUMA_POINT_WRAP_SAMPLER_INDEX", "s4, space100"},
   };
   const Shaders::Compiler::Preprocessed preprocessed = Shaders::Compiler::Preprocess(roots, "Test_DXIL.hlsl", defines, "ps_6_6");
   REQUIRE_FALSE(preprocessed.failed);
   CHECK_FALSE(preprocessed.text.empty());

   const Shaders::Compiler::Compiled compiled = Shaders::Compiler::Compile(roots, "Test_DXIL.hlsl", defines, "ps_6_6", "main");
   INFO(compiled.messages);
   REQUIRE_FALSE(compiled.failed);
   REQUIRE_FALSE(compiled.bytecode.empty());
   CHECK(Shaders::Container::HasChunk(compiled.bytecode, "DXIL"));
   CHECK(Shaders::Container::GetTarget(compiled.bytecode) == "ps_6_6");
   CHECK(Shaders::Compiler::IsSignedDxil(compiled.bytecode));

   // Reflection reads cbuffer & texture bindings with space 100
   const std::vector<Shaders::Compiler::Binding> cbuffers = Shaders::Compiler::ReflectConstantBufferBindings(compiled.bytecode);
   CHECK(std::ranges::any_of(cbuffers, [](const auto& b) { return b.name == "LumaSettings" && b.slot == 13 && b.space == 100; }));
   CHECK(std::ranges::any_of(cbuffers, [](const auto& b) { return b.name == "LumaData" && b.slot == 12 && b.space == 100; }));
   const std::vector<Shaders::Compiler::Binding> textures = Shaders::Compiler::ReflectTextureBindings(compiled.bytecode);
   CHECK(std::ranges::any_of(textures, [](const auto& b) { return b.name == "fast_noise" && b.slot == 127 && b.space == 100; }));

   // Cbuffer layout offsets under DXC match C++ CB::LumaGlobalSettings
   const std::vector<Shaders::Compiler::ConstantBuffer> layouts = Shaders::Compiler::ReflectConstantBuffers(compiled.bytecode);
   const auto settings_it = std::ranges::find(layouts, "LumaSettings", &Shaders::Compiler::ConstantBuffer::name);
   REQUIRE(settings_it != layouts.end());
   const auto find_var = [&](std::string_view name) {
      return std::ranges::find(settings_it->variables, name, &Shaders::Compiler::Variable::name);
   };
   REQUIRE(find_var("LumaSettings.DisplayComposition.PostProcessSpaceType") != settings_it->variables.end());
   CHECK(find_var("LumaSettings.DisplayComposition.PostProcessSpaceType")->offset == offsetof(CB::LumaGlobalSettings, DisplayComposition));

   // Disassembly works for DXIL
   CHECK(Shaders::Compiler::Disassemble(compiled.bytecode).has_value());

   // SignDxil re-signs a zeroed hash container
   Shaders::Bytecode unsigned_copy = compiled.bytecode;
   std::memset(unsigned_copy.data() + 4, 0, 16);
   CHECK_FALSE(Shaders::Compiler::IsSignedDxil(unsigned_copy));
   const std::expected<Shaders::Bytecode, std::string> resigned = Shaders::Compiler::SignDxil(unsigned_copy);
   REQUIRE(resigned.has_value());
   CHECK(Shaders::Compiler::IsSignedDxil(*resigned));

   // D3D12 invariants accept the DXIL variant
   const std::array<uint32_t, 1> tex_slots = {127};
   const Shaders::Invariants::Findings findings = Shaders::Invariants::CheckD3D12(compiled.bytecode,
      {.space = 100, .settings_slot = 13, .data_slot = 12, .reads_luma = true, .static_samplers = true, .texture_slots = tex_slots});
   CHECK(findings.errors.empty());

   std::filesystem::remove_all(temp_root, ec);
}

TEST_CASE("DXIL: D3D12 WARP accepts DXIL PSO, v1.2 bindless root signature, and AS/MS stage tracking")
{
   const Shaders::Compiler::DxcStatus status = Shaders::Compiler::GetDxcStatus();
   REQUIRE(status.loaded);
   REQUIRE(status.signer_loaded);

   const std::filesystem::path temp_root = std::filesystem::temp_directory_path() / L"Luma2DxcWarpTest";
   std::error_code ec;
   std::filesystem::create_directories(temp_root, ec);
   {
      std::ofstream vs(temp_root / L"vs.hlsl", std::ios::binary | std::ios::trunc);
      vs << "float4 main(uint id : SV_VertexID) : SV_Position { return float4(0, 0, 0, 1); }\n";
      std::ofstream ps(temp_root / L"ps.hlsl", std::ios::binary | std::ios::trunc);
      ps << "cbuffer LumaSettings : register(b13, space100) { float4 val; }\n"
            "float4 main(float4 pos : SV_Position) : SV_Target { return val; }\n";
      std::ofstream ms(temp_root / L"ms.hlsl", std::ios::binary | std::ios::trunc);
      ms << "struct V { float4 pos : SV_Position; };\n"
            "[outputtopology(\"triangle\")] [numthreads(1, 1, 1)]\n"
            "void main(out vertices V verts[3], out indices uint3 tris[1]) {\n"
            "   SetMeshOutputCounts(3, 1);\n"
            "   verts[0].pos = float4(0, 0, 0, 1); verts[1].pos = float4(1, 0, 0, 1); verts[2].pos = float4(0, 1, 0, 1);\n"
            "   tris[0] = uint3(0, 1, 2);\n"
            "}\n";
   }
   const std::array<std::filesystem::path, 1> roots = {temp_root};
   const Shaders::Compiler::Compiled vs6 = Shaders::Compiler::Compile(roots, "vs.hlsl", {}, "vs_6_0", "main");
   const Shaders::Compiler::Compiled ps6 = Shaders::Compiler::Compile(roots, "ps.hlsl", {}, "ps_6_0", "main");
   const Shaders::Compiler::Compiled ms6 = Shaders::Compiler::Compile(roots, "ms.hlsl", {}, "ms_6_5", "main");
   REQUIRE_FALSE(vs6.failed);
   REQUIRE_FALSE(ps6.failed);
   REQUIRE_FALSE(ms6.failed);

   // Container::GetStage maps Mesh shader (kind 13) to ShaderStage::Mesh
   const std::optional<Shaders::Container::Program> ms_prog = Shaders::Container::GetProgram(ms6.bytecode);
   REQUIRE(ms_prog.has_value());
   CHECK(ms_prog->dxil);
   CHECK(Shaders::Container::GetStage(*ms_prog) == ShaderStage::Mesh);

   // D3D12 WARP device: extend a v1.2 root signature with bindless flag, read layout natively, and create a DXIL PSO
   Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
   REQUIRE(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
   Microsoft::WRL::ComPtr<IDXGIAdapter> warp;
   REQUIRE(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))));
   Microsoft::WRL::ComPtr<ID3D12Device> device;
   REQUIRE(SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));

   D3D12_ROOT_SIGNATURE_DESC1 rs_desc = {.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED};
   D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned = {.Version = D3D_ROOT_SIGNATURE_VERSION_1_1, .Desc_1_1 = rs_desc};
   Microsoft::WRL::ComPtr<ID3DBlob> rs_blob;
   REQUIRE(SUCCEEDED(D3D12SerializeVersionedRootSignature(&versioned, &rs_blob, nullptr)));
   const Shaders::RootSignatures::D3D12::Extended extended = Shaders::RootSignatures::D3D12::Extend(GetBytes(rs_blob.Get()));
   REQUIRE_FALSE(extended.blob.empty());
   Microsoft::WRL::ComPtr<ID3D12RootSignature> rs;
   REQUIRE(SUCCEEDED(device->CreateRootSignature(0, extended.blob.data(), extended.blob.size(), IID_PPV_ARGS(&rs))));
   const Shaders::Database::D3D12::RootLayout* layout =
      Shaders::Database::D3D12::TrackRootSignature(GetBytes(rs_blob.Get()), rs.Get(), extended.luma_parameter, extended.table, extended.static_samplers);
   REQUIRE(layout);
   CHECK((layout->flags & D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED) != 0);

   D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
   pso_desc.pRootSignature = rs.Get();
   pso_desc.VS = {vs6.bytecode.data(), vs6.bytecode.size()};
   pso_desc.PS = {ps6.bytecode.data(), ps6.bytecode.size()};
   pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
   pso_desc.SampleMask = UINT_MAX;
   pso_desc.RasterizerState = {D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE, FALSE, 0, 0.f, 0.f, TRUE, FALSE, FALSE, 0, D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF};
   pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
   pso_desc.NumRenderTargets = 1;
   pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
   pso_desc.SampleDesc = {1, 0};
   Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
   CHECK(SUCCEEDED(device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso))));

   // Stream desc with MS stage tracks ShaderStage::Mesh in DescribePipeline and CopyDesc
   struct alignas(void*) StreamMs
   {
      D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS;
      D3D12_SHADER_BYTECODE ms;
   };
   struct alignas(void*) StreamPs
   {
      D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS;
      D3D12_SHADER_BYTECODE ps;
   };
   struct StreamDesc
   {
      StreamMs ms;
      StreamPs ps;
   } stream_data = {{D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, {ms6.bytecode.data(), ms6.bytecode.size()}},
      {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, {ps6.bytecode.data(), ps6.bytecode.size()}}};
   const D3D12_PIPELINE_STATE_STREAM_DESC stream = {sizeof(stream_data), &stream_data};
   const Shaders::Database::D3D12::PipelineDesc described = Shaders::Database::D3D12::DescribePipeline(stream);
   CHECK(described.mesh_shaders);
   CHECK(described.stages[size_t(ShaderStage::Mesh)].size() == ms6.bytecode.size());
   const std::shared_ptr<const Shaders::Database::D3D12::DescCopy> copied =
      Shaders::Database::D3D12::CopyDesc(Shaders::Database::D3D12::PipelineKind::Stream, &stream);
   REQUIRE(copied);
   CHECK(copied->stages[size_t(ShaderStage::Mesh)] != SIZE_MAX);

   std::filesystem::remove_all(temp_root, ec);
}
#endif
