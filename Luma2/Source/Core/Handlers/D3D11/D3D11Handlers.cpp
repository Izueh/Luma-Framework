#include "Handlers/D3D11/D3D11Handlers.hpp"

#include <format>
#include <optional>
#include <type_traits>

#include "Settings/SettingsRegistry.hpp"
#include "Draw/D3D11/D3D11Draw.hpp"
#include "Draw/Draw.hpp"
#include "Effects/SR/D3D11/D3D11SuperResolution.hpp"
#include "Effects/SR/SuperResolution.hpp"
#include "Game/D3D11/D3D11DrawEvents.hpp"
#include "Game/GameRegistry.hpp"
#include "Handlers/Handlers.hpp"
#include "Hooks/HookScope.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Resources/D3D11/D3D11ConstantCapture.hpp"
#include "Resources/D3D11/D3D11LumaCBuffers.hpp"
#include "Resources/D3D11/D3D11Mirrors.hpp"
#include "Resources/D3D11/D3D11Samplers.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/SamplerMirrors.hpp"
#include "Resources/D3D11/D3D11ResourceDatabase.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Shaders/D3D11/D3D11Database.hpp"
#include "Shaders/D3D11/D3D11Variants.hpp"
#include "Shaders/Shaders.hpp"
#if DEVELOPMENT
#include "Trace/DebugDraw.hpp"
#include "Trace/FrameCapture.hpp"
#include "Trace/D3D11/D3D11Capture.hpp"
#include "Trace/D3D11/D3D11Readback.hpp"
#include "Trace/ResourceReadback.hpp"
#include "Trace/CbufferTracker.hpp"
#endif
#include "UI/Overlay.hpp"
#include "Utils/Hash.hpp"
#include "Log/Log.hpp"
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

