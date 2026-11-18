#include <doctest/doctest.h>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include <Windows.h>
#include <Psapi.h>
#include <TlHelp32.h>
#include <winternl.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <safetyhook.hpp>

#include "Hooks/DeviceProxy.hpp"
#include "Hooks/ThirdPartyModules.hpp"
#include "Hooks/Hooks.hpp"
#include "Hooks/Scope.hpp"
#include "Hooks/Slots.h"
#include "Hooks/Vtables.hpp"
#include "System/Modules.hpp"

using Microsoft::WRL::ComPtr;
using namespace Luma;

namespace {
using TargetFunction = int (*)(int);

std::atomic<uint32_t> luma_calls = 0;
std::atomic<uint32_t> third_party_calls = 0;
TargetFunction luma_original = nullptr;
TargetFunction third_party_original = nullptr;
// Holds a thread inside Luma's detour while set
HANDLE detour_gate = nullptr;

// Hook targets: not optimized, so each has a prologue long enough for an inline hook, and its own code
#pragma optimize("", off)

__declspec(noinline) int TargetUnder(int x)
{
   return x * 3 + 1;
}

__declspec(noinline) int TargetOver(int x)
{
   return x * 5 + 2;
}

__declspec(noinline) int TargetBusy(int x)
{
   return x * 7 + 3;
}

__declspec(noinline) int TargetBypass(int x)
{
   return x * 11 + 4;
}

// What every Luma detour does: a Luma scope (or the bypass) passes the call through
__declspec(noinline) int LumaDetour(int x)
{
   if (Hooks::Scope::IsActive())
      return luma_original(x);
   luma_calls++;
   if (detour_gate)
      WaitForSingleObject(detour_gate, INFINITE);
   return luma_original(x) + 100;
}

// Plays another tool's hook (an overlay's, ReShade's)
__declspec(noinline) int ThirdPartyDetour(int x)
{
   third_party_calls++;
   return third_party_original(x) + 1000;
}

__declspec(noinline) void ThirdPartyMethod() {}

#pragma optimize("", on)

const void* Resolve(const void* function)
{
   const void* destination = Hooks::FollowJump(function);
   return destination ? destination : function;
}

void DeclareLuma(TargetFunction target, const char* name)
{
   const Hooks::Hook hook = {Hooks::AddressTarget{reinterpret_cast<void*>(target), name}, reinterpret_cast<void*>(&LumaDetour),
      reinterpret_cast<void**>(&luma_original)};
   Hooks::Declare({&hook, 1});
}

safetyhook::InlineHook HookThirdParty(TargetFunction target)
{
   auto hook = safetyhook::InlineHook::create(reinterpret_cast<void*>(target), reinterpret_cast<void*>(&ThirdPartyDetour));
   REQUIRE(hook);
   third_party_original = hook->original<TargetFunction>();
   return std::move(*hook);
}

void ResetCalls()
{
   luma_calls = 0;
   third_party_calls = 0;
}

// Read-write-execute memory to write prologues into
struct CodePage
{
   uint8_t* data = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));

   ~CodePage() { VirtualFree(data, 0, MEM_RELEASE); }
};

template <typename T>
void Write(uint8_t* at, T value)
{
   std::memcpy(at, &value, sizeof(T));
}

uint32_t CountModules()
{
   DWORD needed = 0;
   HMODULE modules[1024];
   EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed);
   return needed / sizeof(HMODULE);
}

uint32_t CountThreads()
{
   uint32_t count = 0;
   HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
   THREADENTRY32 entry = {.dwSize = sizeof(THREADENTRY32)};
   for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry))
      count += entry.th32OwnerProcessID == GetCurrentProcessId();
   CloseHandle(snapshot);
   return count;
}

struct Forwarder
{
   std::wstring module;
   std::string name;
   std::string dll; // Where it forwards, not loaded
};

