#include "Trace/D3D12/D3D12Readback.hpp"

#include "Swapchain/DXGI/DXGISwapchain.hpp"

#if DEVELOPMENT
#include <algorithm>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <dxgi1_4.h>
#include <wrl/client.h>

#include "Resources/D3D12/D3D12Mirrors.hpp"
#include "Resources/Mirrors.hpp"
#include "Shaders/D3D12/D3D12Variants.hpp"
#include "Shaders/Database.hpp"
#include "Trace/D3D12/D3D12Capture.hpp"
#include "Trace/Readback.hpp"
#include "Utils/DXGI/DXGIFormat.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Trace {
namespace Readback {
namespace D3D12 {
namespace {
using namespace Utils::Format::DXGI;

// A resource's mip copied into a readback buffer, one placed footprint per array slice
struct Copy
{
   ComPtr<ID3D12Resource> buffer;
   std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints;
   std::vector<UINT> rows;
   D3D12_RESOURCE_DESC desc = {};
   uint32_t mip = 0;
   DXGI_FORMAT view_format = DXGI_FORMAT_UNKNOWN;
   std::string resource;
   bool mirrored = false; // Read from Luma's mirror of `resource`
   // Compared only: the list the copy was recorded on, then the queue that executed it
   const void* list = nullptr;
   ComPtr<ID3D12CommandQueue> queue;
};

std::mutex mutex;
std::optional<Copy> pending;

// Records the copy of `mip` (every array slice) of `resource`, in `state`, into a new readback buffer
std::optional<Copy> RecordCopy(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES state, uint32_t mip, std::string* error)
{
   const auto fail = [error](std::string message) -> std::optional<Copy> {
      *error = std::move(message);
      return std::nullopt;
   };
   ComPtr<ID3D12Device> device;
   if (FAILED(list->GetDevice(IID_PPV_ARGS(&device))))
      return fail("the list's device couldn't be found");
   Copy copy = {.desc = resource->GetDesc(), .mip = mip};
   const D3D12_RESOURCE_DESC& desc = copy.desc;
   if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
      return fail("the view is of a buffer, not a texture");
   if (desc.SampleDesc.Count > 1)
      return fail("multisampled textures aren't read back on D3D12 yet");
   if (IsDepthStencil(desc.Format))
      return fail("depth stencil textures aren't read back on D3D12 yet");
   if (mip >= desc.MipLevels)
      return fail(std::format("mip {} is out of range ({} mips)", mip, desc.MipLevels));
   const UINT slices = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1u : desc.DepthOrArraySize;
   copy.footprints.resize(slices);
   copy.rows.resize(slices);
   UINT64 total = 0;
   for (UINT slice = 0; slice < slices; slice++)
   {
      UINT64 size = 0;
      device->GetCopyableFootprints(&desc, mip + slice * desc.MipLevels, 1, total, &copy.footprints[slice], &copy.rows[slice], nullptr, &size);
      total = (copy.footprints[slice].Offset + size + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) & ~UINT64(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1);
   }
   const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_READBACK};
   const D3D12_RESOURCE_DESC buffer_desc = {.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
      .Width = total,
      .Height = 1,
      .DepthOrArraySize = 1,
      .MipLevels = 1,
      .SampleDesc = {1, 0},
      .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
   if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&copy.buffer))))
      return fail("the readback buffer couldn't be created");
   std::vector<D3D12_RESOURCE_BARRIER> barriers;
   for (UINT slice = 0; slice < slices; slice++)
   {
      if (state != D3D12_RESOURCE_STATE_COPY_SOURCE)
         barriers.push_back({.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            .Transition = {resource, mip + slice * desc.MipLevels, state, D3D12_RESOURCE_STATE_COPY_SOURCE}});
   }
   // A UAV's writes are done before it's read as a copy source
   if (state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
      barriers.insert(barriers.begin(), D3D12_RESOURCE_BARRIER{.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV, .UAV = {resource}});
   if (!barriers.empty())
      list->ResourceBarrier(UINT(barriers.size()), barriers.data());
   for (UINT slice = 0; slice < slices; slice++)
   {
      const D3D12_TEXTURE_COPY_LOCATION destination = {.pResource = copy.buffer.Get(), .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, .PlacedFootprint = copy.footprints[slice]};
      const D3D12_TEXTURE_COPY_LOCATION source = {.pResource = resource, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, .SubresourceIndex = mip + slice * desc.MipLevels};
      list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
   }
   for (D3D12_RESOURCE_BARRIER& barrier : barriers)
   {
      if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
         std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
   }
   std::erase_if(barriers, [](const D3D12_RESOURCE_BARRIER& barrier) { return barrier.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; });
   if (!barriers.empty())
      list->ResourceBarrier(UINT(barriers.size()), barriers.data());
   copy.resource = Capture::GetObjectId(resource);
   copy.list = list;
   return copy;
}

