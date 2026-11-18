#include "Handlers/D3D12/D3D12Handlers.hpp"

#include <optional>
#include <type_traits>

#include "Frame/Frame.hpp"
#include "Game/Registry.hpp"
#include "Handlers/DXGI/DXGIHandlers.hpp"
#include "Hooks/D3D12/D3D12Lifetime.hpp"
#include "Hooks/DeviceProxy.hpp"
#include "Resources/D3D12/D3D12HeapTail.hpp"
#include "Resources/D3D12/D3D12ResourceDatabase.hpp"
#include "Shaders/D3D12/D3D12PipelineLibrary.hpp"
#include "Shaders/D3D12/D3D12Variants.hpp"
#include "Shaders/Shaders.hpp"
#include "Game/D3D12/D3D12DrawEvents.hpp"
#include "Resources/D3D12/D3D12Descriptors.hpp"
#if DEVELOPMENT
#include "Trace/Capture.hpp"
#include "Trace/D3D12/D3D12Capture.hpp"
#include "Trace/D3D12/D3D12Readback.hpp"
#include "Trace/D3D12/D3D12Stats.hpp"
#include "Trace/Readback.hpp"
#include "Trace/Track.hpp"
#endif
#include "UI/Overlay.hpp"
#include "Utils/Debug.hpp"
#include "Utils/Log.hpp"
#include "Validation/D3D12/D3D12Validation.hpp"

