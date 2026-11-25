#include "Handlers/D3D12/D3D12Handlers.hpp"

#include <array>
#include <optional>
#include <span>
#include <type_traits>

#include "Game/GameRegistry.hpp"
#include "Handlers/Handlers.hpp"
#include "Hooks/D3D12/D3D12Lifetime.hpp"
#include "Resources/D3D12/D3D12HeapTail.hpp"
#include "Resources/D3D12/D3D12Mirrors.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/SamplerMirrors.hpp"
#include "Resources/ResourceUpgrades.hpp"
#include "Resources/D3D12/D3D12ResourceDatabase.hpp"
#include "Shaders/D3D12/D3D12PipelineLibrary.hpp"
#include "Shaders/D3D12/D3D12Variants.hpp"
#include "Shaders/Shaders.hpp"
#include "Game/D3D12/D3D12DrawEvents.hpp"
#include "Resources/D3D12/D3D12Descriptors.hpp"
#if DEVELOPMENT
#include "Trace/DebugDraw.hpp"
#include "Trace/FrameCapture.hpp"
#include "Trace/D3D12/D3D12Capture.hpp"
#include "Trace/D3D12/D3D12Readback.hpp"
#include "Trace/D3D12/D3D12Stats.hpp"
#include "Trace/ResourceReadback.hpp"
#include "Trace/CbufferTracker.hpp"
#endif
#include "UI/Overlay.hpp"
#include "Utils/Debug.hpp"
#include "Log/Log.hpp"
#include "Validation/D3D12/D3D12Validation.hpp"