// After the copy's work finished on the GPU: its bytes in the .bin layout
Result Read(const Copy& copy)
{
   const D3D12_RESOURCE_DESC& desc = copy.desc;
   const D3D12_SUBRESOURCE_FOOTPRINT& first = copy.footprints.front().Footprint;
   Texture texture = {.dimension = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? "3d" : desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ? "1d"
                                                                                                                                                      : "2d",
      .width = std::max(uint32_t(desc.Width >> copy.mip), 1u),
      .height = std::max(desc.Height >> copy.mip, 1u),
      .depth = first.Depth,
      .slices = uint32_t(copy.footprints.size()),
      .mip = copy.mip,
      .mips = desc.MipLevels,
      .format = uint32_t(desc.Format),
      .view_format = uint32_t(copy.view_format != DXGI_FORMAT_UNKNOWN ? copy.view_format : desc.Format),
      .row_pitch = first.RowPitch,
      .rows = copy.rows.front(),
      .block_compressed = IsBlockCompressed(desc.Format),
      .resource = copy.resource,
      .mirrored = copy.mirrored};
   void* mapped = nullptr;
   if (FAILED(copy.buffer->Map(0, nullptr, &mapped)))
      return {.error = "the readback buffer couldn't be mapped"};
   for (size_t slice = 0; slice < copy.footprints.size(); slice++)
   {
      // A placed footprint's depth slices follow each other, `rows` rows of RowPitch bytes each
      const auto* bytes = static_cast<const std::byte*>(mapped) + copy.footprints[slice].Offset;
      texture.bytes.insert(texture.bytes.end(), bytes, bytes + size_t(copy.rows[slice]) * first.RowPitch * first.Depth);
   }
   const D3D12_RANGE written = {};
   copy.buffer->Unmap(0, &written);
   return {.texture = std::move(texture)};
}

// Blocks until `queue` passed everything submitted to it so far
bool WaitForQueue(ID3D12CommandQueue* queue)
{
   ComPtr<ID3D12Device> device;
   ComPtr<ID3D12Fence> fence;
   if (FAILED(queue->GetDevice(IID_PPV_ARGS(&device))) || FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) || FAILED(queue->Signal(fence.Get(), 1)))
      return false;
   // A one off read: the stall is fine
   return SUCCEEDED(fence->SetEventOnCompletion(1, nullptr));
}

// The current back buffer, copied on a list of Luma's on the presenting queue
Result ReadBackBuffer(IDXGISwapChain* swapchain, ID3D12CommandQueue* queue, uint32_t mip)
{
   ComPtr<IDXGISwapChain3> swapchain3;
   ComPtr<ID3D12Resource> back_buffer;
   // The game's image: its own buffer where Luma composes into the real one (in the common state, as a back buffer's present one)
   if (const ComPtr<IUnknown> game_buffer = Swapchain::DXGI::GetGameBuffer(swapchain))
      game_buffer.As(&back_buffer);
   else if (FAILED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain3))) || FAILED(swapchain3->GetBuffer(swapchain3->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&back_buffer))))
      return {.error = "the swapchain has no back buffer"};
   if (!back_buffer)
      return {.error = "the swapchain has no back buffer"};
   // What the game drew is in its buffer's mirror while mirrors are swapped in (in the same state)
   if (ID3D12Resource* mirror = Resources::Mirrors::IsSwapping() ? Resources::Mirrors::D3D12::GetMirror(back_buffer.Get()) : nullptr)
      back_buffer = mirror;
   ComPtr<ID3D12Device> device;
   ComPtr<ID3D12CommandAllocator> allocator;
   ComPtr<ID3D12GraphicsCommandList> list;
   const D3D12_COMMAND_LIST_TYPE type = queue->GetDesc().Type;
   if (FAILED(queue->GetDevice(IID_PPV_ARGS(&device))) || FAILED(device->CreateCommandAllocator(type, IID_PPV_ARGS(&allocator))) ||
      FAILED(device->CreateCommandList(0, type, allocator.Get(), nullptr, IID_PPV_ARGS(&list))))
      return {.error = "the readback command list couldn't be created"};
   std::string error;
   // A back buffer is in the present state between frames
   std::optional<Copy> copy = RecordCopy(list.Get(), back_buffer.Get(), D3D12_RESOURCE_STATE_PRESENT, mip, &error);
   list->Close();
   if (!copy)
      return {.error = error};
   ID3D12CommandList* const lists[] = {list.Get()};
   queue->ExecuteCommandLists(1, lists);
   if (!WaitForQueue(queue))
      return {.error = "the readback couldn't be waited for"};
   return Read(*copy);
}
} // namespace

