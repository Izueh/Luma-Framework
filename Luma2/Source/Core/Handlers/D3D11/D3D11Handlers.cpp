#include "Handlers/D3D11/D3D11Handlers.hpp"

#include <optional>

#include "Draw/D3D11/D3D11Draw.hpp"
#include "Draw/Draw.hpp"
#include "Frame/Frame.hpp"
#include "Game/D3D11/D3D11DrawEvents.hpp"
#include "Game/Registry.hpp"
#include "Handlers/DXGI/DXGIHandlers.hpp"
#include "Hooks/DeviceProxy.hpp"
#include "Luma.hpp"
#include "Resources/D3D11/D3D11LumaCBuffers.hpp"
#include "Resources/D3D11/D3D11ResourceDatabase.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Shaders/D3D11/D3D11Database.hpp"
#include "Shaders/D3D11/D3D11Variants.hpp"
#include "Shaders/Shaders.hpp"
#if DEVELOPMENT
#include "Trace/Capture.hpp"
#include "Trace/D3D11/D3D11Capture.hpp"
#include "Trace/Track.hpp"
#endif
#include "UI/Overlay.hpp"
#include "Utils/Hash.hpp"
#include "Utils/Log.hpp"
#include "Validation/D3D11/D3D11Validation.hpp"

namespace Luma {
namespace Handlers {
namespace {
const Game::D3D11::Events& GameEvents()
{
   return Game::Registry::GetD3D11Events();
}

const Game::D3D11::Callbacks& GameCallbacks()
{
   return Game::Registry::GetD3D11Callbacks();
}

bool IsRestoringGameBuffers()
{
   return Resources::LumaCBuffers::GetOptions().restore_after_draw;
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

// The context's state went back to the defaults
void ForgetContextState(ID3D11DeviceContext* context)
{
   if (Shaders::IsNeeded())
      Shaders::Variants::D3D11::ForgetBoundShaders(context);
   if (Draw::GetRecorded().Any())
      Draw::D3D11::ForgetBoundState(context);
   if (IsRestoringGameBuffers())
      Resources::LumaCBuffers::D3D11::ForgetGameBuffers(context);
}

// With the game's buffers restored after draws, after Luma's cbuffers were bound on `stages`
// (by the fulfil step for a call the API is about to run, or by a callback's BindLumaShader): Luma issues the call itself unless a
// callback ran or skipped it, and binds the game's buffers back at its slots. Whether the call was handled.
bool RestoreGameBuffers(const Game::D3D11::Call& call, Shaders::StageSet stages, bool handled)
{
   if (!stages.Any() || !IsRestoringGameBuffers())
      return handled;
   if (!handled)
      Game::D3D11::RunOriginal(call);
   Resources::LumaCBuffers::D3D11::RestoreGameBuffers(call.context, stages);
   return true;
}

// Before the events: a patch's resources, as 1.0 bound a recipe's before OnDrawOrDispatchCustomPipeline, so a callback sees the
// call as it will run (a patch missing one already gave way to the game's shader). The stages where something was bound.
Shaders::StageSet FulfilBeforeEvents(ID3D11DeviceContext* context, Shaders::StageSet stages)
{
   if (!Shaders::IsNeeded() || !IsEnabled())
      return {};
   return Shaders::Variants::D3D11::BindPatchResources(context, stages);
}

// After the callbacks, which may have bound other shaders: the variants' requests are bound only for a call that runs as the game
// made it, and not when a callback bound Luma's cbuffers itself, nor again where a patch's were bound before the events. The
// stages where Luma's cbuffers are bound.
Shaders::StageSet FulfilAfterEvents(ID3D11DeviceContext* context, Shaders::StageSet stages, const Game::DrawEvents::D3D11::Result& events, bool skipped,
   Shaders::StageSet before)
{
   if (events.luma_stages.Any())
      return events.luma_stages | before;
   if (events.result != Game::DrawResult::None || skipped || !Shaders::IsNeeded())
      return before;
   return Shaders::Variants::D3D11::BindRequestedResources(context, stages - before) | before;
}
} // namespace

void PreCreateDevice(UINT& flags, const D3D_FEATURE_LEVEL*& feature_levels, UINT& feature_level_count)
{
   (void)feature_levels;
   (void)feature_level_count;
   flags = Validation::D3D11::GetCreateFlags(flags);
}

void PostCreateDevice(ID3D11Device* device)
{
   // First, so the debug layer's messages from what follows are read
   Validation::D3D11::WatchDeviceMessages(device);
   Utils::Log::Info("D3D11: device {} created (feature level 0x{:X}, {} live)", static_cast<void*>(device), uint32_t(device->GetFeatureLevel()),
      Hooks::DeviceProxy::GetCount());
   Game::Registry::CreateDeviceData(device);
   Frame::CreateDeviceState(device);
   Shaders::SetApi(Api::D3D11);
   // Before the game's callback, which may create shaders: their variants and Luma's objects are found in these stores
   if (Shaders::IsNeeded())
      Shaders::Variants::D3D11::CreateVariantStore(device);
   Resources::Database::D3D11::CreateDeviceObjects(device);
   // After Core's objects, which the game's may use
   Game::Registry::Call(GameEvents().on_init_device, device);
}

void OnDeviceDestroyed(ID3D11Device* device)
{
   Utils::Log::Info("D3D11: device {} released by the game ({} live)", static_cast<void*>(device), Hooks::DeviceProxy::GetCount());
   // First, while Core's objects are still there
   Game::Registry::Call(GameEvents().on_destroy_device, device);
   // Luma's objects on the device hold it: released now, not with the device
   if (Shaders::IsNeeded())
      Shaders::Variants::D3D11::ReleaseVariants(device);
   Resources::Database::D3D11::ReleaseDeviceObjects(device);
   UI::DetachFromDevice(device);
   ForgetSwapchains(device);
   // Last, so the messages of the releases above are read
   Validation::D3D11::StopWatchingDevice(device);
}

void PreCreateBuffer(ID3D11Device* device, D3D11_BUFFER_DESC& desc, const D3D11_SUBRESOURCE_DATA*& initial_data)
{
   Game::Registry::Call(GameCallbacks().pre_create_buffer, device, desc, initial_data);
}

void PostCreateBuffer(ID3D11Device* device, const D3D11_BUFFER_DESC& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Buffer* buffer)
{
   Game::Registry::Call(GameCallbacks().post_create_buffer, device, desc, initial_data, buffer);
}

void PreCreateTexture1D(ID3D11Device*, D3D11_TEXTURE1D_DESC&, const D3D11_SUBRESOURCE_DATA*&) {}

void PostCreateTexture1D(ID3D11Device*, const D3D11_TEXTURE1D_DESC&, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture1D*) {}

void PreCreateTexture2D(ID3D11Device* device, D3D11_TEXTURE2D_DESC1& desc, const D3D11_SUBRESOURCE_DATA*& initial_data)
{
   Game::Registry::Call(GameCallbacks().pre_create_texture_2d, device, desc, initial_data);
}

void PostCreateTexture2D(ID3D11Device* device, const D3D11_TEXTURE2D_DESC1& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture2D* texture)
{
   Game::Registry::Call(GameCallbacks().post_create_texture_2d, device, desc, initial_data, texture);
}

void PreCreateTexture3D(ID3D11Device*, D3D11_TEXTURE3D_DESC1&, const D3D11_SUBRESOURCE_DATA*&) {}

void PostCreateTexture3D(ID3D11Device*, const D3D11_TEXTURE3D_DESC1&, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture3D*) {}

void PostOpenSharedResource(ID3D11Device*, ID3D11Resource*) {}

void OnResourceDestroyed(ID3D11Device* device, const ID3D11Resource* resource)
{
   Game::Registry::Call(GameCallbacks().on_resource_destroyed, device, resource);
}

void PreCreateShaderResourceView(ID3D11Device*, ID3D11Resource*, std::optional<D3D11_SHADER_RESOURCE_VIEW_DESC1>&) {}

void PostCreateShaderResourceView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_SHADER_RESOURCE_VIEW_DESC1* desc, ID3D11ShaderResourceView* view)
{
   Game::Registry::Call(GameCallbacks().post_create_shader_resource_view, device, resource, desc, view);
}

void PreCreateRenderTargetView(ID3D11Device*, ID3D11Resource*, std::optional<D3D11_RENDER_TARGET_VIEW_DESC1>&) {}

void PostCreateRenderTargetView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_RENDER_TARGET_VIEW_DESC1* desc, ID3D11RenderTargetView* view)
{
   Game::Registry::Call(GameCallbacks().post_create_render_target_view, device, resource, desc, view);
}

void PreCreateUnorderedAccessView(ID3D11Device*, ID3D11Resource*, std::optional<D3D11_UNORDERED_ACCESS_VIEW_DESC1>&) {}

void PostCreateUnorderedAccessView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC1* desc, ID3D11UnorderedAccessView* view)
{
   Game::Registry::Call(GameCallbacks().post_create_unordered_access_view, device, resource, desc, view);
}

