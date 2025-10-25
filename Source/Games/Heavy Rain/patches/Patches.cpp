#include "Patches.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <ShlObj_core.h> // winnt

#include "..\..\Core\utils\system.h"

// Unfinished and unnecessary trampoline path, given that we can successfully allocate the target float within 2GB anyway
#define USE_TRAMPOLINE 0

namespace Patches
{
   static inline constexpr float default_aspect_ratio = 16.f / 9.f;
   static inline float aspect_ratio = default_aspect_ratio;

   static float* aspect_ratio_float_ptr = nullptr;
#if USE_TRAMPOLINE
   static std::byte* pattern_3_trampoline = nullptr;
#endif

   constexpr size_t pattern_3_instruction_set_length = 8; // We replace 8 instructions

   static std::vector<std::byte*> pattern_1_addresses;
   static std::vector<std::byte*> pattern_2_addresses;
   static std::vector<std::byte*> pattern_3_addresses;

   // -----------------------------------------------------------------------------
   // This code is based on findings by Rose
   // Original release: https://community.pcgamingwiki.com/files/file/1327-heavy-rain-ultrawide-1610-and-narrower-fixes/
   // -----------------------------------------------------------------------------

   void FindAddresses()
   {
      // Note: these work on the 2025 version of the game on Steam and GoG.

      assert(pattern_1_addresses.empty());

      HMODULE module_handle = GetModuleHandle(nullptr); // Handle to the current executable
      auto dos_header = reinterpret_cast<PIMAGE_DOS_HEADER>(module_handle);
      auto nt_headers = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<std::byte*>(module_handle) + dos_header->e_lfanew);

      std::byte* base = reinterpret_cast<std::byte*>(module_handle);
      std::size_t section_size = nt_headers->OptionalHeader.SizeOfImage;

      std::vector<std::byte> pattern;

      // There seems to be only one of these
      // This seems to be in the "rdata" section
      pattern = { std::byte{0x38}, std::byte{0x8E}, std::byte{0xE3}, std::byte{0x3F} }; // Just 16.f/9.f in memory
      pattern_1_addresses = System::ScanMemoryForPattern(base, section_size, pattern);

      // TODO: try to patch the other 0x39 version too?
      pattern = { std::byte{0x39}, std::byte{0x8E}, std::byte{0xE3}, std::byte{0x3F} };
      static std::vector<std::byte*> pattern_1b_addresses;
      pattern_1b_addresses = System::ScanMemoryForPattern(base, section_size, pattern);

      // There seems to be only one of these
      // This is probably also in the the "rdata" section
      pattern = { std::byte{0x00}, std::byte{0x00}, std::byte{0x10}, std::byte{0x41}, std::byte{0x00}, std::byte{0x00}, std::byte{0x20}, std::byte{0x41}, std::byte{0x00}, std::byte{0x00}, std::byte{0x50} }; // Projection matrix aspect ratio dependent stuff? 9, 10, ... in float
      pattern_2_addresses = System::ScanMemoryForPattern(base, section_size, pattern);

