#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Windows.h>

#include "Common/Api.hpp"
#include "Hooks/Scope.hpp"
#include "Utils/Log.hpp"

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

// The GPU validation setting (Development builds)
void Register();
// Whether the debug layer, and its GPU-based validation, were turned on for this run's D3D12 devices: by the build
// (LumaDebugLayer, LumaGpuValidation) or the setting
bool IsDebugLayerOn();
bool IsGpuValidationOn();
// D3D12: at the first device's creation, what this run uses (the debug layer can't be turned on once a device exists)
void SetD3D12Layers(bool debug_layer, bool gpu_validation);

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

namespace Detail {
template <typename InfoQueue>
struct InfoQueueReader
{
   InfoQueue* info_queue = nullptr;
   uint64_t next = 0;
};

template <typename Device, typename InfoQueue>
struct InfoQueueStore
{
   std::mutex mutex;
   std::unordered_map<Device*, InfoQueueReader<InfoQueue>> readers;
   std::atomic<uint32_t> count = 0;
};

template <typename Device, typename InfoQueue>
InfoQueueStore<Device, InfoQueue>& GetInfoQueueStore()
{
   static InfoQueueStore<Device, InfoQueue> store;
   return store;
}
} // namespace Detail

template <Api api, typename InfoQueue, typename Device>
bool WatchInfoQueue(Device* device)
{
   InfoQueue* info_queue = nullptr;
   {
      Hooks::Scope::Guard scope;
      if (FAILED(device->QueryInterface(__uuidof(InfoQueue), reinterpret_cast<void**>(&info_queue))) || !info_queue)
         return false;
   }
   info_queue->SetMessageCountLimit(UINT64(-1));
   Utils::Log::Info("{}: device {} has the debug layer, its messages are logged", GetName(api), static_cast<void*>(device));
   auto& store = Detail::GetInfoQueueStore<Device, InfoQueue>();
   std::scoped_lock lock(store.mutex);
   if (const auto it = store.readers.find(device); it != store.readers.end() && it->second.info_queue)
      it->second.info_queue->Release();
   store.readers[device] = {info_queue, 0};
   store.count.store(uint32_t(store.readers.size()), std::memory_order_relaxed);
   return true;
}

template <Api api, typename Device, typename InfoQueue, typename Message>
void ReadInfoQueues()
{
   auto& store = Detail::GetInfoQueueStore<Device, InfoQueue>();
   if (store.count.load(std::memory_order_relaxed) == 0)
      return;
   std::scoped_lock lock(store.mutex);
   for (auto& [device, reader] : store.readers)
      ReadMessages<Message>(api, reader.info_queue, reader.next);
}

template <Api api, typename InfoQueue, typename Message, typename Device>
void StopWatchingInfoQueue(Device* device)
{
   Detail::InfoQueueReader<InfoQueue> reader;
   {
      auto& store = Detail::GetInfoQueueStore<Device, InfoQueue>();
      std::scoped_lock lock(store.mutex);
      const auto it = store.readers.find(device);
      if (it == store.readers.end())
         return;
      reader = it->second;
      store.readers.erase(it);
      store.count.store(uint32_t(store.readers.size()), std::memory_order_relaxed);
   }
   ReadMessages<Message>(api, reader.info_queue, reader.next);
   if (reader.info_queue)
      reader.info_queue->Release();
}
} // namespace Validation
} // namespace Luma