void PreCreateDepthStencilView(ID3D11Device*, ID3D11Resource*, std::optional<D3D11_DEPTH_STENCIL_VIEW_DESC>&) {}

void PostCreateDepthStencilView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc, ID3D11DepthStencilView* view)
{
   Game::Registry::Call(GameCallbacks().post_create_depth_stencil_view, device, resource, desc, view);
}

void OnViewDestroyed(ID3D11Device* device, const ID3D11View* view)
{
   Game::Registry::Call(GameCallbacks().on_view_destroyed, device, view);
}

void PreCreateSamplerState(ID3D11Device* device, D3D11_SAMPLER_DESC& desc)
{
   Game::Registry::Call(GameCallbacks().pre_create_sampler_state, device, desc);
}

void PostCreateSamplerState(ID3D11Device*, const D3D11_SAMPLER_DESC&, ID3D11SamplerState*) {}

void OnSamplerDestroyed(ID3D11Device*, const ID3D11SamplerState*) {}

void PreCreateBlendState(ID3D11Device* device, D3D11_BLEND_DESC1& desc)
{
   Game::Registry::Call(GameCallbacks().pre_create_blend_state, device, desc);
}

void PostCreateBlendState(ID3D11Device*, const D3D11_BLEND_DESC1&, ID3D11BlendState*) {}

