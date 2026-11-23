#include "Hooks/D3D12/D3D12Lifetime.hpp"

#include <array>
#include <deque>
#include <iterator>
#include <mutex>
#include <utility>
#include <vector>

#include <d3d12.h>

#include "Hooks/ObjectLifetime.hpp"
#include "Hooks/HookScope.hpp"
#include "Log/Log.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Hooks {
namespace Lifetime {
namespace D3D12 {
namespace {
using References = std::vector<ComPtr<IUnknown>>;

// In the command list's private data. A list is recorded by one thread at a time, and executed after it's closed.
struct ListReferences
{
   References objects;
   // Objects already in `objects`, by address: a bind of one of them costs no lock. A collision only adds a second reference.
   std::array<IUnknown*, 64> held = {};
};

size_t GetSlot(const IUnknown* object)
{
   return (reinterpret_cast<uintptr_t>(object) >> 4) % 64;
}

struct Submission
{
   uint64_t fence_value;
   References objects;
};

// In the queue's private data
struct QueueReferences
{
   std::mutex mutex;
   ComPtr<ID3D12Fence> fence;
   uint64_t fence_value = 0;
   std::deque<Submission> pending;
};

// With the queue's lock held
void Collect(QueueReferences& references)
{
   if (!references.fence)
      return;
   const uint64_t completed = references.fence->GetCompletedValue();
   while (!references.pending.empty() && references.pending.front().fence_value <= completed)
      references.pending.pop_front();
}
} // namespace

void Replace(Object& object, ComPtr<IUnknown> replacement)
{
   std::scoped_lock lock(object.mutex);
   object.object.Swap(replacement);
   object.current.store(object.object.Get(), std::memory_order_release);
   // The previous object is released when `replacement` goes, after the lock
}

IUnknown* Reference(ID3D12GraphicsCommandList* list, Object& object)
{
   IUnknown* current = object.current.load(std::memory_order_acquire);
   if (!current)
      return nullptr;
   ListReferences* references = GetState<ListReferences>(list);
   if (!references)
      references = SetState<ListReferences>(list);
   if (!references)
      return nullptr;
   // The list holds a reference to it, so it's alive and it's this object, even if it was replaced since
   if (references->held[GetSlot(current)] == current)
      return current;

   ComPtr<IUnknown> referenced;
   {
      std::scoped_lock lock(object.mutex);
      referenced = object.object;
   }
   if (!referenced)
      return nullptr;
   current = referenced.Get();
   references->held[GetSlot(current)] = current;
   references->objects.push_back(std::move(referenced));
   return current;
}

void ForgetReferences(ID3D12GraphicsCommandList* list)
{
   if (ListReferences* references = GetState<ListReferences>(list))
   {
      references->objects.clear();
      references->held.fill(nullptr);
   }
}

void MoveReferencesToQueue(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
{
   References executed;
   for (UINT i = 0; i < count; i++)
   {
      ListReferences* references = lists[i] ? GetState<ListReferences>(lists[i]) : nullptr;
      if (!references)
         continue;
      executed.insert(executed.end(), references->objects.begin(), references->objects.end());
   }

   QueueReferences* queue_references = GetState<QueueReferences>(queue);
   if (!queue_references)
   {
      if (executed.empty())
         return;
      queue_references = SetState<QueueReferences>(queue);
      if (!queue_references)
         return;
   }
   Scope::Guard scope;
   std::scoped_lock lock(queue_references->mutex);
   Collect(*queue_references);
   if (executed.empty())
      return;
   if (!queue_references->fence)
   {
      ComPtr<ID3D12Device> device;
      if (FAILED(queue->GetDevice(IID_PPV_ARGS(&device))) || FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&queue_references->fence))))
      {
         Log::Error("D3D12: no fence for queue {}, Luma objects its lists used are kept until it's destroyed", static_cast<void*>(queue));
         queue_references->pending.push_back({UINT64_MAX, std::move(executed)});
         return;
      }
   }
   queue->Signal(queue_references->fence.Get(), ++queue_references->fence_value);
   queue_references->pending.push_back({queue_references->fence_value, std::move(executed)});
}
} // namespace D3D12
} // namespace Lifetime
} // namespace Hooks
} // namespace Luma