// An export of a loaded System32 dll forwarded to a dll this process hasn't loaded, if there's one
std::optional<Forwarder> FindForwarderToUnloadedDll()
{
   HMODULE modules[1024];
   DWORD needed = 0;
   EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed);
   for (DWORD m = 0; m < needed / sizeof(HMODULE); m++)
   {
      const std::filesystem::path path = System::Modules::GetPath(modules[m]);
      if (System::Modules::FindSystemModule(path.filename().native()) != modules[m])
         continue;
      const auto* base = reinterpret_cast<const uint8_t*>(modules[m]);
      const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
      const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
      if (!directory.VirtualAddress)
         continue;
      const auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + directory.VirtualAddress);
      const auto* functions = reinterpret_cast<const DWORD*>(base + exports->AddressOfFunctions);
      const auto* names = reinterpret_cast<const DWORD*>(base + exports->AddressOfNames);
      const auto* ordinals = reinterpret_cast<const WORD*>(base + exports->AddressOfNameOrdinals);
      for (DWORD i = 0; i < exports->NumberOfNames; i++)
      {
         const DWORD rva = functions[ordinals[i]];
         if (rva < directory.VirtualAddress || rva >= directory.VirtualAddress + directory.Size)
            continue;
         const std::string_view forwarder = reinterpret_cast<const char*>(base + rva);
         const size_t dot = forwarder.rfind('.');
         if (dot == std::string_view::npos || forwarder.substr(dot + 1).starts_with('#'))
            continue;
         const std::string dll = std::string(forwarder.substr(0, dot)) + ".dll";
         if (dll.starts_with("api-ms-") || dll.starts_with("ext-ms-") || GetModuleHandleA(dll.c_str()))
            continue;
         return Forwarder{path.filename().native(), reinterpret_cast<const char*>(base + names[i]), dll};
      }
   }
   return std::nullopt;
}

using LdrLockLoaderLockFunction = NTSTATUS(NTAPI*)(ULONG flags, ULONG* state, ULONG_PTR* cookie);
using LdrUnlockLoaderLockFunction = NTSTATUS(NTAPI*)(ULONG flags, ULONG_PTR cookie);
} // namespace

// The D3D11 GetDevice hooks use one slot for children and contexts
TEST_CASE("Hooks: D3D11 contexts and children have GetDevice in the same slot")
{
   CHECK(LumaSlot_ID3D11DeviceContext_GetDevice == LumaSlot_ID3D11DeviceChild_GetDevice);
}

TEST_CASE("Hooks: the Luma scope nests per thread")
{
   CHECK_FALSE(Hooks::Scope::IsActive());
   {
      Hooks::Scope::Guard outer;
      {
         Hooks::Scope::Guard inner;
         CHECK(Hooks::Scope::IsActive());
      }
      CHECK(Hooks::Scope::IsActive());
   }
   CHECK_FALSE(Hooks::Scope::IsActive());
}

TEST_CASE("Hooks: a jump at a target is followed to the hooker's code (E9, FF 25, mov rax and jmp rax)")
{
   CodePage page;
   const void* detour = Resolve(reinterpret_cast<const void*>(&ThirdPartyDetour));

   // E9 to a relay, the relay FF 25 to the detour, as SafetyHook and MinHook do
   uint8_t* relay = page.data + 0x100;
   page.data[0] = 0xE9;
   Write<int32_t>(page.data + 1, int32_t(relay - (page.data + 5)));
   relay[0] = 0xFF;
   relay[1] = 0x25;
   Write<int32_t>(relay + 2, 0);
   Write<const void*>(relay + 6, detour);
   CHECK(Hooks::FollowJump(page.data) == detour);
   CHECK(Hooks::FollowJump(relay) == detour);

   // mov rax, imm64; jmp rax
   uint8_t* absolute = page.data + 0x200;
   absolute[0] = 0x48;
   absolute[1] = 0xB8;
   Write<const void*>(absolute + 2, detour);
   absolute[10] = 0xFF;
   absolute[11] = 0xE0;
   CHECK(Hooks::FollowJump(absolute) == detour);

   // Code that isn't a jump
   uint8_t* plain = page.data + 0x300;
   plain[0] = 0x8B;
   plain[1] = 0xC1;
   plain[2] = 0xC3;
   CHECK(Hooks::FollowJump(plain) == nullptr);
}