// Nothing changes the game's bytecode, which is what Core hashes: shaders are changed through variants
void PreCreateShader(ID3D11Device*, ShaderStage, const void*&, SIZE_T&) {}

void PostCreateShader(ID3D11Device* device, ShaderStage stage, const void* bytecode, SIZE_T length, bool stream_output, ID3D11DeviceChild* shader)
{
   const std::span<const uint8_t> code(static_cast<const uint8_t*>(bytecode), length);
   std::optional<uint32_t> hash;
   // The database first: the variant is found by the shader's hash
   if (Shaders::IsNeeded())
   {
      hash = Shaders::Database::D3D11::TrackShader(stage, code, shader);
      Shaders::Variants::D3D11::AttachVariant(device, shader, stream_output);
   }
   if (const auto on_create_shader = GameEvents().on_create_shader)
      on_create_shader(device, stage, hash ? *hash : Utils::Hash::CRC32(code), code, shader);
}

void OnShaderDestroyed(ID3D11Device*, const ID3D11DeviceChild*) {}

void PostCreateInputLayout(ID3D11Device*, const D3D11_INPUT_ELEMENT_DESC*, UINT, const void*, SIZE_T, ID3D11InputLayout*) {}

void OnInputLayoutDestroyed(ID3D11Device*, const ID3D11InputLayout*) {}