void ForgetContextState(ID3D11DeviceContext* context)
{
   if (Shaders::IsNeeded())
      Shaders::Variants::D3D11::ForgetBoundShaders(context);
   if (Draw::GetRecorded().Any())
      Draw::D3D11::ForgetBoundState(context);
   if (IsRestoringGameBuffers())
      Resources::LumaCBuffers::D3D11::ForgetGameBuffers(context);
   if (Resources::Mirrors::IsNeeded())
      Resources::Mirrors::D3D11::ForgetContextState(context);
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

// Before the events: a patch's resources, so a callback sees the call as it will run (a patch missing one already gave way to the game's shader). The stages where something was bound.
Shaders::StageSet FulfilBeforeEvents(ID3D11DeviceContext* context, Shaders::StageSet stages)
{
   if (!Shaders::IsNeeded() || !Settings::IsEnabled())
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

void OnTrackedMirrorResourceDestroyed(IUnknown* device, const IUnknown* resource)
{
   Hooks::Scope::Guard scope;
   OnResourceDestroyed(static_cast<ID3D11Device*>(device), static_cast<const ID3D11Resource*>(resource));
}

void OnTrackedMirrorViewDestroyed(IUnknown* device, const IUnknown* view)
{
   Hooks::Scope::Guard scope;
   OnViewDestroyed(static_cast<ID3D11Device*>(device), static_cast<const ID3D11View*>(view));
}

void TrackMirrorResourceDestruction(ID3D11Device* device, ID3D11Resource* original)
{
   if (ID3D11Resource* mirror = Resources::Mirrors::D3D11::FindMirror(original))
      Hooks::Lifetime::Track(mirror, device, &OnTrackedMirrorResourceDestroyed);
}

void TrackMirrorViewDestruction(ID3D11Device* device, ID3D11View* original)
{
   if (ID3D11View* mirror = Resources::Mirrors::D3D11::FindMirrorView(original))
      Hooks::Lifetime::Track(mirror, device, &OnTrackedMirrorViewDestroyed);
}
} // namespace

void PreCreateDevice(UINT& flags, const D3D_FEATURE_LEVEL*& feature_levels, UINT& feature_level_count)
{
   flags = Validation::D3D11::GetCreateFlags(flags);
   if (Effects::SR::IsNeeded())
      Effects::SR::D3D11::PreCreateDevice(feature_levels, feature_level_count);
}

void PostCreateDevice(ID3D11Device* device)
{
   HandlePostCreateDevice<Api::D3D11>(
      device, std::format("feature level 0x{:X}", uint32_t(device->GetFeatureLevel())),
      [&] { Validation::D3D11::WatchDeviceMessages(device); },
      [&] {
         if (Shaders::IsNeeded())
            Shaders::Variants::D3D11::CreateVariantStore(device);
         Resources::Database::D3D11::CreateDeviceObjects(device);
         if (Effects::SR::IsNeeded())
            Effects::SR::D3D11::CreateDeviceObjects(device);
      });
}

void OnDeviceDestroyed(ID3D11Device* device)
{
   HandleOnDeviceDestroyed<Api::D3D11>(
      device,
      [&] {
#if DEVELOPMENT
         Trace::DebugDraw::ReleaseD3D11(device);
#endif
         Effects::SR::D3D11::ReleaseDeviceObjects(device);
         if (Shaders::IsNeeded())
            Shaders::Variants::D3D11::ReleaseVariants(device);
         Resources::Mirrors::D3D11::ReleaseDeviceObjects(device);
         Resources::Database::D3D11::ReleaseDeviceObjects(device);
      },
      [&] { Validation::D3D11::StopWatchingDevice(device); });
}

void PreCreateBuffer(ID3D11Device* device, D3D11_BUFFER_DESC& desc, const D3D11_SUBRESOURCE_DATA*& initial_data)
{
   Game::Registry::Call(GameEvents().on_create_buffer, device, desc, initial_data);
}

void PostCreateBuffer(ID3D11Device* device, const D3D11_BUFFER_DESC& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Buffer* buffer)
{
   Game::Registry::Call(GameEvents().on_init_buffer, device, desc, initial_data, buffer);
}

void PreCreateTexture1D(ID3D11Device*, D3D11_TEXTURE1D_DESC&, const D3D11_SUBRESOURCE_DATA*&) {}

void PostCreateTexture1D(ID3D11Device* device, const D3D11_TEXTURE1D_DESC& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture1D* texture)
{
   if (Resources::Mirrors::IsNeeded())
   {
      Resources::Mirrors::D3D11::CreateMirror(device, desc, initial_data, texture);
      TrackMirrorResourceDestruction(device, texture);
   }
}

void PreCreateTexture2D(ID3D11Device* device, D3D11_TEXTURE2D_DESC1& desc, const D3D11_SUBRESOURCE_DATA*& initial_data)
{
   Game::Registry::Call(GameEvents().on_create_texture_2d, device, desc, initial_data);
}

void PostCreateTexture2D(ID3D11Device* device, const D3D11_TEXTURE2D_DESC1& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture2D* texture)
{
   ID3D11Texture2D* effective = texture;
   if (Resources::Mirrors::IsNeeded())
   {
      Resources::Mirrors::D3D11::CreateMirror(device, desc, initial_data, texture);
      TrackMirrorResourceDestruction(device, texture);
      effective = static_cast<ID3D11Texture2D*>(Resources::Mirrors::D3D11::GetEffectiveResource(texture));
   }
   Game::Registry::Call(GameEvents().on_init_texture_2d, device, desc, initial_data, effective);
}

void PreCreateTexture3D(ID3D11Device*, D3D11_TEXTURE3D_DESC1&, const D3D11_SUBRESOURCE_DATA*&) {}

void PostCreateTexture3D(ID3D11Device* device, const D3D11_TEXTURE3D_DESC1& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture3D* texture)
{
   if (Resources::Mirrors::IsNeeded())
   {
      Resources::Mirrors::D3D11::CreateMirror(device, desc, initial_data, texture);
      TrackMirrorResourceDestruction(device, texture);
   }
}

void PostOpenSharedResource(ID3D11Device*, ID3D11Resource*) {}

void OnResourceDestroyed(ID3D11Device* device, const ID3D11Resource* resource)
{
   Game::Registry::Call(GameEvents().on_resource_destroyed, device, resource);
}

void PreCreateShaderResourceView(ID3D11Device*, ID3D11Resource*, std::optional<D3D11_SHADER_RESOURCE_VIEW_DESC1>&) {}

void PostCreateShaderResourceView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_SHADER_RESOURCE_VIEW_DESC1* desc, ID3D11ShaderResourceView* view)
{
   ID3D11Resource* effective_resource = resource;
   ID3D11ShaderResourceView* effective_view = view;
   if (Resources::Mirrors::IsNeeded())
   {
      Resources::Mirrors::D3D11::CreateMirrorView(device, resource, desc, view);
      TrackMirrorViewDestruction(device, view);
      effective_resource = Resources::Mirrors::D3D11::GetEffectiveResource(resource);
      effective_view = Resources::Mirrors::D3D11::GetEffectiveView(view);
   }
   else
   {
      Resources::Mirrors::D3D11::TrackView(resource, view);
   }
   Game::Registry::Call(GameEvents().on_init_shader_resource_view, device, effective_resource, desc, effective_view);
}