bool ReadAfterCall(const Game::D3D12::Call& call, Shaders::StageSet stages, bool handled)
{
   const std::optional<Target> target = GetTarget();
   if (!target || target->view == ViewKind::Swapchain || !stages.Has(target->stage))
      return handled;
   const Shaders::Variants::D3D12::BoundPipeline bound = Shaders::Variants::D3D12::GetBoundPipeline(call.list);
   const Shaders::Database::Shader* shader = bound.tracked ? bound.tracked->shaders[size_t(target->stage)] : nullptr;
   if (!shader || shader->hash != target->hash || !CountCall())
      return handled;
   // The view is found before the call: a callback's own work after it doesn't change the list's record
   const std::optional<Capture::D3D12::BoundView> view = Capture::D3D12::FindBoundView(call.list, target->view, target->stage, target->slot);
   if (!handled)
      Game::D3D12::RunOriginal(call);
   MarkCopied();
   if (!view)
   {
      Finish({.error = std::format("the call has no {} at slot {} (space 0)", GetName(target->view), target->slot)});
      return true;
   }
   if (!view->state)
   {
      Finish({.error = "the list didn't transition the resource before the call, so its state isn't known (a render target or UAV is)"});
      return true;
   }
   // A mirrored texture is read from its mirror while the mirrors are swapped in (it's what the call wrote), unless the game's is asked
   // for. The mirror is in the original's state: it gets every barrier the original does.
   ID3D12Resource* mirror = !target->original && Resources::Mirrors::IsSwapping() ? Resources::Mirrors::D3D12::GetMirror(view->resource) : nullptr;
   std::string error;
   std::optional<Copy> copy = RecordCopy(call.list, mirror ? mirror : view->resource, *view->state, target->mip, &error);
   if (!copy)
   {
      Finish({.error = error.empty() ? "the copy couldn't be recorded" : error});
      return true;
   }
   copy->view_format = target->view == ViewKind::Depth || target->view == ViewKind::Stencil ? GetDepthReadFormat(view->format, target->view == ViewKind::Stencil) : view->format;
   if (mirror)
   {
      // The game's view format names the game's texture: the mirror's own is how its data reads
      copy->view_format = DXGI_FORMAT_UNKNOWN;
      copy->resource = Capture::GetObjectId(view->resource);
      copy->mirrored = true;
   }
   std::scoped_lock lock(mutex);
   pending = std::move(copy);
   return true;
}

void OnExecute(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
{
   if (!IsArmed())
      return;
   std::scoped_lock lock(mutex);
   if (!pending || pending->queue)
      return;
   for (UINT i = 0; i < count; i++)
   {
      if (lists[i] && static_cast<const void*>(static_cast<ID3D12GraphicsCommandList*>(lists[i])) == pending->list)
         pending->queue = queue;
   }
}

void ReadBack(IDXGISwapChain* swapchain, ID3D12CommandQueue* queue)
{
   if (!IsArmed())
      return;
   const std::optional<Target> target = GetTarget();
   if (!target)
      return;
   if (target->view == ViewKind::Swapchain)
      return Finish(ReadBackBuffer(swapchain, queue, target->mip));
   std::optional<Copy> copy;
   {
      std::scoped_lock lock(mutex);
      // Its list may run in a later frame
      if (!pending || !pending->queue)
         return;
      copy = std::exchange(pending, std::nullopt);
   }
   if (!WaitForQueue(copy->queue.Get()))
      return Finish({.error = "the readback couldn't be waited for"});
   Finish(Read(*copy));
}
} // namespace D3D12
} // namespace Readback
} // namespace Trace
} // namespace Luma
#endif