bool PreSetShader(ID3D11DeviceContext* context, ShaderStage stage, ID3D11DeviceChild*& shader)
{
   if (Shaders::IsNeeded())
      Shaders::Variants::D3D11::SwapInVariant(context, stage, shader);
   return false;
}

bool PreSetShaderResources(ID3D11DeviceContext*, ShaderStage, UINT, UINT, ID3D11ShaderResourceView**)
{
   return false;
}

bool PreSetConstantBuffers(ID3D11DeviceContext* context, ShaderStage stage, UINT start, UINT count, ID3D11Buffer** buffers, const UINT* first_constant,
   const UINT* num_constants)
{
   // Reported before Core records them (1.0)
   Game::Registry::Call(GameEvents().on_bind_constant_buffers, context, stage, start, count, buffers);
   if (IsRestoringGameBuffers())
      Resources::LumaCBuffers::D3D11::RecordGameBuffers(context, stage, start, count, buffers, first_constant, num_constants);
   return false;
}

bool PreSetSamplers(ID3D11DeviceContext*, ShaderStage, UINT, UINT, ID3D11SamplerState**)
{
   return false;
}

bool PreCSSetUnorderedAccessViews(ID3D11DeviceContext*, UINT, UINT, ID3D11UnorderedAccessView**, const UINT*)
{
   return false;
}

bool PreOMSetRenderTargets(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView** views, ID3D11DepthStencilView*& depth_stencil)
{
   return Game::Registry::Call(GameCallbacks().pre_om_set_render_targets, context, count, views, depth_stencil);
}

bool PreOMSetRenderTargetsAndUnorderedAccessViews(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView** views,
   ID3D11DepthStencilView*& depth_stencil, UINT uav_start, UINT uav_count, ID3D11UnorderedAccessView** uavs, const UINT* initial_counts)
{
   return Game::Registry::Call(GameCallbacks().pre_om_set_render_targets_and_unordered_access_views, context, count, views, depth_stencil, uav_start, uav_count, uavs,
      initial_counts);
}

void PostRSSetViewports(ID3D11DeviceContext* context, UINT count, const D3D11_VIEWPORT* viewports)
{
   if (Draw::GetRecorded().viewports)
      Draw::D3D11::RecordViewports(context, count, viewports);
   Game::Registry::Call(GameEvents().on_bind_viewports, context, count, viewports);
}

void PostRSSetScissorRects(ID3D11DeviceContext* context, UINT count, const D3D11_RECT* rects)
{
   if (Draw::GetRecorded().viewports)
      Draw::D3D11::RecordScissorRects(context, count, rects);
   Game::Registry::Call(GameEvents().on_bind_scissor_rects, context, count, rects);
}

void PostOMSetBlendState(ID3D11DeviceContext* context, ID3D11BlendState* state, const FLOAT blend_factor[4], UINT sample_mask)
{
   if (Draw::GetRecorded().blend)
      Draw::D3D11::RecordBlendState(context, state, blend_factor, sample_mask);
   Game::Registry::Call(GameEvents().on_bind_blend_state, context, state, blend_factor, sample_mask);
}

void PostOMSetDepthStencilState(ID3D11DeviceContext* context, ID3D11DepthStencilState* state, UINT stencil_ref)
{
   if (Draw::GetRecorded().depth)
      Draw::D3D11::RecordDepthStencilState(context, state, stencil_ref);
   Game::Registry::Call(GameEvents().on_bind_depth_stencil_state, context, state, stencil_ref);
}

void PostIASetVertexBuffers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11Buffer* const* buffers, const UINT* strides, const UINT* offsets)
{
   Game::Registry::Call(GameCallbacks().post_ia_set_vertex_buffers, context, start, count, buffers, strides, offsets);
}

void PostIASetIndexBuffer(ID3D11DeviceContext* context, ID3D11Buffer* buffer, DXGI_FORMAT format, UINT offset)
{
   Game::Registry::Call(GameCallbacks().post_ia_set_index_buffer, context, buffer, format, offset);
}