void PreCreateRenderTargetView(ID3D11Device*, ID3D11Resource*, std::optional<D3D11_RENDER_TARGET_VIEW_DESC1>&) {}

void PostCreateRenderTargetView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_RENDER_TARGET_VIEW_DESC1* desc, ID3D11RenderTargetView* view)
{
   ID3D11Resource* effective_resource = resource;
   ID3D11RenderTargetView* effective_view = view;
   if (Resources::Mirrors::IsNeeded())
   {
      Resources::Mirrors::D3D11::CreateMirrorView(device, resource, desc, view);
      TrackMirrorViewDestruction(device, view);
      effective_resource = Resources::Mirrors::D3D11::GetEffectiveResource(resource);
      effective_view = Resources::Mirrors::D3D11::GetEffectiveView(view);
   }
   else
   {
      Resources::Mirrors::D3D11::TrackView(resource, view);
   }
   Game::Registry::Call(GameEvents().on_init_render_target_view, device, effective_resource, desc, effective_view);
}

void PreCreateUnorderedAccessView(ID3D11Device*, ID3D11Resource*, std::optional<D3D11_UNORDERED_ACCESS_VIEW_DESC1>&) {}

void PostCreateUnorderedAccessView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC1* desc, ID3D11UnorderedAccessView* view)
{
   ID3D11Resource* effective_resource = resource;
   ID3D11UnorderedAccessView* effective_view = view;
   if (Resources::Mirrors::IsNeeded())
   {
      Resources::Mirrors::D3D11::CreateMirrorView(device, resource, desc, view);
      TrackMirrorViewDestruction(device, view);
      effective_resource = Resources::Mirrors::D3D11::GetEffectiveResource(resource);
      effective_view = Resources::Mirrors::D3D11::GetEffectiveView(view);
   }
   else
   {
      Resources::Mirrors::D3D11::TrackView(resource, view);
   }
   Game::Registry::Call(GameEvents().on_init_unordered_access_view, device, effective_resource, desc, effective_view);
}

void PreCreateDepthStencilView(ID3D11Device*, ID3D11Resource*, std::optional<D3D11_DEPTH_STENCIL_VIEW_DESC>&) {}

void PostCreateDepthStencilView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc, ID3D11DepthStencilView* view)
{
   ID3D11Resource* effective_resource = resource;
   ID3D11DepthStencilView* effective_view = view;
   if (Resources::Mirrors::IsNeeded())
   {
      Resources::Mirrors::D3D11::CreateMirrorView(device, resource, desc, view);
      TrackMirrorViewDestruction(device, view);
      effective_resource = Resources::Mirrors::D3D11::GetEffectiveResource(resource);
      effective_view = Resources::Mirrors::D3D11::GetEffectiveView(view);
   }
   else
   {
      Resources::Mirrors::D3D11::TrackView(resource, view);
   }
   Game::Registry::Call(GameEvents().on_init_depth_stencil_view, device, effective_resource, desc, effective_view);
}

void OnViewDestroyed(ID3D11Device* device, const ID3D11View* view)
{
   Game::Registry::Call(GameEvents().on_view_destroyed, device, view);
}

void PreCreateSamplerState(ID3D11Device* device, D3D11_SAMPLER_DESC& desc)
{
   Game::Registry::Call(GameEvents().on_create_sampler_state, device, desc);
}

void PostCreateSamplerState(ID3D11Device* device, const D3D11_SAMPLER_DESC& desc, ID3D11SamplerState* sampler)
{
   if (Resources::Samplers::IsNeeded())
      Resources::Samplers::D3D11::TrackSampler(device, desc, sampler);
}