TEST_CASE("Hooks: installed on top of another hooker, both run, and unhooking gives the target back to it")
{
   ResetCalls();
   Hooks::Scope::SetBypass(false);
   safetyhook::InlineHook third_party = HookThirdParty(&TargetUnder);
   DeclareLuma(&TargetUnder, "TargetUnder");

   const std::optional<Hooks::ChainState> state = Hooks::GetChainState(reinterpret_cast<const void*>(&LumaDetour));
   REQUIRE(state);
   CHECK(state->chain == Hooks::Chain::Intact);
   CHECK(state->detoured_by == System::Modules::GetName(reinterpret_cast<const void*>(&ThirdPartyDetour)));
   CHECK(Hooks::Verify().detoured_before >= 1);

   // Luma outermost, then the other hooker, then the function
   CHECK(TargetUnder(2) == 7 + 1000 + 100);
   CHECK(luma_calls == 1);
   CHECK(third_party_calls == 1);

   // Luma's patch is first: its bytes go, which were the other hooker's jump. Every hook goes, those of the tests before too.
   const Hooks::UninstallReport report = Hooks::UninstallAll(true);
   CHECK(report.removed >= 1);
   CHECK(report.neutralised == 0);
   CHECK(report.kept == 0);
   CHECK_FALSE(report.code_in_use);
   CHECK(TargetUnder(2) == 7 + 1000);
   CHECK(luma_calls == 1);

   third_party.reset();
   CHECK(TargetUnder(2) == 7);
}

TEST_CASE("Hooks: a hooker installed above Luma keeps working when Luma unhooks, and after it unhooks too")
{
   ResetCalls();
   Hooks::Scope::SetBypass(false);
   DeclareLuma(&TargetOver, "TargetOver");
   safetyhook::InlineHook third_party = HookThirdParty(&TargetOver);

   const Hooks::Verification verification = Hooks::Verify();
   CHECK(verification.chained == 1);
   const std::optional<Hooks::ChainState> state = Hooks::GetChainState(reinterpret_cast<const void*>(&LumaDetour));
   REQUIRE(state);
   CHECK(state->chain == Hooks::Chain::Chained);
   CHECK(state->chained_by == System::Modules::GetName(reinterpret_cast<const void*>(&ThirdPartyDetour)));

   // The other hooker outermost
   CHECK(TargetOver(2) == 12 + 100 + 1000);
   CHECK(luma_calls == 1);

   // Its trampoline copied Luma's jump: Luma's patch stays, leading straight to the original
   Hooks::Scope::SetBypass(true);
   const Hooks::UninstallReport report = Hooks::UninstallAll(true);
   Hooks::Scope::SetBypass(false);
   CHECK(report.removed == 0);
   CHECK(report.neutralised == 1);
   CHECK(report.kept == 0);
   CHECK(Hooks::GetCounts().neutralised >= 1);
   CHECK(TargetOver(2) == 12 + 1000);
   CHECK(luma_calls == 1);
   CHECK(third_party_calls == 2);

   // The other hooker restores what it found, Luma's jump, which still works without Luma
   third_party.reset();
   CHECK(TargetOver(2) == 12);
   CHECK(luma_calls == 1);

   // And Luma can hook it again
   DeclareLuma(&TargetOver, "TargetOver");
   CHECK(TargetOver(2) == 12 + 100);
   CHECK(luma_calls == 2);
   CHECK(Hooks::UninstallAll(true).removed == 1);
   CHECK(TargetOver(2) == 12);
}

TEST_CASE("Hooks: a thread inside a detour keeps its trampoline and Luma's code from being freed")
{
   ResetCalls();
   Hooks::Scope::SetBypass(false);
   DeclareLuma(&TargetBusy, "TargetBusy");
   detour_gate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
   std::atomic<int> result = 0;
   std::thread caller([&result] { result = TargetBusy(2); });
   while (luma_calls == 0)
      Sleep(1);

   // Under the loader lock (DllMain's DLL_PROCESS_DETACH) this doesn't wait long: the trampoline is left, valid, for the thread
   const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
   const auto lock_loader = reinterpret_cast<LdrLockLoaderLockFunction>(GetProcAddress(ntdll, "LdrLockLoaderLock"));
   const auto unlock_loader = reinterpret_cast<LdrUnlockLoaderLockFunction>(GetProcAddress(ntdll, "LdrUnlockLoaderLock"));
   ULONG_PTR cookie = 0;
   REQUIRE(lock_loader(0, nullptr, &cookie) >= 0);
   Hooks::Scope::SetBypass(true);
   const Hooks::UninstallReport report = Hooks::UninstallAll(false);
   Hooks::Scope::SetBypass(false);
   unlock_loader(0, cookie);
   CHECK(report.removed == 1);
   CHECK(report.leaked_trampolines == 1);
   CHECK(report.code_in_use);
   CHECK(report.NeedsModule());

   SetEvent(detour_gate);
   caller.join();
   CloseHandle(std::exchange(detour_gate, nullptr));
   CHECK(result == 17 + 100);
   CHECK(TargetBusy(2) == 17);
}