      // There seems to be only one of these
      // This is probably also in the the "text" (code) section
      pattern = { std::byte{0xF3}, std::byte{0x0F}, std::byte{0x10}, std::byte{0xB0}, std::byte{0x08}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0xE8} };
      pattern_3_addresses = System::ScanMemoryForPattern(base, section_size, pattern);
   }
   
   void AllocateData()
   {
      assert(!aspect_ratio_float_ptr);

      // Just do the first one
      if (!pattern_3_addresses.empty())
      {
#if !USE_TRAMPOLINE
         auto pattern_address = pattern_3_addresses[0];

         aspect_ratio_float_ptr = static_cast<float*>((System::VirtualAllocNear(pattern_address + pattern_3_instruction_set_length, sizeof(float), PAGE_READWRITE)));
         assert(aspect_ratio_float_ptr);

         uintptr_t rip_after = reinterpret_cast<uintptr_t>(pattern_address) + pattern_3_instruction_set_length;

         int64_t rel32test = reinterpret_cast<uintptr_t>(aspect_ratio_float_ptr) - rip_after;
         assert(rel32test <= INT32_MAX && rel32test >= INT32_MIN);
#else
         assert(!pattern_3_trampoline);
         auto pattern_address = pattern_3_addresses[0];

         // Allocate trampoline
         constexpr size_t trampoline_size = 64; // 16 bytes is enough // TODO: reduce
         pattern_3_trampoline = static_cast<std::byte*>(System::VirtualAllocNear(pattern_address + 5, trampoline_size, PAGE_EXECUTE_READWRITE));
         if (!pattern_3_trampoline)
         {
            assert(false);
            return;
         }

         // Allocate it within the trampoline instead of with "PAGE_READWRITE" as there would be no guarantee it'd end up within 2GB
         aspect_ratio_float_ptr = reinterpret_cast<float*>(pattern_3_trampoline + 32);

         // Build trampoline:
         // 1) MOVSS xmm6, [RIP+disp32]
         // 2) Jump back to original code after overwritten bytes

         uintptr_t rip_after = reinterpret_cast<uintptr_t>(pattern_address) + pattern_3_instruction_set_length;

         // Calculate displacement
         int32_t displacement = reinterpret_cast<uint8_t*>(aspect_ratio_float_ptr) - (reinterpret_cast<uint8_t*>(pattern_3_trampoline) + 5); // 5 is the number of instructions below
         int64_t displacement2 = reinterpret_cast<uint8_t*>(aspect_ratio_float_ptr) - (reinterpret_cast<uint8_t*>(pattern_3_trampoline) + 5);
         assert(displacement2 <= INT32_MAX && displacement2 >= INT32_MIN);
         // 7 = length of MOVSS RIP-relative instruction

         std::vector<uint8_t> trampoline_patch;

         // Keep the previous 3 instructions
         trampoline_patch.push_back(0xF3);
         trampoline_patch.push_back(0x0F);
         trampoline_patch.push_back(0x10);
         trampoline_patch.push_back(0x35); // Original "0xB0" was not valid for RIP-relative addressing
         trampoline_patch.insert(trampoline_patch.end(),
            reinterpret_cast<uint8_t*>(&displacement),
            reinterpret_cast<uint8_t*>(&displacement) + sizeof(displacement));

         // From JMP to the original code (the next address after the last instruction we replaced) after overwritten bytes
         // Jumps always start counting offsets from the next instruction.
         uintptr_t jmp_back_addr = rip_after;
         int32_t jmp_rel = static_cast<int32_t>(jmp_back_addr - (reinterpret_cast<uintptr_t>(pattern_3_trampoline) + trampoline_patch.size() + 5)); // 5 is the length of the instructions below
         trampoline_patch.push_back(0xE9); // JMP rel32
         trampoline_patch.insert(trampoline_patch.end(),
            reinterpret_cast<uint8_t*>(&jmp_rel),
            reinterpret_cast<uint8_t*>(&jmp_rel) + sizeof(jmp_rel));

         // Apply trampoline
         std::memcpy(pattern_3_trampoline, trampoline_patch.data(), trampoline_patch.size());
#endif
      }
   }

   bool Patch(float target_aspect_ratio = aspect_ratio, bool restore = false)
   {
      bool failed = pattern_1_addresses.empty() || pattern_2_addresses.empty() || pattern_3_addresses.empty();

      if (restore)
      {
         target_aspect_ratio = default_aspect_ratio;
      }

      for (auto pattern_address : pattern_1_addresses)
      {
         DWORD old_protect;
         BOOL success = VirtualProtect(pattern_address, sizeof(float), PAGE_EXECUTE_READWRITE, &old_protect);
         if (!success) { failed = true; continue; }

         std::memcpy(pattern_address, &target_aspect_ratio, sizeof(float));
         DWORD temp_protect;
         VirtualProtect(pattern_address, sizeof(float), old_protect, &temp_protect);
      }

      for (auto pattern_address : pattern_2_addresses)
      {
         DWORD old_protect;
         BOOL success = VirtualProtect(pattern_address, sizeof(float), PAGE_EXECUTE_READWRITE, &old_protect);
         if (!success) { failed = true; continue; }

         constexpr float original_value = 9.f; // Likely just the height of the default aspect ratio (16:9)
         const float custom_value = original_value * default_aspect_ratio / target_aspect_ratio; // This controls the size of the composition/viewport

         std::memcpy(pattern_address, &custom_value, sizeof(float)); // We only replace the first float of the pattern
         DWORD temp_protect;
         VirtualProtect(pattern_address, sizeof(float), old_protect, &temp_protect);
      }

      // The field of view (directly affects it)
      if (!restore && aspect_ratio_float_ptr)
      {
         *aspect_ratio_float_ptr = default_aspect_ratio;
      }

      for (auto pattern_address : pattern_3_addresses)
      {
#if !USE_TRAMPOLINE
         DWORD old_protect;
         BOOL success = VirtualProtect(pattern_address, pattern_3_instruction_set_length, PAGE_EXECUTE_READWRITE, &old_protect);
         if (!success) { failed = true; continue; }

         std::vector<uint8_t> patch;
         // Build instructions
         if (restore || !aspect_ratio_float_ptr)
         {
            patch = { 0xF3, 0x0F, 0x10, 0xB0, 0x08, 0x01, 0x00, 0x00 };
            // We skip the "0xE8" at the end, that was just to make the pattern search more reliable
         }
         else
         {
            // Address after instruction (next RIP)
            uintptr_t rip_after = reinterpret_cast<uintptr_t>(pattern_address) + pattern_3_instruction_set_length;

            // Calculate displacement
            int32_t rel32 = reinterpret_cast<uintptr_t>(aspect_ratio_float_ptr) - rip_after;

            // This will replace the address from which some aspect ratio float is loaded, it needs to be forced to 16:9 even when the game is in other aspect ratios

            // New MOVSS (RIP-relative, with disp32)
            patch.push_back(0xF3);
            patch.push_back(0x0F);
            patch.push_back(0x10);
            patch.push_back(0x35);
            patch.insert(patch.end(),
               reinterpret_cast<uint8_t*>(&rel32),
               reinterpret_cast<uint8_t*>(&rel32) + sizeof(rel32)
            );
         }

         // Apply
         std::memcpy(pattern_address, patch.data(), patch.size());

         // Restore state
         DWORD temp_protect;
         success = VirtualProtect(pattern_address, pattern_3_instruction_set_length, old_protect, &temp_protect);
         assert(success);

         FlushInstructionCache(GetCurrentProcess(), pattern_address, pattern_3_instruction_set_length);
#else
         if (pattern_3_trampoline)
         {
            // Overwrite original instruction with JMP to trampoline
            constexpr size_t instructions_num = 5; // JMP + address
            DWORD old_protect;
            BOOL success = VirtualProtect(pattern_address, instructions_num, PAGE_EXECUTE_READWRITE, &old_protect);
            if (!success)
               continue;

            // From JMP to Trampoline.
            // Jumps always start counting offsets from the next instruction.
            int32_t displacement = static_cast<int32_t>(reinterpret_cast<uint8_t*>(pattern_3_trampoline) - (reinterpret_cast<uint8_t*>(pattern_address) + instructions_num));
            int64_t displacement2 = static_cast<int32_t>(reinterpret_cast<uint8_t*>(pattern_3_trampoline) - (reinterpret_cast<uint8_t*>(pattern_address) + instructions_num));
            assert(displacement2 <= INT32_MAX && displacement2 >= INT32_MIN);

            uint8_t jmp_instruction = 0xE9; // JMP rel32
            std::memcpy(pattern_address, &jmp_instruction, sizeof(jmp_instruction));
            std::memcpy(pattern_address + sizeof(jmp_instruction), &displacement, sizeof(displacement));

            DWORD temp_protect;
            VirtualProtect(pattern_address, instructions_num, old_protect, &temp_protect);

            FlushInstructionCache(GetCurrentProcess(), pattern_address, instructions_num);
         }
#endif
      }

      return !failed;
   }

   void Init(const char* name, uint32_t version)
   {
      FindAddresses();
      AllocateData();

      // Pre-patch to the current value if it was already changed
      if (aspect_ratio != default_aspect_ratio)
      {
         Patch(aspect_ratio);
      }
   }

   void Uninit()
   {
      // The "Trampoline" doesn't need de-allocation.
      
      // Restore the original aspect ratio and any other patched stuff, otherwise we'd point at memory that might now be de-allocated (actually, this is fairly useless, because "aspect_ratio_float_ptr" would stay allocated unless we manually de-allocate it).
      aspect_ratio = default_aspect_ratio;
      Patch(default_aspect_ratio, true);

#if USE_TRAMPOLINE
      if (pattern_3_trampoline)
      {
         VirtualFree(pattern_3_trampoline, 0, MEM_RELEASE);
         pattern_3_trampoline = nullptr;
         aspect_ratio_float_ptr = nullptr;
      }
#else
      if (aspect_ratio_float_ptr)
      {
         VirtualFree(aspect_ratio_float_ptr, 0, MEM_RELEASE);
      }
#endif
   }

   // Note: you might need to toggle between windowed/fullscreen/borderless in the game for the changes to take effect if you change the aspect ratio after boot
   bool SetOutputResolution(uint32_t output_res_x, uint32_t output_res_y)
   {
      float new_aspect_ratio = float(output_res_x) / float(output_res_y);
      if (new_aspect_ratio != aspect_ratio)
      {
         aspect_ratio = new_aspect_ratio;
         if (!pattern_1_addresses.empty()) // Only apply if ready or anyway found successfully
         {
            return Patch(aspect_ratio);
         }
      }
      return false;
   }
}