void OnSamplerDestroyed(ID3D11Device*, const ID3D11SamplerState*) {}

void PreCreateBlendState(ID3D11Device* device, D3D11_BLEND_DESC1& desc)
{
   Game::Registry::Call(GameCallbacks().pre_create_blend_state, device, desc);
}

void PostCreateBlendState(ID3D11Device*, const D3D11_BLEND_DESC1&, ID3D11BlendState*) {}

// Nothing changes the game's bytecode, which is what Core hashes: shaders are changed through variants
void PreCreateShader(ID3D11Device* device, ShaderStage stage, const void* bytecode, SIZE_T length, bool stream_output)
{
   if (Shaders::IsNeeded() && bytecode && !stream_output)
      Shaders::Variants::D3D11::CreateVariant(device, Shaders::Database::Track(Shaders::GetStore(device).get(), stage, {static_cast<const uint8_t*>(bytecode), length}));
}

void PostCreateShader(ID3D11Device* device, ShaderStage stage, const void* bytecode, SIZE_T length, bool stream_output, ID3D11DeviceChild* shader)
{
   const std::span<const uint8_t> code(static_cast<const uint8_t*>(bytecode), length);
   std::optional<uint32_t> hash;
   // The database first: the variant is found by the shader's hash
   if (Shaders::IsNeeded())
   {
      hash = Shaders::Database::D3D11::TrackShader(device, stage, code, shader);
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

bool PreSetShaderResources(ID3D11DeviceContext* context, ShaderStage stage, UINT start, UINT count, ID3D11ShaderResourceView** views)
{
   if (Resources::Mirrors::IsNeeded() && Resources::Mirrors::IsSwapping())
      Resources::Mirrors::D3D11::SwapViews(context, stage, start, views, count);
   return false;
}

bool PreSetConstantBuffers(ID3D11DeviceContext* context, ShaderStage stage, UINT start, UINT count, ID3D11Buffer** buffers, const UINT* first_constant,
   const UINT* num_constants)
{
   // Reported before Core records them
   Game::Registry::Call(GameEvents().on_bind_constant_buffers, context, stage, start, count, buffers);
   if (IsRestoringGameBuffers())
      Resources::LumaCBuffers::D3D11::RecordGameBuffers(context, stage, start, count, buffers, first_constant, num_constants);
   return false;
}

bool PreSetSamplers(ID3D11DeviceContext*, ShaderStage, UINT, UINT count, ID3D11SamplerState** samplers)
{
   if (Resources::Samplers::IsNeeded() && Resources::Samplers::IsSwapping())
      Resources::Samplers::D3D11::SwapSamplers(samplers, count);
   return false;
}

bool PreCSSetUnorderedAccessViews(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11UnorderedAccessView** views, const UINT*)
{
   if (Resources::Mirrors::IsNeeded() && Resources::Mirrors::IsSwapping())
      Resources::Mirrors::D3D11::SwapViews(context, ShaderStage::Compute, start, views, count);
   return false;
}

// Core swaps mirrors first so the game's event sees the effective views (matching draw-time OMGetRenderTargets)
bool PreOMSetRenderTargets(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView** views, ID3D11DepthStencilView*& depth_stencil)
{
   if (Resources::Mirrors::IsNeeded() && Resources::Mirrors::IsSwapping())
   {
      if (views)
         Resources::Mirrors::D3D11::SwapViews(views, count);
      Resources::Mirrors::D3D11::SwapView(depth_stencil);
   }
   return Game::Registry::Call(GameEvents().on_bind_render_targets, context, count, views, depth_stencil);
}

bool PreOMSetRenderTargetsAndUnorderedAccessViews(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView** views,
   ID3D11DepthStencilView*& depth_stencil, UINT uav_start, UINT uav_count, ID3D11UnorderedAccessView** uavs, const UINT* initial_counts)
{
   if (Resources::Mirrors::IsNeeded() && Resources::Mirrors::IsSwapping())
   {
      // D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL preserves both RTVs and the DSV.
      // By contrast, count == 0 with views == nullptr unbinds RTVs while still binding a supplied DSV (depth-only pass),
      // so depth_stencil must be swapped independently of whether `views` is null.
      if (count != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL)
      {
         if (views)
            Resources::Mirrors::D3D11::SwapViews(views, count);
         Resources::Mirrors::D3D11::SwapView(depth_stencil);
      }
      if (uavs && uav_count != D3D11_KEEP_UNORDERED_ACCESS_VIEWS)
         Resources::Mirrors::D3D11::SwapViews(context, ShaderStage::Pixel, uav_start, uavs, uav_count);
   }
   return Game::Registry::Call(GameEvents().on_bind_render_targets_and_unordered_access_views, context, count, views, depth_stencil, uav_start, uav_count, uavs,
      initial_counts);
}

void PostRSSetViewports(ID3D11DeviceContext* context, UINT count, const D3D11_VIEWPORT* viewports)
{
   if (Draw::GetRecorded().viewports)
      Draw::D3D11::RecordViewports(context, count, viewports);
   if (Resources::Mirrors::IsNeeded())
      Resources::Mirrors::D3D11::OnBindViewports(context, count, viewports);
   Game::Registry::Call(GameEvents().on_bind_viewports, context, count, viewports);
}

void PostRSSetScissorRects(ID3D11DeviceContext* context, UINT count, const D3D11_RECT* rects)
{
   if (Draw::GetRecorded().viewports)
      Draw::D3D11::RecordScissorRects(context, count, rects);
   if (Resources::Mirrors::IsNeeded())
      Resources::Mirrors::D3D11::OnBindScissorRects(context, count, rects);
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
#if DEVELOPMENT
   if (Trace::Capture::IsCapturing())
   {
      const auto get_map_type = [](D3D11_MAP map_type) -> std::string {
         switch (map_type)
         {
         case D3D11_MAP_READ: return "Read";
         case D3D11_MAP_WRITE: return "Write";
         case D3D11_MAP_READ_WRITE: return "ReadWrite";
         case D3D11_MAP_WRITE_DISCARD: return "WriteDiscard";
         case D3D11_MAP_WRITE_NO_OVERWRITE: return "WriteNoOverwrite";
         default: return std::to_string(uint32_t(map_type));
         }
      };
      const Trace::Capture::CpuAccess access = {
         .api_function = "Map",
         .map_type = get_map_type(type),
         .subresource = subresource,
         .row_pitch = mapped.RowPitch,
         .depth_pitch = mapped.DepthPitch,
      };
      Trace::Capture::D3D11::AddWrite(context, "Map", resource, access,
         type == D3D11_MAP_READ ? Trace::Capture::EventType::CpuRead : Trace::Capture::EventType::CpuWrite);
   }
#endif
   if (Resources::Mirrors::IsNeeded())
      Resources::Mirrors::D3D11::TrackMappedMirror(context, resource, subresource, type, mapped);
   Resources::ConstantCapture::D3D11::OnMap(context, resource, type, mapped.pData);
   Game::Registry::Call(GameEvents().on_map, context, resource, subresource, type, flags, mapped);
}

// Both the game's on_unmap and Core's constant capture / mapped mirror commit must finish while the Map() pointer (pData)
// is still valid, before native Unmap() invalidates it. on_unmap runs first so any edits it makes to mapped bytes are seen
// by Core; if it returns true (takes ownership of Unmap), Core discards its tracked entries without reading pData.
bool PreUnmap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource)
{
   if (Game::Registry::Call(GameEvents().on_unmap, context, resource, subresource))
   {
      Resources::ConstantCapture::D3D11::DiscardMapped(context, resource);
      if (Resources::Mirrors::IsNeeded())
         Resources::Mirrors::D3D11::DiscardMappedMirror(context, resource, subresource);
      return true;
   }
   Resources::ConstantCapture::D3D11::OnUnmap(context, resource);
   if (Resources::Mirrors::IsNeeded())
      Resources::Mirrors::D3D11::CommitMappedMirror(context, resource, subresource);
   return false;
}

void PostUnmap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource)
{
   Game::Registry::Call(GameEvents().on_unmapped, context, resource, subresource);
}

