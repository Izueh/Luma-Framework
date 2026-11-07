#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <Windows.h>

#include "common/api.hpp"

#ifndef LUMA_DEBUG_LAYER
#define LUMA_DEBUG_LAYER 0
#endif
#ifndef LUMA_GPU_VALIDATION
#define LUMA_GPU_VALIDATION 0
#endif

namespace Luma {
namespace Validation {
inline constexpr bool DEBUG_LAYER = LUMA_DEBUG_LAYER != 0;
inline constexpr bool GPU_VALIDATION = LUMA_GPU_VALIDATION != 0;

enum class Severity : uint8_t
{
   Corruption,
   Error,
   Warning,
   Info,
};

struct Counts
{
   uint64_t corruptions = 0;
   uint64_t errors = 0;
   uint64_t warnings = 0;
};

// Each message id is logged a few times, then only counted
void Report(Api api, Severity severity, int32_t id, std::string_view text);
Counts GetCounts();
// Forgets the counts (tests)
void Reset();

// Reports what an ID3D11InfoQueue or ID3D12InfoQueue stored since `next`, clearing it before it fills and drops messages
template <typename Message, typename InfoQueue>
void ReadMessages(Api api, InfoQueue* info_queue, uint64_t& next)
{
   const uint64_t stored = info_queue->GetNumStoredMessages();
   std::vector<uint8_t> storage;
   for (; next < stored; next++)
   {
      SIZE_T size = 0;
      if (FAILED(info_queue->GetMessage(next, nullptr, &size)) || size == 0)
         continue;
      storage.resize(size);
      auto* message = reinterpret_cast<Message*>(storage.data());
      if (SUCCEEDED(info_queue->GetMessage(next, message, &size)))
         Report(api, Severity(uint32_t(message->Severity) < 3 ? uint32_t(message->Severity) : 3), int32_t(message->ID),
            std::string_view(message->pDescription, message->DescriptionByteLength ? message->DescriptionByteLength - 1 : 0));
   }
   if (next >= 4096)
   {
      info_queue->ClearStoredMessages();
      next = 0;
   }
}
} // namespace Validation
} // namespace Luma