void PostClearState(ID3D11DeviceContext* context)
{
   ForgetContextState(context);
   Game::Registry::Call(GameEvents().on_clear_state, context);
}

void PostMap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE& mapped)
{
   Game::Registry::Call(GameCallbacks().post_map, context, resource, subresource, type, flags, mapped);
}

bool PreUnmap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource)
{
   return Game::Registry::Call(GameCallbacks().pre_unmap, context, resource, subresource);
}

void PostUnmap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource)
{
   Game::Registry::Call(GameCallbacks().post_unmap, context, resource, subresource);
}

bool PreUpdateSubresource(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, const D3D11_BOX* box, const void* data,
   UINT row_pitch, UINT depth_pitch, UINT copy_flags)
{
   return Game::Registry::Call(GameCallbacks().pre_update_subresource, context, resource, subresource, box, data, row_pitch, depth_pitch, copy_flags);
}

bool PreCopyResource(ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source)
{
   return Game::Registry::Call(GameCallbacks().pre_copy_resource, context, destination, source);
}

bool PreCopySubresourceRegion(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, UINT x, UINT y, UINT z,
   ID3D11Resource* source, UINT source_subresource, const D3D11_BOX* box, UINT copy_flags)
{
   return Game::Registry::Call(GameCallbacks().pre_copy_subresource_region, context, destination, destination_subresource, x, y, z, source, source_subresource, box,
      copy_flags);
}

bool PreResolveSubresource(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, ID3D11Resource* source,
   UINT source_subresource, DXGI_FORMAT format)
{
   return Game::Registry::Call(GameCallbacks().pre_resolve_subresource, context, destination, destination_subresource, source, source_subresource, format);
}

bool PreClearRenderTargetView(ID3D11DeviceContext* context, ID3D11RenderTargetView* view, const FLOAT color[4])
{
   return Game::Registry::Call(GameCallbacks().pre_clear_render_target_view, context, view, color);
}

bool PreCopyStructureCount(ID3D11DeviceContext* context, ID3D11Buffer* destination, UINT destination_offset, ID3D11UnorderedAccessView* source)
{
   return Game::Registry::Call(GameCallbacks().pre_copy_structure_count, context, destination, destination_offset, source);
}

bool PreClearDepthStencilView(ID3D11DeviceContext*, ID3D11DepthStencilView*, UINT, FLOAT, UINT8)
{
   return false;
}

bool PreClearUnorderedAccessViewUint(ID3D11DeviceContext*, ID3D11UnorderedAccessView*, const UINT[4])
{
   return false;
}

bool PreClearUnorderedAccessViewFloat(ID3D11DeviceContext*, ID3D11UnorderedAccessView*, const FLOAT[4])
{
   return false;
}

bool PreClearView(ID3D11DeviceContext* context, ID3D11View* view, const FLOAT color[4], const D3D11_RECT* rects, UINT rect_count)
{
   return Game::Registry::Call(GameCallbacks().pre_clear_view, context, view, color, rects, rect_count);
}

// The trace records the state before the game's callback changes it (1.0), and what the callback did with the call
bool PreDraw(ID3D11DeviceContext* context, const DrawCall& draw)
{
#if DEVELOPMENT
   std::optional<Trace::Capture::D3D11::Recording> recording;
   if (Trace::Capture::IsCapturing())
      recording = Trace::Capture::D3D11::BeginDraw(context, draw);
   if (Trace::Track::IsActive())
      Trace::Capture::D3D11::TrackCall(context, Shaders::GRAPHICS_STAGES);
#endif
   const Shaders::StageSet before = FulfilBeforeEvents(context, Shaders::GRAPHICS_STAGES);
   const Game::DrawEvents::D3D11::Result events = Game::Registry::HasDrawCallbacks() ? Game::DrawEvents::D3D11::Run(context, draw) : Game::DrawEvents::D3D11::Result();
#if DEVELOPMENT
   const bool skipped = events.result == Game::DrawResult::None && Trace::Capture::D3D11::IsSkipped(context, Shaders::GRAPHICS_STAGES);
#else
   constexpr bool skipped = false;
#endif
   const Shaders::StageSet bound = FulfilAfterEvents(context, Shaders::GRAPHICS_STAGES, events, skipped, before);
   const bool handled = RestoreGameBuffers({.context = context, .draw = &draw}, bound, events.result != Game::DrawResult::None || skipped);
#if DEVELOPMENT
   if (recording)
      Trace::Capture::D3D11::End(std::move(*recording), GetOutcome(events.result, skipped));
#endif
   return handled;
}