bool PreUpdateSubresource(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, const D3D11_BOX* box, const void* data,
   UINT row_pitch, UINT depth_pitch, UINT copy_flags)
{
   if (Game::Registry::Call(GameEvents().on_update_subresource, context, resource, subresource, box, data, row_pitch, depth_pitch, copy_flags))
      return true;
#if DEVELOPMENT
   if (Trace::Capture::IsCapturing())
   {
      Trace::Capture::CpuAccess access = {
         .api_function = "UpdateSubresource",
         .subresource = subresource,
         .row_pitch = row_pitch,
         .depth_pitch = depth_pitch,
      };
      if (box)
         access.destination_box = std::array<uint32_t, 6>{box->left, box->top, box->front, box->right, box->bottom, box->back};
      Trace::Capture::D3D11::AddWrite(context, "UpdateSubresource", resource, access, Trace::Capture::EventType::CpuWrite);
   }
#endif
   Resources::ConstantCapture::D3D11::OnUpdateSubresource(context, resource, data);
   // Into the mirror too, whether it's swapped in or not; the game's call still writes the original
   if (Resources::Mirrors::IsNeeded())
      Resources::Mirrors::D3D11::WriteToMirror(context, resource, subresource, box, data, row_pitch, depth_pitch);
   return false;
}