TEST_CASE("Hooks: bypassed, a detour only calls its original")
{
   ResetCalls();
   Hooks::Scope::SetBypass(false);
   DeclareLuma(&TargetBypass, "TargetBypass");
   CHECK(TargetBypass(1) == 15 + 100);
   CHECK(luma_calls == 1);

   Hooks::Scope::SetBypass(true);
   CHECK(Hooks::Scope::IsActive());
   CHECK(TargetBypass(1) == 15);
   CHECK(luma_calls == 1);

   Hooks::Scope::SetBypass(false);
   CHECK_FALSE(Hooks::Scope::IsActive());
   CHECK(Hooks::UninstallAll(true).removed == 1);
}

TEST_CASE("Hooks: a forwarded export whose dll isn't loaded waits for it, without loading anything under the loader lock")
{
   const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
   const auto lock_loader = reinterpret_cast<LdrLockLoaderLockFunction>(GetProcAddress(ntdll, "LdrLockLoaderLock"));
   const auto unlock_loader = reinterpret_cast<LdrUnlockLoaderLockFunction>(GetProcAddress(ntdll, "LdrUnlockLoaderLock"));
   REQUIRE(lock_loader);
   REQUIRE(unlock_loader);
   const std::optional<Forwarder> forwarder = FindForwarderToUnloadedDll();

   CHECK_FALSE(System::Modules::IsLoaderLockHeld());
   ULONG_PTR cookie = 0;
   REQUIRE(lock_loader(0, nullptr, &cookie) >= 0);
   CHECK(System::Modules::IsLoaderLockHeld());
   const uint32_t modules = CountModules();
   const uint32_t threads = CountThreads();
   const Hooks::Counts before = Hooks::GetCounts();
   if (forwarder)
   {
      // Its detour is never reached: the hook is removed before the dll loads
      const Hooks::Hook hook = {Hooks::ExportTarget{forwarder->module.c_str(), forwarder->name.c_str()}, reinterpret_cast<void*>(&ThirdPartyMethod),
         reinterpret_cast<void**>(&third_party_original)};
      Hooks::Declare({&hook, 1});
      Hooks::OnModuleLoaded(forwarder->module);
      Hooks::InstallPending();
   }
   const Hooks::Counts after = Hooks::GetCounts();
   const uint32_t modules_after = CountModules();
   const uint32_t threads_after = CountThreads();
   unlock_loader(0, cookie);
   CHECK_FALSE(System::Modules::IsLoaderLockHeld());

   CHECK(modules_after == modules);
   CHECK(threads_after == threads);
   if (forwarder)
   {
      // Pending: neither installed nor failed, and its dll still isn't loaded
      CHECK(after.declared == before.declared + 1);
      CHECK(after.installed == before.installed);
      CHECK(after.failed == before.failed);
      CHECK(GetModuleHandleA(forwarder->dll.c_str()) == nullptr);
      bool waits_for_dll = false;
      CHECK(System::Modules::FindExport(System::Modules::FindSystemModule(forwarder->module), forwarder->name.c_str(), &waits_for_dll) == nullptr);
      CHECK(waits_for_dll);
   }
   else
   {
      MESSAGE("No System32 export forwards to a dll this process hasn't loaded: only the loader lock part ran");
   }
   Hooks::UninstallAll(true);
}

