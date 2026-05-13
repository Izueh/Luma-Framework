#pragma once

// License: MIT License

// Copyright (c) 2024 Lyall

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// Source: https://github.com/Lyall/GBFRelinkFix/blob/master/src/helper.hpp

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>
#include <Windows.h>

namespace Memory
{
   inline std::vector<int> PatternToByte(const char* pattern)
   {
      std::vector<int> bytes;
      const char* current = pattern;
      const char* end = pattern + std::strlen(pattern);

      while (current < end)
      {
         if (*current == ' ')
         {
            ++current;
            continue;
         }

         if (*current == '?')
         {
            ++current;
            if (current < end && *current == '?')
               ++current;
            bytes.push_back(-1);
            continue;
         }

         char* next = nullptr;
         bytes.push_back(static_cast<int>(std::strtoul(current, &next, 16)));
         current = next;
      }

      return bytes;
   }

   inline bool GetSectionRange(void* module, std::string_view section_name, std::uint8_t*& begin, std::uint8_t*& end)
   {
      if (!module)
         return false;

      auto* dos_header = reinterpret_cast<PIMAGE_DOS_HEADER>(module);
      if (dos_header->e_magic != IMAGE_DOS_SIGNATURE)
         return false;

      auto* nt_headers = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<std::uint8_t*>(module) + dos_header->e_lfanew);
      if (nt_headers->Signature != IMAGE_NT_SIGNATURE)
         return false;

      IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt_headers);
      for (unsigned i = 0; i < nt_headers->FileHeader.NumberOfSections; ++i, ++section)
      {
         char name[9] = {};
         std::memcpy(name, section->Name, sizeof(section->Name));
         if (section_name == name)
         {
            begin = reinterpret_cast<std::uint8_t*>(module) + section->VirtualAddress;
            end = begin + section->Misc.VirtualSize;
            return true;
         }
      }

      return false;
   }

   inline std::uint8_t* PatternScan(void* module, const char* signature, std::string_view section_name = ".text")
   {
      std::uint8_t* section_begin = nullptr;
      std::uint8_t* section_end = nullptr;
      if (!GetSectionRange(module, section_name, section_begin, section_end))
         return nullptr;

      const auto pattern_bytes = PatternToByte(signature);
      if (pattern_bytes.empty())
         return nullptr;

      const std::size_t section_size = static_cast<std::size_t>(section_end - section_begin);
      if (section_size < pattern_bytes.size())
         return nullptr;

      const std::size_t pattern_size = pattern_bytes.size();
      for (std::size_t i = 0; i <= section_size - pattern_size; ++i)
      {
         bool found = true;
         for (std::size_t j = 0; j < pattern_size; ++j)
         {
            const int want = pattern_bytes[j];
            if (want != -1 && section_begin[i + j] != static_cast<std::uint8_t>(want))
            {
               found = false;
               break;
            }
         }

         if (found)
            return &section_begin[i];
      }

      return nullptr;
   }

   inline std::uint8_t* PatternScanUnique(void* module, const char* signature, std::string_view section_name, std::size_t& out_match_count)
   {
      out_match_count = 0;

      std::uint8_t* section_begin = nullptr;
      std::uint8_t* section_end = nullptr;
      if (!GetSectionRange(module, section_name, section_begin, section_end))
         return nullptr;

      const auto pattern_bytes = PatternToByte(signature);
      if (pattern_bytes.empty())
         return nullptr;

      const std::size_t section_size = static_cast<std::size_t>(section_end - section_begin);
      if (section_size < pattern_bytes.size())
         return nullptr;

      const std::size_t pattern_size = pattern_bytes.size();
      std::uint8_t* first_match = nullptr;
      for (std::size_t i = 0; i <= section_size - pattern_size; ++i)
      {
         bool found = true;
         for (std::size_t j = 0; j < pattern_size; ++j)
         {
            const int want = pattern_bytes[j];
            if (want != -1 && section_begin[i + j] != static_cast<std::uint8_t>(want))
            {
               found = false;
               break;
            }
         }

         if (found)
         {
            ++out_match_count;
            if (!first_match)
               first_match = &section_begin[i];
         }
      }

      return (out_match_count == 1) ? first_match : nullptr;
   }

   inline std::uintptr_t GetAbsolute64(std::uintptr_t address) noexcept
   {
      return address + 4 + static_cast<std::intptr_t>(*reinterpret_cast<std::int32_t*>(address));
   }

   inline std::uintptr_t GetAbsolute64(std::uintptr_t instruction_address, std::size_t disp_offset, std::size_t instruction_size) noexcept
   {
      const auto disp = *reinterpret_cast<std::int32_t*>(instruction_address + disp_offset);
      return instruction_address + instruction_size + static_cast<std::intptr_t>(disp);
   }

}