bool PreCopyResource(ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source)
{
   ID3D11Resource* effective_dst = destination;
   ID3D11Resource* effective_src = source;
   if (Resources::Mirrors::IsNeeded())
      Resources::Mirrors::D3D11::PrepareCopyTargets(context, effective_dst, effective_src);
   if (Game::Registry::Call(GameEvents().on_copy_resource, context, effective_dst, effective_src))
      return true;
#if DEVELOPMENT
   if (Trace::Capture::IsCapturing())
      Trace::Capture::D3D11::AddCopy(context, "CopyResource", destination, source, {});
   if (Trace::DebugDraw::IsActive())
      Trace::DebugDraw::CaptureD3D11(context, source);
#endif
   return Resources::Mirrors::IsNeeded() && Resources::Mirrors::D3D11::RedirectCopy(context, destination, source);
}

bool PreCopySubresourceRegion(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, UINT x, UINT y, UINT z,
   ID3D11Resource* source, UINT source_subresource, const D3D11_BOX* box, UINT copy_flags)
{
   ID3D11Resource* effective_dst = destination;
   ID3D11Resource* effective_src = source;
   if (Resources::Mirrors::IsNeeded())
      Resources::Mirrors::D3D11::PrepareCopyTargets(context, effective_dst, effective_src);
   if (Game::Registry::Call(GameEvents().on_copy_subresource_region, context, effective_dst, destination_subresource, x, y, z, effective_src,
          source_subresource, box, copy_flags))
      return true;
#if DEVELOPMENT
   if (Trace::Capture::IsCapturing())
   {
      D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
      destination->GetType(&dimension);
      Trace::Capture::Copy copy = {.kind = dimension == D3D11_RESOURCE_DIMENSION_BUFFER ? Trace::Capture::CopyKind::BufferRegion : Trace::Capture::CopyKind::Region,
         .source_subresource = source_subresource,
         .destination_subresource = destination_subresource,
         .destination_offset = {x, y, z}};
      if (box)
         copy.source_box = std::array<uint64_t, 6>{box->left, box->top, box->front, box->right, box->bottom, box->back};
      Trace::Capture::D3D11::AddCopy(context, "CopySubresourceRegion", destination, source, copy);
   }
   if (Trace::DebugDraw::IsActive())
      Trace::DebugDraw::CaptureD3D11(context, source);
#endif
   return Resources::Mirrors::IsNeeded() &&
      Resources::Mirrors::D3D11::RedirectCopyRegion(context, destination, destination_subresource, x, y, z, source, source_subresource, box, copy_flags);
}

