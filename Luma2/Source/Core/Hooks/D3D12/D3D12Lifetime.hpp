#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include <Unknwn.h>
#include <wrl/client.h>

struct ID3D12CommandList;
struct ID3D12CommandQueue;
struct ID3D12GraphicsCommandList;

// A Luma object is released only after the work that used it has finished. D3D11 keeps bound objects alive itself; on
// D3D12 each command list that binds a Luma object references it, the references move to the queue when the list is
// executed, tagged with a fence value of Luma's, and are dropped when the GPU passes it.
namespace Luma {
namespace Hooks {
namespace Lifetime {
namespace D3D12 {
// A Luma object command lists may bind (a variant, a mirror, a cbuffer), replaceable from another thread while lists
// that bound it are still recording. Only use it through Replace and Reference.
struct Object
{
   std::mutex mutex; // Never held with another
   Microsoft::WRL::ComPtr<IUnknown> object; // Under mutex
   // `object`, to compare only: it may be gone unless the list already holds it
   std::atomic<IUnknown*> current = nullptr;
};

// Drops Luma's own reference to the previous object: it lives on in the lists that bound it
void Replace(Object& object, Microsoft::WRL::ComPtr<IUnknown> replacement);
// `list` (recorded on this thread) keeps the object alive until it has executed. Returns the object the list holds, the one
// to bind, or null.
IUnknown* Reference(ID3D12GraphicsCommandList* list, Object& object);
// After ExecuteCommandLists: the queue holds the lists' references until the GPU has passed this execution, and what it
// finished with is released. The lists keep theirs too, for a closed list executed again (or a bundle), until they're reset.
void MoveReferencesToQueue(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists);
// The list is reset: what it recorded before is the queues' to release
void ForgetReferences(ID3D12GraphicsCommandList* list);
} // namespace D3D12
} // namespace Lifetime
} // namespace Hooks
} // namespace Luma