namespace Luma {
namespace Handlers {
namespace {
using Shaders::Database::D3D12::PipelineKind;

Shaders::Database::D3D12::PipelineDesc Describe(PipelineKind kind, const void* desc)
{
   switch (kind)
   {
   case PipelineKind::Graphics: return Shaders::Database::D3D12::DescribePipeline(*static_cast<const D3D12_GRAPHICS_PIPELINE_STATE_DESC*>(desc));
   case PipelineKind::Compute: return Shaders::Database::D3D12::DescribePipeline(*static_cast<const D3D12_COMPUTE_PIPELINE_STATE_DESC*>(desc));
   default: return Shaders::Database::D3D12::DescribePipeline(*static_cast<const D3D12_PIPELINE_STATE_STREAM_DESC*>(desc));
   }
}

// Before a draw, dispatch or indirect execution: the variant bound requests its resources. One it can't have (Luma's textures
// with a heap without a tail) runs the game's pipeline for the call, as D3D11's UseOriginal.
void BindVariantRequests(ID3D12GraphicsCommandList* list)
{
   if (const Shaders::Variants::D3D12::Built* bound = Shaders::Variants::D3D12::GetBound(list))
   {
      if ((bound->textures && bound->table == Resources::Database::D3D12::NO_TABLE) ||
         !Resources::Database::D3D12::BindRequested(list, bound->compute, bound->entries, {}, bound->table))
         Shaders::Variants::D3D12::UseOriginal(list);
   }
}

template <typename Call>
bool RunCall(ID3D12GraphicsCommandList* list, const Call& call)
{
   constexpr bool dispatch = std::is_same_v<Call, Game::D3D12::DispatchCall>;
#if DEVELOPMENT
   Trace::Stats::D3D12::RecordDraw(list, dispatch);
#endif
   const auto run_draw = [&](Shaders::StageSet stages, Game::DrawResult& result, bool& skipped) {
      if (Resources::Mirrors::IsNeeded())
         Resources::Mirrors::D3D12::PrepareDrawOrDispatch(list, dispatch);
      result = Game::Registry::HasDrawCallbacks<Api::D3D12>() ? Game::DrawEvents::D3D12::Run(list, call) : Game::DrawResult::None;
#if DEVELOPMENT
      skipped = result == Game::DrawResult::None && Trace::Capture::D3D12::IsSkipped(list, stages);
#else
      (void)stages;
#endif
      if (result == Game::DrawResult::None && !skipped)
      {
         BindVariantRequests(list);
         if constexpr (!dispatch)
            Shaders::Variants::D3D12::BindFormatVariant(list);
      }
      if (Resources::Mirrors::IsNeeded())
         Resources::Mirrors::D3D12::FinishDrawOrDispatch(list, dispatch);
      return result != Game::DrawResult::None || skipped;
   };
#if DEVELOPMENT
   Game::D3D12::Call original = {.list = list};
   if constexpr (dispatch)
      original.dispatch = &call;
   else
      original.draw = &call;
   return HandleDrawOrDispatch<dispatch>(
      [&] {
         if constexpr (dispatch)
            return Trace::Capture::D3D12::BeginDispatch(list, call);
         else
            return Trace::Capture::D3D12::BeginDraw(list, call);
      },
      [&](Shaders::StageSet stages) { Trace::Capture::D3D12::TrackCall(list, stages); },
      [&](Shaders::StageSet stages, bool handled) { return Trace::Readback::D3D12::ReadAfterCall(original, stages, handled); },
      [](Trace::Capture::D3D12::Recording recording, Trace::Capture::Outcome outcome) { Trace::Capture::D3D12::End(std::move(recording), outcome); },
      run_draw);
#else
   Game::DrawResult result = Game::DrawResult::None;
   bool skipped = false;
   return run_draw(dispatch ? Shaders::COMPUTE_STAGES : Shaders::GRAPHICS_STAGES, result, skipped);
#endif
}
} // namespace

void PreCreateDevice()
{
   Validation::D3D12::EnableDebugLayer();
}

void PostCreateDevice(ID3D12Device* device)
{
   HandlePostCreateDevice<Api::D3D12>(
      device, {},
      [&] { Validation::D3D12::WatchDeviceMessages(device); },
      [&] {
         if (Shaders::IsNeeded())
            Resources::Database::D3D12::CreateDeviceObjects(device);
#if DEVELOPMENT
         Trace::Stats::D3D12::RecordDevice(device);
#endif
      });
}

void OnDeviceDestroyed(ID3D12Device* device)
{
   HandleOnDeviceDestroyed<Api::D3D12>(
      device,
      [&] {
#if DEVELOPMENT
         Trace::DebugDraw::ReleaseD3D12(device);
#endif
         Resources::Database::D3D12::ReleaseDeviceObjects(device);
         Resources::Mirrors::D3D12::ReleaseDeviceObjects(device);
         Shaders::RootSignatures::D3D12::ReleaseDeviceObjects(device);
         Shaders::PipelineLibrary::D3D12::ReleaseDeviceObjects(device);
      },
      [&] { Validation::D3D12::StopWatchingDevice(device); });
}

void PostExecuteCommandLists(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
{
   if (Resources::Mirrors::IsNeeded())
      Resources::Mirrors::D3D12::CommitExecutedBarriers(count, lists);
   Hooks::Lifetime::D3D12::MoveReferencesToQueue(queue, count, lists);
#if DEVELOPMENT
   Trace::Capture::D3D12::RecordExecute(queue, count, lists);
   Trace::Readback::D3D12::OnExecute(queue, count, lists);
   Trace::Stats::D3D12::RecordExecute(count);
#endif
}

std::optional<Shaders::RootSignatures::D3D12::RewrittenDesc> PreCreatePipelineState(ID3D12Device* device, PipelineKind kind, const void* desc)
{
   if (!Shaders::IsNeeded() || !device)
      return std::nullopt;
   return Shaders::RootSignatures::D3D12::RewritePipelineDesc(device, kind, desc);
}

void PostCreatePipelineState(PipelineKind kind, const void* desc, bool from_library, ID3D12PipelineState* pipeline)
{
   if (!Shaders::IsNeeded())
      return;
   const Shaders::Database::D3D12::PipelineDesc described = Describe(kind, desc);
   std::shared_ptr<const Shaders::Database::D3D12::DescCopy> copy;
   if (Shaders::Variants::D3D12::NeedsDescCopy(described))
      copy = Shaders::Database::D3D12::CopyDesc(kind, desc);
   const Shaders::Database::D3D12::Pipeline* tracked = Shaders::Database::D3D12::TrackPipeline(described, std::move(copy), from_library, pipeline);
   if (!tracked)
      return;
   Shaders::Variants::D3D12::AttachVariants(pipeline, *tracked);
#if DEVELOPMENT
   Trace::Stats::D3D12::RecordPipeline(*tracked, described);
#endif
}

void PostLoadPipeline([[maybe_unused]] HRESULT result)
{
#if DEVELOPMENT
   Trace::Stats::D3D12::RecordLibraryLoad(result);
#endif
}

void PostCreatePipelineLibrary([[maybe_unused]] size_t blob_size, [[maybe_unused]] HRESULT result)
{
#if DEVELOPMENT
   Trace::Stats::D3D12::RecordPipelineLibrary(blob_size, result);
#endif
}

Shaders::RootSignatures::D3D12::Extended PreCreateRootSignature(std::span<const uint8_t> blob)
{
   if (!Shaders::IsNeeded())
      return {};
   return Shaders::RootSignatures::D3D12::Extend(blob);
}

void PostExtendedRootSignatureRefused(HRESULT result)
{
   ASSERT_ONCE_MSG(false, "The device refused an extended root signature");
   Log::Error("D3D12: the device refused an extended root signature (0x{:08X}), the game's is used and its pipelines get no variants", uint32_t(result));
}

void PostCreateRootSignature(std::span<const uint8_t> blob, ID3D12RootSignature* root_signature, uint32_t luma_parameter, bool luma_table, bool luma_samplers)
{
   if (!Shaders::IsNeeded())
      return;
   [[maybe_unused]] const Shaders::Database::D3D12::RootLayout* layout = Shaders::Database::D3D12::TrackRootSignature(blob, root_signature, luma_parameter, luma_table, luma_samplers);
#if DEVELOPMENT
   if (layout)
      Trace::Stats::D3D12::RecordRootSignature(*layout);
#endif
}

void PostCreateRootSignatureFromLibrary()
{
   LUMA_UNIMPLEMENTED("Root signatures from a DXIL library: not extended, their pipelines get no variants");
}

void PostCreateStateObject()
{
   LUMA_UNIMPLEMENTED("Variants of state objects (DXIL, the first SM6 game)");
}

std::optional<D3D12_DESCRIPTOR_HEAP_DESC> PreCreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC& desc)
{
   if (!Shaders::IsNeeded())
      return std::nullopt;
   return Resources::HeapTail::D3D12::Extend(desc);
}

std::optional<D3D12_DESCRIPTOR_HEAP_DESC> PostExtendDescriptorHeap(ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC& desc, const D3D12_DESCRIPTOR_HEAP_DESC& extended,
   HRESULT result)
{
   return Resources::HeapTail::D3D12::RecordExtension(device, desc, extended, result);
}

void PostCreateDescriptorHeap(ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC& desc, ID3D12DescriptorHeap* heap, uint32_t tail_size)
{
   if (tail_size)
      Resources::HeapTail::D3D12::Track(device, heap, desc, tail_size);
   if (Resources::Mirrors::IsNeeded() || Resources::Samplers::IsNeeded())
      Resources::Mirrors::D3D12::TrackHeap(device, heap, desc);
#if DEVELOPMENT
   Resources::Descriptors::D3D12::TrackHeap(device, heap, desc);
   Trace::Stats::D3D12::RecordHeap(desc, heap);
#endif
}

void PostCreateView(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, const Resources::Descriptors::D3D12::Descriptor& descriptor)
{
   using Kind = Resources::Descriptors::D3D12::Kind;
   using ViewKind = Resources::Mirrors::D3D12::ViewKind;
   if (descriptor.kind == Kind::Sampler)
   {
      if (Resources::Samplers::IsNeeded() && descriptor.has_desc)
         Resources::Mirrors::D3D12::TrackSampler(device, destination, descriptor.desc.sampler, Resources::Samplers::IsSwapping());
   }
   else if (Resources::Mirrors::IsNeeded())
   {
      ViewKind kind = ViewKind::ConstantBuffer;
      switch (descriptor.kind)
      {
      case Kind::ShaderResource: kind = ViewKind::ShaderResource; break;
      case Kind::UnorderedAccess: kind = ViewKind::UnorderedAccess; break;
      case Kind::RenderTarget: kind = ViewKind::RenderTarget; break;
      case Kind::DepthStencil: kind = ViewKind::DepthStencil; break;
      default: break;
      }
      Resources::Mirrors::D3D12::TrackView(device, destination, kind, descriptor.resource, descriptor.has_desc ? &descriptor.desc : nullptr,
         Resources::Mirrors::IsSwapping());
   }
#if DEVELOPMENT
   Resources::Descriptors::D3D12::Write(destination, descriptor);
   Trace::Stats::D3D12::RecordViewWrite(destination);
#endif
}

void PostCopyDescriptors(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, D3D12_CPU_DESCRIPTOR_HANDLE source, UINT count, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
   if (Resources::Mirrors::IsNeeded() || Resources::Samplers::IsNeeded())
      Resources::Mirrors::D3D12::SwapCopiedDescriptors(device, destination, source, count, type, Resources::Mirrors::IsSwapping(), Resources::Samplers::IsSwapping());
#if DEVELOPMENT
   Resources::Descriptors::D3D12::Copy(destination, source, count);
   Trace::Stats::D3D12::RecordCopy(destination, count);
#endif
}

void PostCreateResource(ID3D12Device* device, ID3D12Resource* resource, std::optional<D3D12_RESOURCE_STATES> state, const D3D12_CLEAR_VALUE* clear, bool reserved)
{
   if (Resources::Mirrors::IsNeeded())
      Resources::Mirrors::D3D12::CreateMirror(device, resource, state, clear, reserved);
#if DEVELOPMENT
   Resources::Descriptors::D3D12::TrackResource(resource);
#endif
}

void PreReset(ID3D12GraphicsCommandList* list, ID3D12PipelineState*& pipeline)
{
   // No targets bound any more
   if (Resources::Mirrors::IsNeeded())
   {
      Shaders::Variants::D3D12::SetTargetFormats(list, {});
      Resources::Mirrors::D3D12::ForgetListState(list);
   }
   // What the list recorded before is the queues' to release now, and it starts with nothing of Luma's
   Hooks::Lifetime::D3D12::ForgetReferences(list);
   Resources::HeapTail::D3D12::ForgetList(list);
   Resources::Database::D3D12::ForgetList(list);
   Shaders::Variants::D3D12::SwapInVariant(list, pipeline);
#if DEVELOPMENT
   Trace::Capture::D3D12::RecordReset(list);
   Trace::Stats::D3D12::RecordReset(list);
   if (pipeline)
      Trace::Stats::D3D12::RecordBind(list, Trace::Stats::D3D12::Bind::PipelineState);
#endif
}

void PreClearState(ID3D12GraphicsCommandList* list, ID3D12PipelineState*& pipeline)
{
   if (Resources::Mirrors::IsNeeded())
   {
      Shaders::Variants::D3D12::SetTargetFormats(list, {});
      Resources::Mirrors::D3D12::ForgetListState(list);
   }
   Resources::Database::D3D12::ForgetList(list);
   Shaders::Variants::D3D12::SwapInVariant(list, pipeline);
#if DEVELOPMENT
   Trace::Capture::D3D12::RecordClearState(list);
#endif
}

void PreSetPipelineState(ID3D12GraphicsCommandList* list, ID3D12PipelineState*& pipeline)
{
   Shaders::Variants::D3D12::SwapInVariant(list, pipeline);
#if DEVELOPMENT
   Trace::Stats::D3D12::RecordBind(list, Trace::Stats::D3D12::Bind::PipelineState);
#endif
}

void PreSetStateObject(ID3D12GraphicsCommandList* list)
{
   Shaders::Variants::D3D12::ForgetBound(list);
}

void PreSetRootSignature(ID3D12GraphicsCommandList* list, bool compute, ID3D12RootSignature* root_signature)
{
   Resources::Database::D3D12::SetRootSignature(list, compute, root_signature);
   if (!compute && Resources::Mirrors::IsNeeded())
      Resources::Mirrors::D3D12::RecordRootSignature(list, root_signature);
#if DEVELOPMENT
   Trace::Capture::D3D12::RecordRootSignature(list, compute, root_signature);
   Trace::Stats::D3D12::RecordBind(list, Trace::Stats::D3D12::Bind::RootSignature);
#endif
}

void PreOMSetRenderTargets(ID3D12GraphicsCommandList* list, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE*& render_targets, BOOL& single_range,
   const D3D12_CPU_DESCRIPTOR_HANDLE*& depth_stencil)
{
#if DEVELOPMENT
   // The game's targets
   Trace::Capture::D3D12::RecordRenderTargets(list, count, render_targets, single_range, depth_stencil);
   Trace::Stats::D3D12::RecordBind(list, Trace::Stats::D3D12::Bind::RenderTargets);
#endif
   if (Resources::Mirrors::IsNeeded())
   {
      Shaders::Database::D3D12::TargetFormats formats;
      if (Resources::Mirrors::IsSwapping())
         Resources::Mirrors::D3D12::SwapRenderTargets(list, count, render_targets, single_range, depth_stencil, formats);
      Shaders::Variants::D3D12::SetTargetFormats(list, formats);
      // What a converting copy binds back: the targets as they're bound now
      Resources::Mirrors::D3D12::RecordRenderTargets(list, count, render_targets, single_range, depth_stencil);
   }
}

std::optional<uint32_t> GetGameHeapSize(ID3D12DescriptorHeap* heap)
{
   return Resources::HeapTail::D3D12::GetGameSize(heap);
}

// Trace reads descriptor tables through the heaps' GPU ranges; Luma's table is in the bound heap's tail
void PreSetDescriptorHeaps(ID3D12GraphicsCommandList* list, UINT count, ID3D12DescriptorHeap* const* heaps)
{
   Resources::Database::D3D12::SetDescriptorHeaps(list, count, heaps);
}

void PreSetRootTable([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] bool compute, [[maybe_unused]] UINT index, [[maybe_unused]] D3D12_GPU_DESCRIPTOR_HANDLE table)
{
#if DEVELOPMENT
   Trace::Capture::D3D12::RecordRootTable(list, compute, index, table);
#endif
}

void PreSetRootDescriptor([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] bool compute, [[maybe_unused]] UINT index,
   [[maybe_unused]] D3D12_GPU_VIRTUAL_ADDRESS address)
{
#if DEVELOPMENT
   Trace::Capture::D3D12::RecordRootDescriptor(list, compute, index, address);
#endif
}

void PreSetRootConstants([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] bool compute, [[maybe_unused]] UINT index, [[maybe_unused]] UINT count,
   [[maybe_unused]] const void* values, [[maybe_unused]] UINT offset)
{
#if DEVELOPMENT
   Trace::Capture::D3D12::RecordRootConstants(list, compute, index, count, values, offset);
#endif
}

void PreSetViewports(ID3D12GraphicsCommandList* list, UINT& count, const D3D12_VIEWPORT*& viewports)
{
   if (Resources::Mirrors::IsNeeded())
      Resources::Mirrors::D3D12::RecordViewports(list, count, viewports);
#if DEVELOPMENT
   Trace::Capture::D3D12::RecordViewports(list, count, viewports);
#endif
}

void PreSetScissorRects(ID3D12GraphicsCommandList* list, UINT& count, const D3D12_RECT*& rects)
{
   if (Resources::Mirrors::IsNeeded())
      Resources::Mirrors::D3D12::RecordScissorRects(list, count, rects);
#if DEVELOPMENT
   Trace::Capture::D3D12::RecordScissorRects(list, count, rects);
#endif
}

void PreSetTopology(ID3D12GraphicsCommandList* list, D3D12_PRIMITIVE_TOPOLOGY topology)
{
#if DEVELOPMENT
   Trace::Capture::D3D12::RecordTopology(list, topology);
#endif
   if (Resources::Mirrors::IsNeeded())
      Resources::Mirrors::D3D12::RecordTopology(list, topology);
}

void PreSetBlendFactor([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] const FLOAT factor[4])
{
#if DEVELOPMENT
   Trace::Capture::D3D12::RecordBlendFactor(list, factor);
#endif
}

void PreSetStencilRef([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] UINT stencil_ref)
{
#if DEVELOPMENT
   Trace::Capture::D3D12::RecordStencilRef(list, stencil_ref);
#endif
}

std::span<const D3D12_RESOURCE_BARRIER> PreResourceBarrier([[maybe_unused]] ID3D12GraphicsCommandList* list, std::span<const D3D12_RESOURCE_BARRIER> barriers)
{
#if DEVELOPMENT
   Trace::Capture::D3D12::RecordBarrier(list, UINT(barriers.size()), barriers.data());
   if (Trace::DebugDraw::IsActive())
   {
      for (const D3D12_RESOURCE_BARRIER& barrier : barriers)
      {
         if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION && barrier.Transition.pResource && barrier.Flags != D3D12_RESOURCE_BARRIER_FLAG_END_ONLY)
            Trace::DebugDraw::CaptureD3D12(list, barrier.Transition.pResource, barrier.Transition.StateBefore);
      }
   }
#endif
   return Resources::Mirrors::IsNeeded() ? Resources::Mirrors::D3D12::AddMirrorBarriers(list, barriers) : barriers;
}

bool PreCopyResource([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] ID3D12Resource*& destination, [[maybe_unused]] ID3D12Resource*& source)
{
#if DEVELOPMENT
   if (Trace::Capture::IsCapturing())
      Trace::Capture::D3D12::AddCopy(list, "CopyResource", destination, source, {});
#endif
   Resources::Mirrors::D3D12::CopyRegion region = {.convertible = true, .whole_resource = true};
   return Resources::Mirrors::IsSwapping() && Resources::Mirrors::D3D12::SwapCopy(list, destination, source, region);
}

bool PreCopyTextureRegion([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] D3D12_TEXTURE_COPY_LOCATION& destination, [[maybe_unused]] UINT& x,
   [[maybe_unused]] UINT& y, [[maybe_unused]] UINT& z, [[maybe_unused]] D3D12_TEXTURE_COPY_LOCATION& source, [[maybe_unused]] const D3D12_BOX*& box)
{
#if DEVELOPMENT
   if (Trace::Capture::IsCapturing())
   {
      // A footprint (an upload or a readback) has no subresource
      const auto subresource = [](const D3D12_TEXTURE_COPY_LOCATION& location) {
         return location.Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX ? location.SubresourceIndex : 0u;
      };
      Trace::Capture::Copy copy = {.kind = Trace::Capture::CopyKind::Region,
         .source_subresource = subresource(source),
         .destination_subresource = subresource(destination),
         .destination_offset = {x, y, z}};
      if (box)
         copy.source_box = std::array<uint64_t, 6>{box->left, box->top, box->front, box->right, box->bottom, box->back};
      Trace::Capture::D3D12::AddCopy(list, "CopyTextureRegion", destination.pResource, source.pResource, copy);
   }
#endif
   if (!Resources::Mirrors::IsSwapping())
      return false;
   // A footprint side is a buffer: never mirrored, never converted
   const bool subresources = destination.Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX && source.Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
   Resources::Mirrors::D3D12::CopyRegion region = {.convertible = subresources,
      .destination_subresource = subresources ? destination.SubresourceIndex : 0,
      .source_subresource = subresources ? source.SubresourceIndex : 0,
      .offset = {x, y, z},
      .box = box};
   const bool handled = Resources::Mirrors::D3D12::SwapCopy(list, destination.pResource, source.pResource, region);
   if (!handled)
   {
      x = region.offset[0];
      y = region.offset[1];
      z = region.offset[2];
      box = region.box;
   }
   return handled;
}

bool PreCopyBufferRegion([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] ID3D12Resource* destination, [[maybe_unused]] UINT64 destination_offset,
   [[maybe_unused]] ID3D12Resource* source, [[maybe_unused]] UINT64 source_offset, [[maybe_unused]] UINT64 size)
{
#if DEVELOPMENT
   if (Trace::Capture::IsCapturing())
      Trace::Capture::D3D12::AddCopy(list, "CopyBufferRegion", destination, source,
         {.kind = Trace::Capture::CopyKind::BufferRegion,
            .destination_offset = {destination_offset, 0, 0},
            .source_box = std::array<uint64_t, 6>{source_offset, 0, 0, source_offset + size, 1, 1}});
#endif
   return false;
}

bool PreResolveSubresource([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] ID3D12Resource*& destination, [[maybe_unused]] UINT destination_subresource,
   [[maybe_unused]] ID3D12Resource*& source, [[maybe_unused]] UINT source_subresource, [[maybe_unused]] DXGI_FORMAT& format)
{
#if DEVELOPMENT
   if (Trace::Capture::IsCapturing())
      Trace::Capture::D3D12::AddCopy(list, "ResolveSubresource", destination, source,
         {.kind = Trace::Capture::CopyKind::Resolve, .source_subresource = source_subresource, .destination_subresource = destination_subresource, .resolve_format = uint32_t(format)});
#endif
   ID3D12Resource* const game_source = source;
   Resources::Mirrors::D3D12::CopyRegion region = {};
   if (Resources::Mirrors::IsSwapping())
      Resources::Mirrors::D3D12::SwapCopy(list, destination, source, region);
   if (source != game_source)
      format = Resources::Mirrors::GetResolveFormat(format, game_source->GetDesc().Format, source->GetDesc().Format);
   return false;
}

bool PreClearRenderTargetView([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] D3D12_CPU_DESCRIPTOR_HANDLE& view, [[maybe_unused]] const FLOAT color[4],
   [[maybe_unused]] UINT rect_count, [[maybe_unused]] const D3D12_RECT*& rects)
{
#if DEVELOPMENT
   if (Trace::Capture::IsCapturing())
      Trace::Capture::D3D12::AddClear(list, "ClearRenderTargetView", view, {color[0], color[1], color[2], color[3]});
#endif
   if (Resources::Mirrors::IsSwapping())
      Resources::Mirrors::D3D12::SwapHandle(view, rect_count, &rects);
   return false;
}

bool PreClearDepthStencilView([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] D3D12_CPU_DESCRIPTOR_HANDLE& view, [[maybe_unused]] D3D12_CLEAR_FLAGS flags,
   [[maybe_unused]] FLOAT depth, [[maybe_unused]] UINT8 stencil, [[maybe_unused]] UINT rect_count, [[maybe_unused]] const D3D12_RECT*& rects)
{
#if DEVELOPMENT
   if (Trace::Capture::IsCapturing())
      Trace::Capture::D3D12::AddClear(list, "ClearDepthStencilView", view,
         {(flags & D3D12_CLEAR_FLAG_DEPTH) ? depth : 0.f, (flags & D3D12_CLEAR_FLAG_STENCIL) ? float(stencil) : 0.f, 0.f, 0.f});
#endif
   if (Resources::Mirrors::IsSwapping())
      Resources::Mirrors::D3D12::SwapHandle(view, rect_count, &rects);
   return false;
}

bool PreClearUnorderedAccessView([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] D3D12_GPU_DESCRIPTOR_HANDLE& gpu, [[maybe_unused]] D3D12_CPU_DESCRIPTOR_HANDLE& cpu,
   [[maybe_unused]] ID3D12Resource*& resource, [[maybe_unused]] ClearValues values, [[maybe_unused]] UINT rect_count, [[maybe_unused]] const D3D12_RECT* rects)
{
#if DEVELOPMENT
   if (Trace::Capture::IsCapturing())
   {
      const std::array<float, 4> cleared = values.float_values
         ? std::array<float, 4>{values.float_values[0], values.float_values[1], values.float_values[2], values.float_values[3]}
         : std::array<float, 4>{float(values.uint_values[0]), float(values.uint_values[1]), float(values.uint_values[2]), float(values.uint_values[3])};
      Trace::Capture::D3D12::AddClear(list, values.float_values ? "ClearUnorderedAccessViewFloat" : "ClearUnorderedAccessViewUint", resource, cleared);
   }
#endif
   if (Resources::Mirrors::IsSwapping())
      Resources::Mirrors::D3D12::SwapClearUnorderedAccess(list, gpu, cpu, resource);
   return false;
}

bool PreDraw(ID3D12GraphicsCommandList* list, const Game::D3D12::DrawCall& draw)
{
   return RunCall(list, draw);
}

bool PreDispatch(ID3D12GraphicsCommandList* list, const Game::D3D12::DispatchCall& dispatch)
{
   return RunCall(list, dispatch);
}

// Its draws or dispatches use the pipeline bound: a draw or a dispatch by the pipeline's kind. A command signature changes only the
// game's root arguments.
bool PreExecuteIndirect(ID3D12GraphicsCommandList* list, const Game::D3D12::IndirectArguments& arguments)
{
#if DEVELOPMENT
   Trace::Stats::D3D12::RecordExecuteIndirect();
#endif
   const Shaders::Variants::D3D12::BoundPipeline bound = Shaders::Variants::D3D12::GetBoundPipeline(list);
   if (bound.tracked && bound.tracked->shaders[size_t(ShaderStage::Compute)])
      return RunCall(list, Game::D3D12::DispatchCall{.indirect_call = true, .indirect = arguments});
   return RunCall(list, Game::D3D12::DrawCall{.function = Game::D3D12::DrawFunction::ExecuteIndirect, .indirect = arguments});
}

// A bundle inherits the list's root arguments: Luma's cbuffers are bound for the variants it may bind
void PreExecuteBundle(ID3D12GraphicsCommandList* list, [[maybe_unused]] ID3D12GraphicsCommandList* bundle)
{
#if DEVELOPMENT
   Trace::Stats::D3D12::RecordExecuteBundle();
   if (Trace::Capture::IsCapturing())
      Trace::Capture::D3D12::RecordExecuteBundle(list, bundle);
#endif
   Resources::Database::D3D12::BindForBundle(list);
}

void PresentD3D12(ID3D12CommandQueue* queue)
{
   if (Resources::Mirrors::IsNeeded() && queue)
   {
      Microsoft::WRL::ComPtr<ID3D12Device> device;
      if (SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&device))))
         Resources::Mirrors::D3D12::OnPresent(device.Get());
   }
   if (!Shaders::IsNeeded())
      return;
   Shaders::Variants::D3D12::UpdateVariants();
   Shaders::PipelineLibrary::D3D12::Save();
}
} // namespace Handlers
} // namespace Luma