bool PreResolveSubresource(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, ID3D11Resource* source,
   UINT source_subresource, DXGI_FORMAT format)
{
   ID3D11Resource* effective_dst = destination;
   ID3D11Resource* effective_src = source;
   if (Resources::Mirrors::IsNeeded())
      Resources::Mirrors::D3D11::PrepareCopyTargets(context, effective_dst, effective_src);
   if (Game::Registry::Call(GameEvents().on_resolve_subresource, context, effective_dst, destination_subresource, effective_src, source_subresource, format))
      return true;
#if DEVELOPMENT
   if (Trace::Capture::IsCapturing())
      Trace::Capture::D3D11::AddCopy(context, "ResolveSubresource", destination, source,
         {.kind = Trace::Capture::CopyKind::Resolve, .source_subresource = source_subresource, .destination_subresource = destination_subresource, .resolve_format = uint32_t(format)});
   if (Trace::DebugDraw::IsActive())
      Trace::DebugDraw::CaptureD3D11(context, source);
#endif
   return Resources::Mirrors::IsNeeded() && Resources::Mirrors::D3D11::RedirectResolve(context, destination, destination_subresource, source, source_subresource, format);
}

bool PreClearRenderTargetView(ID3D11DeviceContext* context, ID3D11RenderTargetView* view, const FLOAT color[4])
{
   ID3D11RenderTargetView* effective_view = Resources::Mirrors::IsNeeded() ? Resources::Mirrors::D3D11::GetEffectiveView(view) : view;
   if (Game::Registry::Call(GameEvents().on_clear_render_target_view, context, effective_view, color))
      return true;
#if DEVELOPMENT
   if (Trace::Capture::IsCapturing())
      Trace::Capture::D3D11::AddClear(context, "ClearRenderTargetView", view, {color[0], color[1], color[2], color[3]});
#endif
   return Resources::Mirrors::IsNeeded() && Resources::Mirrors::D3D11::RedirectClear(context, view, color);
}

bool PreCopyStructureCount(ID3D11DeviceContext* context, ID3D11Buffer* destination, UINT destination_offset, ID3D11UnorderedAccessView* source)
{
   return Game::Registry::Call(GameCallbacks().pre_copy_structure_count, context, destination, destination_offset, source);
}

bool PreClearDepthStencilView([[maybe_unused]] ID3D11DeviceContext* context, [[maybe_unused]] ID3D11DepthStencilView* view, [[maybe_unused]] UINT flags,
   [[maybe_unused]] FLOAT depth, [[maybe_unused]] UINT8 stencil)
{
#if DEVELOPMENT
   if (Trace::Capture::IsCapturing())
      Trace::Capture::D3D11::AddClear(context, "ClearDepthStencilView", view,
         {(flags & D3D11_CLEAR_DEPTH) ? depth : 0.f, (flags & D3D11_CLEAR_STENCIL) ? float(stencil) : 0.f, 0.f, 0.f});
#endif
   return Resources::Mirrors::IsNeeded() && Resources::Mirrors::D3D11::RedirectClear(context, view, flags, depth, stencil);
}

bool PreClearUnorderedAccessViewUint([[maybe_unused]] ID3D11DeviceContext* context, [[maybe_unused]] ID3D11UnorderedAccessView* view,
   [[maybe_unused]] const UINT values[4])
{
#if DEVELOPMENT
   if (Trace::Capture::IsCapturing())
      Trace::Capture::D3D11::AddClear(context, "ClearUnorderedAccessViewUint", view, {float(values[0]), float(values[1]), float(values[2]), float(values[3])});
#endif
   return Resources::Mirrors::IsNeeded() && Resources::Mirrors::D3D11::RedirectClear(context, view, values);
}

bool PreClearUnorderedAccessViewFloat([[maybe_unused]] ID3D11DeviceContext* context, [[maybe_unused]] ID3D11UnorderedAccessView* view,
   [[maybe_unused]] const FLOAT values[4])
{
#if DEVELOPMENT
   if (Trace::Capture::IsCapturing())
      Trace::Capture::D3D11::AddClear(context, "ClearUnorderedAccessViewFloat", view, {values[0], values[1], values[2], values[3]});
#endif
   return Resources::Mirrors::IsNeeded() && Resources::Mirrors::D3D11::RedirectClear(context, view, values);
}

bool PreClearView(ID3D11DeviceContext* context, ID3D11View* view, const FLOAT color[4], const D3D11_RECT* rects, UINT rect_count)
{
   ID3D11View* effective_view = Resources::Mirrors::IsNeeded() ? Resources::Mirrors::D3D11::GetEffectiveView(view) : view;
   if (Game::Registry::Call(GameEvents().on_clear_view, context, effective_view, color, rects, rect_count))
      return true;
#if DEVELOPMENT
   if (Trace::Capture::IsCapturing())
      Trace::Capture::D3D11::AddClear(context, "ClearView", view, {color[0], color[1], color[2], color[3]});
#endif
   return Resources::Mirrors::IsNeeded() && Resources::Mirrors::D3D11::RedirectClearView(context, view, color, rects, rect_count);
}