namespace Luma {
namespace Handlers {
namespace {
using Shaders::Database::D3D12::PipelineKind;

Shaders::Database::D3D12::PipelineDesc Describe(PipelineKind kind, const void* desc)
{
   switch (kind)
   {
   case PipelineKind::Graphics: return Shaders::Database::D3D12::Describe(*static_cast<const D3D12_GRAPHICS_PIPELINE_STATE_DESC*>(desc));
   case PipelineKind::Compute: return Shaders::Database::D3D12::Describe(*static_cast<const D3D12_COMPUTE_PIPELINE_STATE_DESC*>(desc));
   default: return Shaders::Database::D3D12::Describe(*static_cast<const D3D12_PIPELINE_STATE_STREAM_DESC*>(desc));
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

#if DEVELOPMENT
Trace::Capture::Outcome GetOutcome(Game::DrawResult result, bool skipped)
{
   if (skipped)
      return Trace::Capture::Outcome::SkippedByTrace;
   switch (result)
   {
   case Game::DrawResult::Skip: return Trace::Capture::Outcome::Skipped;
   case Game::DrawResult::Replaced: return Trace::Capture::Outcome::Replaced;
   default: return Trace::Capture::Outcome::Ran;
   }
}
#endif

// As D3D11's: the trace records the state before the game's events change it, then the events, the trace's skip, and the variant's
// requests for a call that runs as the game made it. Whether the game's call doesn't run.
template <typename Call>
bool RunCall(ID3D12GraphicsCommandList* list, const Call& call)
{
   constexpr bool dispatch = std::is_same_v<Call, Game::D3D12::DispatchCall>;
   constexpr Shaders::StageSet stages = dispatch ? Shaders::COMPUTE_STAGES : Shaders::GRAPHICS_STAGES;
#if DEVELOPMENT
   Trace::Stats::D3D12::RecordDraw(list, dispatch);
   std::optional<Trace::Capture::D3D12::Recording> recording;
   if (Trace::Capture::IsCapturing())
   {
      if constexpr (dispatch)
         recording = Trace::Capture::D3D12::BeginDispatch(list, call);
      else
         recording = Trace::Capture::D3D12::BeginDraw(list, call);
   }
   if (Trace::Track::IsActive())
      Trace::Capture::D3D12::TrackCall(list, stages);
#endif
   const Game::DrawResult result = Game::Registry::HasD3D12DrawCallbacks() ? Game::DrawEvents::D3D12::Run(list, call) : Game::DrawResult::None;
#if DEVELOPMENT
   const bool skipped = result == Game::DrawResult::None && Trace::Capture::D3D12::IsSkipped(list, stages);
#else
   constexpr bool skipped = false;
#endif
   if (result == Game::DrawResult::None && !skipped)
      BindVariantRequests(list);
   bool handled = result != Game::DrawResult::None || skipped;
#if DEVELOPMENT
   // Last: the call as it ran is what's read
   if (Trace::Readback::IsArmed())
   {
      Game::D3D12::Call original = {.list = list};
      if constexpr (dispatch)
         original.dispatch = &call;
      else
         original.draw = &call;
      handled = Trace::Readback::D3D12::ReadAfterCall(original, stages, handled);
   }
   if (recording)
      Trace::Capture::D3D12::End(std::move(*recording), GetOutcome(result, skipped));
#endif
   return handled;
}
} // namespace

void PreCreateDevice()
{
   Validation::D3D12::EnableDebugLayer();
}

void PostCreateDevice(ID3D12Device* device)
{
   Validation::D3D12::WatchDeviceMessages(device);
   Utils::Log::Info("D3D12: device {} created ({} live)", static_cast<void*>(device), Hooks::DeviceProxy::GetCount());
   Game::Registry::CreateDeviceData(device);
   Frame::CreateDeviceState(device);
   Shaders::SetApi(Api::D3D12);
   if (Shaders::IsNeeded())
      Resources::Database::D3D12::CreateDeviceObjects(device);
#if DEVELOPMENT
   Trace::Stats::D3D12::RecordDevice(device);
#endif
   Game::Registry::Call(Game::Registry::GetD3D12Events().on_init_device, device);
}

void OnDeviceDestroyed(ID3D12Device* device)
{
   Utils::Log::Info("D3D12: device {} released by the game ({} live)", static_cast<void*>(device), Hooks::DeviceProxy::GetCount());
   Game::Registry::Call(Game::Registry::GetD3D12Events().on_destroy_device, device);
   UI::DetachFromDevice(device);
   ForgetSwapchains(device);
   // Luma's objects hold the device
   Resources::Database::D3D12::ReleaseDeviceObjects(device);
   Shaders::RootSignatures::D3D12::ReleaseDeviceObjects(device);
   Shaders::PipelineLibrary::D3D12::ReleaseDeviceObjects(device);
   Validation::D3D12::StopWatchingDevice(device);
}

void PostExecuteCommandLists(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
{
   Hooks::Lifetime::D3D12::MoveReferencesToQueue(queue, count, lists);
#if DEVELOPMENT
   Trace::Capture::D3D12::OnExecute(queue, count, lists);
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
   Utils::Log::Error("D3D12: the device refused an extended root signature (0x{:08X}), the game's is used and its pipelines get no variants", uint32_t(result));
}

void PostCreateRootSignature(std::span<const uint8_t> blob, ID3D12RootSignature* root_signature, uint32_t luma_parameter, bool luma_table)
{
   if (!Shaders::IsNeeded())
      return;
   [[maybe_unused]] const Shaders::Database::D3D12::RootLayout* layout = Shaders::Database::D3D12::TrackRootSignature(blob, root_signature, luma_parameter, luma_table);
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

// Descriptors are tracked for trace (Development)
void PostCreateDescriptorHeap(ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC& desc, ID3D12DescriptorHeap* heap, bool with_tail)
{
   if (with_tail)
      Resources::HeapTail::D3D12::Track(device, heap, desc);
#if DEVELOPMENT
   Resources::Descriptors::D3D12::TrackHeap(device, heap, desc);
   Trace::Stats::D3D12::RecordHeap(desc, heap);
#endif
}

void PostCreateView([[maybe_unused]] D3D12_CPU_DESCRIPTOR_HANDLE destination, [[maybe_unused]] const Resources::Descriptors::D3D12::Descriptor& descriptor)
{
#if DEVELOPMENT
   Resources::Descriptors::D3D12::Write(destination, descriptor);
   Trace::Stats::D3D12::RecordViewWrite(destination);
#endif
}

void PostCopyDescriptors([[maybe_unused]] D3D12_CPU_DESCRIPTOR_HANDLE destination, [[maybe_unused]] D3D12_CPU_DESCRIPTOR_HANDLE source, [[maybe_unused]] UINT count)
{
#if DEVELOPMENT
   Resources::Descriptors::D3D12::Copy(destination, source, count);
   Trace::Stats::D3D12::RecordCopy(destination, count);
#endif
}

void PostCreateResource([[maybe_unused]] ID3D12Resource* resource)
{
#if DEVELOPMENT
   Resources::Descriptors::D3D12::TrackResource(resource);
#endif
}

void PreReset(ID3D12GraphicsCommandList* list, ID3D12PipelineState*& pipeline)
{
   // What the list recorded before is the queues' to release now, and it starts with nothing of Luma's
   Hooks::Lifetime::D3D12::ForgetReferences(list);
   Resources::Database::D3D12::ForgetList(list);
   Shaders::Variants::D3D12::SwapInVariant(list, pipeline);
#if DEVELOPMENT
   Trace::Capture::D3D12::OnReset(list);
   Trace::Stats::D3D12::RecordReset(list);
   if (pipeline)
      Trace::Stats::D3D12::RecordBind(list, Trace::Stats::D3D12::Bind::PipelineState);
#endif
}

void PreClearState(ID3D12GraphicsCommandList* list, ID3D12PipelineState*& pipeline)
{
   Resources::Database::D3D12::ForgetList(list);
   Shaders::Variants::D3D12::SwapInVariant(list, pipeline);
#if DEVELOPMENT
   Trace::Capture::D3D12::OnClearState(list);
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
#if DEVELOPMENT
   Trace::Capture::D3D12::OnSetRootSignature(list, compute, root_signature);
   Trace::Stats::D3D12::RecordBind(list, Trace::Stats::D3D12::Bind::RootSignature);
#endif
}

void PreOMSetRenderTargets([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] UINT count, [[maybe_unused]] const D3D12_CPU_DESCRIPTOR_HANDLE* render_targets,
   [[maybe_unused]] BOOL single_range, [[maybe_unused]] const D3D12_CPU_DESCRIPTOR_HANDLE* depth_stencil)
{
#if DEVELOPMENT
   Trace::Capture::D3D12::OnSetRenderTargets(list, count, render_targets, single_range, depth_stencil);
   Trace::Stats::D3D12::RecordBind(list, Trace::Stats::D3D12::Bind::RenderTargets);
#endif
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
   Trace::Capture::D3D12::OnSetRootTable(list, compute, index, table);
#endif
}

void PreSetRootDescriptor([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] bool compute, [[maybe_unused]] UINT index,
   [[maybe_unused]] D3D12_GPU_VIRTUAL_ADDRESS address)
{
#if DEVELOPMENT
   Trace::Capture::D3D12::OnSetRootDescriptor(list, compute, index, address);
#endif
}

void PreSetRootConstants([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] bool compute, [[maybe_unused]] UINT index, [[maybe_unused]] UINT count,
   [[maybe_unused]] const void* values, [[maybe_unused]] UINT offset)
{
#if DEVELOPMENT
   Trace::Capture::D3D12::OnSetRootConstants(list, compute, index, count, values, offset);
#endif
}

void PreSetViewports([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] UINT count, [[maybe_unused]] const D3D12_VIEWPORT* viewports)
{
#if DEVELOPMENT
   Trace::Capture::D3D12::OnSetViewports(list, count, viewports);
#endif
}

void PreSetScissorRects([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] UINT count, [[maybe_unused]] const D3D12_RECT* rects)
{
#if DEVELOPMENT
   Trace::Capture::D3D12::OnSetScissorRects(list, count, rects);
#endif
}

void PreSetTopology([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] D3D12_PRIMITIVE_TOPOLOGY topology)
{
#if DEVELOPMENT
   Trace::Capture::D3D12::OnSetTopology(list, topology);
#endif
}

void PreSetBlendFactor([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] const FLOAT factor[4])
{
#if DEVELOPMENT
   Trace::Capture::D3D12::OnSetBlendFactor(list, factor);
#endif
}

void PreSetStencilRef([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] UINT stencil_ref)
{
#if DEVELOPMENT
   Trace::Capture::D3D12::OnSetStencilRef(list, stencil_ref);
#endif
}

void PreResourceBarrier([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] UINT count, [[maybe_unused]] const D3D12_RESOURCE_BARRIER* barriers)
{
#if DEVELOPMENT
   Trace::Capture::D3D12::OnResourceBarrier(list, count, barriers);
#endif
}

bool PreCopyResource([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] ID3D12Resource*& destination, [[maybe_unused]] ID3D12Resource*& source)
{
#if DEVELOPMENT
   if (Trace::Capture::IsCapturing())
      Trace::Capture::D3D12::AddCopy(list, "CopyResource", destination, source, {});
#endif
   return false;
}

bool PreCopyTextureRegion([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] D3D12_TEXTURE_COPY_LOCATION& destination, [[maybe_unused]] UINT x,
   [[maybe_unused]] UINT y, [[maybe_unused]] UINT z, [[maybe_unused]] D3D12_TEXTURE_COPY_LOCATION& source, [[maybe_unused]] const D3D12_BOX* box)
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
         copy.source_box = std::array<uint32_t, 6>{box->left, box->top, box->front, box->right, box->bottom, box->back};
      Trace::Capture::D3D12::AddCopy(list, "CopyTextureRegion", destination.pResource, source.pResource, copy);
   }
#endif
   return false;
}

bool PreCopyBufferRegion([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] ID3D12Resource* destination, [[maybe_unused]] UINT64 destination_offset,
   [[maybe_unused]] ID3D12Resource* source, [[maybe_unused]] UINT64 source_offset, [[maybe_unused]] UINT64 size)
{
#if DEVELOPMENT
   if (Trace::Capture::IsCapturing())
      Trace::Capture::D3D12::AddCopy(list, "CopyBufferRegion", destination, source,
         {.kind = Trace::Capture::CopyKind::BufferRegion,
            .destination_offset = {uint32_t(destination_offset), 0, 0},
            .source_box = std::array<uint32_t, 6>{uint32_t(source_offset), 0, 0, uint32_t(source_offset + size), 1, 1}});
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
   return false;
}

bool PreClearRenderTargetView([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] D3D12_CPU_DESCRIPTOR_HANDLE& view, [[maybe_unused]] const FLOAT color[4],
   [[maybe_unused]] UINT rect_count, [[maybe_unused]] const D3D12_RECT* rects)
{
#if DEVELOPMENT
   if (Trace::Capture::IsCapturing())
      Trace::Capture::D3D12::AddClear(list, "ClearRenderTargetView", view, {color[0], color[1], color[2], color[3]});
#endif
   return false;
}

bool PreClearDepthStencilView([[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] D3D12_CPU_DESCRIPTOR_HANDLE& view, [[maybe_unused]] D3D12_CLEAR_FLAGS flags,
   [[maybe_unused]] FLOAT depth, [[maybe_unused]] UINT8 stencil, [[maybe_unused]] UINT rect_count, [[maybe_unused]] const D3D12_RECT* rects)
{
#if DEVELOPMENT
   if (Trace::Capture::IsCapturing())
      Trace::Capture::D3D12::AddClear(list, "ClearDepthStencilView", view,
         {(flags & D3D12_CLEAR_FLAG_DEPTH) ? depth : 0.f, (flags & D3D12_CLEAR_FLAG_STENCIL) ? float(stencil) : 0.f, 0.f, 0.f});
#endif
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
      Trace::Capture::D3D12::OnExecuteBundle(list, bundle);
#endif
   Resources::Database::D3D12::BindForBundle(list);
}

void PresentD3D12()
{
   if (!Shaders::IsNeeded())
      return;
   Shaders::Variants::D3D12::UpdateVariants();
   Shaders::PipelineLibrary::D3D12::Save();
}
} // namespace Handlers
} // namespace Luma