bool PreDispatch(ID3D11DeviceContext* context, const DispatchCall& dispatch)
{
#if DEVELOPMENT
   std::optional<Trace::Capture::D3D11::Recording> recording;
   if (Trace::Capture::IsCapturing())
      recording = Trace::Capture::D3D11::BeginDispatch(context, dispatch);
   if (Trace::Track::IsActive())
      Trace::Capture::D3D11::TrackCall(context, Shaders::COMPUTE_STAGES);
#endif
   const Shaders::StageSet before = FulfilBeforeEvents(context, Shaders::COMPUTE_STAGES);
   const Game::DrawEvents::D3D11::Result events =
      Game::Registry::HasDrawCallbacks() ? Game::DrawEvents::D3D11::Run(context, dispatch) : Game::DrawEvents::D3D11::Result();
#if DEVELOPMENT
   const bool skipped = events.result == Game::DrawResult::None && Trace::Capture::D3D11::IsSkipped(context, Shaders::COMPUTE_STAGES);
#else
   constexpr bool skipped = false;
#endif
   const Shaders::StageSet bound = FulfilAfterEvents(context, Shaders::COMPUTE_STAGES, events, skipped, before);
   const bool handled = RestoreGameBuffers({.context = context, .dispatch = &dispatch}, bound, events.result != Game::DrawResult::None || skipped);
#if DEVELOPMENT
   if (recording)
      Trace::Capture::D3D11::End(std::move(*recording), GetOutcome(events.result, skipped));
#endif
   return handled;
}

void PostCreateDeferredContext(ID3D11Device* device, ID3D11DeviceContext* context)
{
   Game::Registry::Call(GameCallbacks().post_create_deferred_context, device, context);
}

void OnDeferredContextDestroyed(ID3D11Device* device, const ID3D11DeviceContext* context)
{
   Game::Registry::Call(GameCallbacks().on_deferred_context_destroyed, device, context);
}

void PostFinishCommandList(ID3D11DeviceContext* context, BOOL restore, ID3D11CommandList* list)
{
   if (!restore)
      ForgetContextState(context);
   if (Shaders::IsNeeded())
      Resources::Database::D3D11::ForgetUploads(context);
#if DEVELOPMENT
   if (Trace::Track::IsActive())
      Trace::Capture::D3D11::OnFinishCommandList(context, list);
#endif
   Game::Registry::Call(GameEvents().on_finish_command_list, context, restore, list);
}

void OnCommandListDestroyed(ID3D11Device*, const ID3D11CommandList*) {}

bool PreExecuteCommandList(ID3D11DeviceContext* context, ID3D11CommandList* list, BOOL restore)
{
   // Before Core's handling (1.0): the game may execute its own lists or passes first, or skip this one
   if (Game::Registry::Call(GameEvents().on_execute_command_list, context, list, restore))
      return true;
#if DEVELOPMENT
   // Executed right after this, before the present reads the copy back
   if (Trace::Track::IsActive())
      Trace::Capture::D3D11::OnExecuteCommandList(context, list);
#endif
   // Without restoring, the execution leaves the context's state at the defaults
   if (!restore)
      ForgetContextState(context);
   if (Shaders::IsNeeded())
      Resources::Database::D3D11::ForgetUploads(context);
   return false;
}

bool PreFlush(ID3D11DeviceContext*)
{
   return false;
}
} // namespace Handlers
} // namespace Luma
