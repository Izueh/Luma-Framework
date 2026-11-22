#include <doctest/doctest.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <dxgiformat.h>

#include <glaze/core/context.hpp>

#include "Utils/Format.hpp"
#include "Utils/Hash.hpp"
#include "Utils/Json.hpp"
#include "Utils/Strings.hpp"
#include "Utils/Toml.hpp"

using namespace Luma;

namespace {
std::span<const uint8_t> Bytes(std::string_view text)
{
   return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}
} // namespace

// Reflected by glaze, which can't reflect types with internal linkage (an anonymous namespace)
namespace UtilsTests {
struct Reply
{
   bool ok = true;
   int64_t count = 3;
   std::string name = "quote\" slash\\ newline\n";
   double ratio = 0.5;
};
} // namespace UtilsTests

using UtilsTests::Reply;

TEST_CASE("Format: DXGI format names follow dxgiformat.h")
{
   CHECK(Utils::Format::GetName(DXGI_FORMAT_UNKNOWN) == "UNKNOWN");
   CHECK(Utils::Format::GetName(DXGI_FORMAT_R16G16B16A16_FLOAT) == "R16G16B16A16_FLOAT");
   CHECK(Utils::Format::GetName(DXGI_FORMAT_R10G10B10A2_UNORM) == "R10G10B10A2_UNORM");
   CHECK(Utils::Format::GetName(DXGI_FORMAT_R11G11B10_FLOAT) == "R11G11B10_FLOAT");
   CHECK(Utils::Format::GetName(DXGI_FORMAT_D24_UNORM_S8_UINT) == "D24_UNORM_S8_UINT");
   CHECK(Utils::Format::GetName(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) == "B8G8R8A8_UNORM_SRGB");
   CHECK(Utils::Format::GetName(DXGI_FORMAT_BC7_UNORM_SRGB) == "BC7_UNORM_SRGB");
   CHECK(Utils::Format::GetName(DXGI_FORMAT_B4G4R4A4_UNORM) == "B4G4R4A4_UNORM");
   CHECK(Utils::Format::GetName(DXGI_FORMAT_P208) == "P208");
   CHECK(Utils::Format::GetName(DXGI_FORMAT_V408) == "V408");
   CHECK(Utils::Format::GetName(120) == "UNKNOWN");
}

TEST_CASE("Hash: CRC32 matches the standard check values")
{
   CHECK(Utils::Hash::CRC32(Bytes("")) == 0x00000000u);
   CHECK(Utils::Hash::CRC32(Bytes("123456789")) == 0xCBF43926u);
   CHECK(Utils::Hash::CRC32(Bytes("The quick brown fox jumps over the lazy dog")) == 0x414FA339u);
}

TEST_CASE("Hash: CRC32 can be computed incrementally")
{
   const uint32_t first_half = Utils::Hash::CRC32(Bytes("12345"));
   CHECK(Utils::Hash::CRC32(Bytes("6789"), first_half) == 0xCBF43926u);
}

TEST_CASE("Hash: name hashes are evaluated at compile time and match the runtime version")
{
   constexpr uint64_t compile_time_hash = "luma_settings"_h;
   static_assert(compile_time_hash == Utils::Hash::Name("luma_settings"));
   static_assert(""_h == 0xCBF29CE484222325ull); // FNV-1a offset basis
   static_assert("a"_h == 0xAF63DC4C8601EC8Cull);
   CHECK("luma_settings"_h != "luma_data"_h);
}

TEST_CASE("Strings: conversions and comparisons")
{
   CHECK(Utils::Strings::ToUtf8(L"Final Fantasy®") == "Final Fantasy\xC2\xAE");
   CHECK(Utils::Strings::ToUtf8(std::filesystem::path(L"a\\b.dll")) == "a\\b.dll");
   CHECK(Utils::Strings::EqualsIgnoreCase(L"D3D12.dll", L"d3d12.DLL"));
   CHECK_FALSE(Utils::Strings::EqualsIgnoreCase(L"d3d11.dll", L"d3d12.dll"));
}

TEST_CASE("Json: reflected structs round trip with escaping")
{
   const std::string text = Utils::Json::Write(Reply{});
   CHECK(text == R"({"ok":true,"count":3,"name":"quote\" slash\\ newline\n","ratio":0.5})");

   Reply read{.ok = false, .count = 0, .name = {}, .ratio = 0};
   REQUIRE(Utils::Json::Read(text, read));
   CHECK(read.ok);
   CHECK(read.count == 3);
   CHECK(read.name == Reply{}.name);

   std::string error;
   CHECK_FALSE(Utils::Json::Read("{\"ok\":", read, &error));
   CHECK_FALSE(error.empty());
}

TEST_CASE("Toml: documents keep their tables, order and value types")
{
   constexpr std::string_view text = R"(# Comment
Root = 1

[Display]
Count = 99
Ratio = 0.5
Name = "x"
List = [1, 2]

[Display.Nested]
Flag = true

[Other]
Kept = true
)";
   Utils::Toml::Table document;
   REQUIRE(Utils::Toml::Read(text, document));
   REQUIRE(document["Display"].is_object());
   Utils::Toml::Table& display = document["Display"].get_object();
   CHECK(std::get<int64_t>(display["Count"].data) == 99);
   CHECK(std::get<double>(display["Ratio"].data) == 0.5);
   CHECK(display["Nested"].is_object());

   // Tables are written as sections after the values of their parent, in their original order
   CHECK(Utils::Toml::Write(document) == R"(Root = 1

[Display]
Count = 99
Ratio = 0.5
Name = "x"
List = [1, 2]

[Display.Nested]
Flag = true

[Other]
Kept = true
)");
}