template <typename Call>
bool RunCall(ID3D11DeviceContext* context, const Call& call)
{
   constexpr bool dispatch = std::is_same_v<Call, DispatchCall>;
   Game::D3D11::Call original = {.context = context};
   if constexpr (dispatch)
      original.dispatch = &call;
   else
      original.draw = &call;
   const auto run_draw = [&](Shaders::StageSet stages, Game::DrawResult& result, bool& skipped) {
      if (Resources::Mirrors::IsNeeded())
         Resources::Mirrors::D3D11::PrepareDrawOrDispatch(context, dispatch);
      const Shaders::StageSet before = FulfilBeforeEvents(context, stages);
      const Game::DrawEvents::D3D11::Result events =
         Game::Registry::HasDrawCallbacks<Api::D3D11>() ? Game::DrawEvents::D3D11::Run(context, call) : Game::DrawEvents::D3D11::Result();
      result = events.result;
#if DEVELOPMENT
      skipped = result == Game::DrawResult::None && Trace::Capture::D3D11::IsSkipped(context, stages);
#endif
      const Shaders::StageSet bound = FulfilAfterEvents(context, stages, events, skipped, before);
      bool handled = RestoreGameBuffers(original, bound, result != Game::DrawResult::None || skipped);
#if DEVELOPMENT
      if (Trace::DebugDraw::IsActive())
         handled = Trace::DebugDraw::CaptureCallD3D11(original, stages, handled);
#endif
      return handled;
   };
#if DEVELOPMENT
   return HandleDrawOrDispatch<dispatch>(
      [&] {
         if constexpr (dispatch)
            return Trace::Capture::D3D11::BeginDispatch(context, call);
         else
            return Trace::Capture::D3D11::BeginDraw(context, call);
      },
      [&](Shaders::StageSet stages) { Trace::Capture::D3D11::TrackCall(context, stages); },
      [&](Shaders::StageSet stages, bool handled) { return Trace::Readback::D3D11::ReadAfterCall(original, stages, handled); },
      [](Trace::Capture::D3D11::Recording recording, Trace::Capture::Outcome outcome) { Trace::Capture::D3D11::End(std::move(recording), outcome); },
      run_draw);
#else
   Game::DrawResult result = Game::DrawResult::None;
   bool skipped = false;
   return run_draw(dispatch ? Shaders::COMPUTE_STAGES : Shaders::GRAPHICS_STAGES, result, skipped);
#endif
}

bool PreDraw(ID3D11DeviceContext* context, const DrawCall& draw)
{
   return RunCall(context, draw);
}

void PostDraw(ID3D11DeviceContext* context)
{
   if (Resources::Mirrors::IsNeeded())
      Resources::Mirrors::D3D11::FinishDrawOrDispatch(context, false);
}

bool PreDispatch(ID3D11DeviceContext* context, const DispatchCall& dispatch)
{
   return RunCall(context, dispatch);
}

void PostDispatch(ID3D11DeviceContext* context)
{
   if (Resources::Mirrors::IsNeeded())
      Resources::Mirrors::D3D11::FinishDrawOrDispatch(context, true);
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
   // Before Core's handling: the game may execute its own lists or passes first, or skip this one
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

void PresentD3D11(ID3D11Device* device)
{
   if (Shaders::IsNeeded())
      Shaders::Variants::D3D11::PublishVariants(device);
   if (Resources::Samplers::IsNeeded())
      Resources::Samplers::D3D11::RefreshMirrors(device);
   if (Resources::Mirrors::IsNeeded())
      Resources::Mirrors::D3D11::OnPresent(device);
   if (Effects::SR::IsNeeded())
      Effects::SR::D3D11::OnPresent(device);
   Resources::Database::D3D11::OnPresent(device);
   Resources::ConstantCapture::OnPresent();
   Frame::OnPresent(device);
   Resources::Database::D3D11::UpdateShaderObjects(device);
}
} // namespace Handlers
} // namespace Luma
