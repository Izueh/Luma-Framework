#include "Hooks/D3D12Lifetime.hpp"

#include <deque>
#include <mutex>
#include <utility>
#include <vector>

#include <d3d12.h>

#include "Hooks/Lifetime.hpp"
#include "Hooks/Scope.hpp"
#include "Utils/Log.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Hooks {
namespace Lifetime {
namespace D3D12 {
namespace {
using References = std::vector<ComPtr<IUnknown>>;

// Unique per recording of any list: a new one starts when the list is executed
std::atomic<uint64_t> next_token = 1;

// In the command list's private data. A list is recorded by one thread at a time, and executed after it's closed.
struct ListReferences
{
   uint64_t token = next_token++;
   References objects;
};

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

void Reference(ID3D12GraphicsCommandList* list, Object& object)
{
   if (!object.object)
      return;
   ListReferences* references = GetState<ListReferences>(list);
   if (!references)
      references = SetState<ListReferences>(list);
   if (!references || object.stamp.exchange(references->token) == references->token)
      return;
   references->objects.push_back(object.object);
}

void OnExecute(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
{
   References executed;
   for (UINT i = 0; i < count; i++)
   {
      ListReferences* references = lists[i] ? GetState<ListReferences>(lists[i]) : nullptr;
      if (!references)
         continue;
      std::move(references->objects.begin(), references->objects.end(), std::back_inserter(executed));
      references->objects.clear();
      references->token = next_token++;
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
         Utils::Log::Error("D3D12: no fence for queue {}, Luma objects its lists used are kept until it's destroyed", static_cast<void*>(queue));
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