TEST_CASE("Toml: malformed documents fail and keys are quoted when needed")
{
   Utils::Toml::Table document;
   std::string error;
   CHECK_FALSE(Utils::Toml::Read("[Display\nCount = ", document, &error));
   CHECK_FALSE(error.empty());

   document.clear();
   document["needs quotes"].data = true;
   CHECK(Utils::Toml::Write(document) == "\"needs quotes\" = true\n");
}

#include <Windows.h>
#include <Psapi.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include "System/Modules.hpp"

TEST_CASE("Dll exports: every dxgi export reaches the System32 dxgi.dll")
{
   // Tests.exe links the same exports as a game dll
   HMODULE self = GetModuleHandleW(nullptr);
   HMODULE dxgi = Luma::System::Modules::FindSystemModule(L"dxgi.dll");
   REQUIRE(dxgi);
   // Wine/DXVK-implemented exports are Luma functions that call into the System32 dxgi.dll
   for (const char* name : {"CreateDXGIFactory", "CreateDXGIFactory1", "CreateDXGIFactory2", "DXGID3D10CreateDevice", "DXGID3D10RegisterLayers", "DXGIDeclareAdapterRemovalSupport", "DXGIGetDebugInterface1"})
   {
      CAPTURE(name);
      CHECK(GetProcAddress(self, name) != nullptr);
   }
   // Same ordinals as the system dll
   CHECK(GetProcAddress(self, MAKEINTRESOURCEA(11)) == GetProcAddress(self, "CreateDXGIFactory1"));
   // Calling through the exported wrapper succeeds
   const auto create_factory1 = reinterpret_cast<HRESULT(WINAPI*)(REFIID, void**)>(GetProcAddress(self, "CreateDXGIFactory1"));
   REQUIRE(create_factory1 != nullptr);
   Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
   CHECK(SUCCEEDED(create_factory1(IID_PPV_ARGS(&factory))));
   CHECK(factory != nullptr);

   // Windows-only exports stay PE forwarders to the System32 dxgi.dll
   for (const char* name : {"CompatValue", "CompatString", "SetAppCompatStringPointer"})
   {
      CAPTURE(name);
      void* forwarded = reinterpret_cast<void*>(GetProcAddress(self, name));
      CHECK(forwarded);
      CHECK(forwarded == Luma::System::Modules::FindExport(dxgi, name));
   }

   // The full path forwarders and wrappers reuse the loaded dxgi instead of loading a second copy
   HMODULE modules[1024];
   DWORD size = 0;
   REQUIRE(K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &size));
   int dxgi_count = 0;
   for (DWORD i = 0; i < size / sizeof(HMODULE); i++)
      dxgi_count += Luma::System::Modules::GetPath(modules[i]).filename() == L"dxgi.dll" ? 1 : 0;
   CHECK(dxgi_count == 1);
}

TEST_CASE("Modules: exports are read from the export table like the loader resolves them")
{
   HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
   for (const char* name : {"GetCurrentProcessId", "CreateFileW", "LoadLibraryW"})
   {
      CAPTURE(name);
      CHECK(Luma::System::Modules::FindExport(kernel32, name) == reinterpret_cast<void*>(GetProcAddress(kernel32, name)));
   }
   // Forwarded to ntdll's RtlAllocateHeap
   CHECK(Luma::System::Modules::FindExport(kernel32, "HeapAlloc") == reinterpret_cast<void*>(GetProcAddress(kernel32, "HeapAlloc")));
   CHECK(Luma::System::Modules::FindExport(kernel32, "HeapAlloc") == reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlAllocateHeap")));
   CHECK_FALSE(Luma::System::Modules::FindExport(kernel32, "LumaNoSuchExport"));

   // By ordinal
   HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
   REQUIRE(dxgi);
   CHECK(Luma::System::Modules::FindExport(dxgi, MAKEINTRESOURCEA(11)) == reinterpret_cast<void*>(GetProcAddress(dxgi, MAKEINTRESOURCEA(11))));
   CHECK_FALSE(Luma::System::Modules::FindExport(dxgi, MAKEINTRESOURCEA(60000)));
}

// Luma2.Common.props renames Luma's glaze namespace: DXP's library carries another glaze version, and under one name the linker mixed their glz::context
namespace Luma::Tests {
// Not in an anonymous namespace: glaze can't reflect types with internal linkage
struct GlazeNamed
{
   int first_field = 1;
};
} // namespace Luma::Tests

TEST_CASE("Utils: Luma's glaze has a namespace of its own, apart from a library's")
{
   CHECK(std::string_view(typeid(glz::context).name()).find("luma_glz::") != std::string_view::npos);
   // Reflection still names members (glaze finds them through its namespace's name in __FUNCSIG__)
   CHECK(Utils::Json::Write(Tests::GlazeNamed{}).find("first_field") != std::string::npos);
}
