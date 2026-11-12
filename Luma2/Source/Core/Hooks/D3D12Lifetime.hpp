#pragma once

#include <atomic>
#include <cstdint>

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
// A Luma object command lists may bind (a variant, a mirror, a cbuffer). Replacing `object` drops Luma's own reference:
// the old one lives on in the lists that bound it.
struct Object
{
   Microsoft::WRL::ComPtr<IUnknown> object;
   // The last recording that referenced it, so a list notes it once
   std::atomic<uint64_t> stamp = 0;
};

// `list` (recorded on this thread) keeps the object alive until it has executed
void Reference(ID3D12GraphicsCommandList* list, Object& object);
// After ExecuteCommandLists: the lists' references move to the queue, and what the GPU finished with is released
void OnExecute(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists);
} // namespace D3D12
} // namespace Lifetime
} // namespace Hooks
} // namespace Luma