TEST_CASE("Vtables: a method patched in from another dll is third-party, one from the API's dlls is a variant")
{
   const HMODULE d3d11 = GetModuleHandleW(L"d3d11.dll");
   REQUIRE(d3d11);
   void* api_a = GetProcAddress(d3d11, "D3D11CreateDevice");
   void* api_b = GetProcAddress(d3d11, "D3D11CreateDeviceAndSwapChain");
   REQUIRE(api_a);
   REQUIRE(api_b);
   void* third_party = reinterpret_cast<void*>(&ThirdPartyMethod);
   CHECK(Hooks::Vtables::ClassifyMethod(api_a, api_a) == Hooks::Vtables::Method::Same);
   CHECK(Hooks::Vtables::ClassifyMethod(api_a, api_b) == Hooks::Vtables::Method::Variant);
   CHECK(Hooks::Vtables::ClassifyMethod(api_a, third_party) == Hooks::Vtables::Method::ThirdParty);

   // Objects whose vtables differ from the first one's: a patched slot still matches, a slot of the API's doesn't. Recorded on the
   // last context variant, which nothing else uses here.
   struct FakeObject
   {
      void* const* vtable;
   };

   static const auto query_interface = [](IUnknown* self, REFIID riid, void** result) -> HRESULT {
      *result = riid == __uuidof(IUnknown) ? self : nullptr;
      return *result ? S_OK : E_NOINTERFACE;
   };
   static const auto add_reference = [](IUnknown*) -> ULONG { return 1; };
   void* const iunknown[] = {reinterpret_cast<void*>(+query_interface), reinterpret_cast<void*>(+add_reference), reinterpret_cast<void*>(+add_reference)};
   void* const first[] = {iunknown[0], iunknown[1], iunknown[2], api_a, api_a, api_b};
   void* const patched[] = {iunknown[0], iunknown[1], iunknown[2], api_a, third_party, api_b};
   void* const variant[] = {iunknown[0], iunknown[1], iunknown[2], api_a, api_b, api_b};
   FakeObject first_object = {first};
   FakeObject patched_object = {patched};
   FakeObject variant_object = {variant};
   const Hooks::Vtables::InterfaceVersion versions[] = {{__uuidof(IUnknown), "Fake", 6}};
   const Hooks::Vtables::Interface slot = Hooks::Vtables::Interface::D3D11ContextVariant3;
   REQUIRE(Hooks::Vtables::Set(slot, reinterpret_cast<IUnknown*>(&first_object), versions));
   CHECK(Hooks::Vtables::Matches(slot, reinterpret_cast<IUnknown*>(&patched_object)));
   CHECK(Hooks::Vtables::Set(slot, reinterpret_cast<IUnknown*>(&patched_object), versions));
   CHECK_FALSE(Hooks::Vtables::Matches(slot, reinterpret_cast<IUnknown*>(&variant_object)));
   CHECK_FALSE(Hooks::Vtables::Set(slot, reinterpret_cast<IUnknown*>(&variant_object), versions));
   // The hooks stay on the recorded methods
   CHECK(Hooks::Vtables::GetMethod(slot, 4) == api_a);

   // A wrapper object's IUnknown is its own too (in another dll than the recorded one's): it's another instance, not patched slots
   const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
   REQUIRE(kernel32);
   void* const other[] = {GetProcAddress(kernel32, "GetTickCount"), GetProcAddress(kernel32, "GetCurrentThreadId"), GetProcAddress(kernel32, "GetCurrentProcessId")};
   REQUIRE((other[0] && other[1] && other[2]));
   void* const wrapper[] = {other[0], other[1], other[2], third_party, third_party, api_b};
   FakeObject wrapper_object = {wrapper};
   CHECK_FALSE(Hooks::Vtables::Matches(slot, reinterpret_cast<IUnknown*>(&wrapper_object)));
}

TEST_CASE("ThirdPartyModules: a return address in an overlay's code is found by its range")
{
   static const uint8_t code[64] = {};
   CHECK(Hooks::ThirdPartyModules::Find(code + 8).empty());
   Hooks::ThirdPartyModules::Add("TestOverlay64.dll", code, sizeof(code));
   CHECK(Hooks::ThirdPartyModules::Find(code + 8) == "TestOverlay64.dll");
   CHECK(Hooks::ThirdPartyModules::Find(code + sizeof(code)).empty());
   Hooks::ThirdPartyModules::OnModuleUnloaded(code);
   CHECK(Hooks::ThirdPartyModules::Find(code + 8).empty());
}
