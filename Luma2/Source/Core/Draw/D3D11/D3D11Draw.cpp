#include "Draw/D3D11/D3D11Draw.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

#include "Draw/D3D11/D3D11Callbacks.hpp"
#include "Game/Registry.hpp"
#include "Hooks/D3D11/D3D11Stages.hpp"
#include "Hooks/Lifetime.hpp"
#include "Resources/D3D11/D3D11ResourceDatabase.hpp"
#include "Resources/ResourceDatabase.hpp"
#include "Shaders/D3D11/D3D11Variants.hpp"
#include "Utils/Debug.hpp"
#include "Utils/Hash.hpp"
#include "Utils/Log.hpp"
#if DEVELOPMENT
#include "Trace/Capture.hpp"
#include "Trace/D3D11/D3D11Capture.hpp"
#endif

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Draw {
namespace D3D11 {
namespace {
using Context = ID3D11DeviceContext;
using Context1 = ID3D11DeviceContext1;

// The per stage functions, in ShaderStage order
struct StageFunctions
{
   void (STDMETHODCALLTYPE Context::*get_resources)(UINT, UINT, ID3D11ShaderResourceView**);
   void (STDMETHODCALLTYPE Context::*set_resources)(UINT, UINT, ID3D11ShaderResourceView* const*);
   void (STDMETHODCALLTYPE Context::*get_buffers)(UINT, UINT, ID3D11Buffer**);
   void (STDMETHODCALLTYPE Context::*set_buffers)(UINT, UINT, ID3D11Buffer* const*);
   void (STDMETHODCALLTYPE Context1::*get_buffers1)(UINT, UINT, ID3D11Buffer**, UINT*, UINT*);
   void (STDMETHODCALLTYPE Context1::*set_buffers1)(UINT, UINT, ID3D11Buffer* const*, const UINT*, const UINT*);
   void (STDMETHODCALLTYPE Context::*get_samplers)(UINT, UINT, ID3D11SamplerState**);
   void (STDMETHODCALLTYPE Context::*set_samplers)(UINT, UINT, ID3D11SamplerState* const*);
};

template <ShaderStage Stage>
constexpr StageFunctions GetStageFunctions()
{
   using Traits = Hooks::D3D11::StageTraits<Stage>;
   return {Traits::GetShaderResources, Traits::SetShaderResources, Traits::GetConstantBuffers, Traits::SetConstantBuffers, Traits::GetConstantBuffers1,
      Traits::SetConstantBuffers1, Traits::GetSamplers, Traits::SetSamplers};
}

constexpr std::array<StageFunctions, 6> STAGES = {GetStageFunctions<ShaderStage::Vertex>(), GetStageFunctions<ShaderStage::Hull>(),
   GetStageFunctions<ShaderStage::Domain>(), GetStageFunctions<ShaderStage::Geometry>(), GetStageFunctions<ShaderStage::Pixel>(),
   GetStageFunctions<ShaderStage::Compute>()};

ComPtr<ID3D11DeviceChild> GetShader(Context* context, ShaderStage stage)
{
   ComPtr<ID3D11DeviceChild> shader;
   switch (stage)
   {
   case ShaderStage::Vertex: context->VSGetShader(reinterpret_cast<ID3D11VertexShader**>(shader.GetAddressOf()), nullptr, nullptr); break;
   case ShaderStage::Hull: context->HSGetShader(reinterpret_cast<ID3D11HullShader**>(shader.GetAddressOf()), nullptr, nullptr); break;
   case ShaderStage::Domain: context->DSGetShader(reinterpret_cast<ID3D11DomainShader**>(shader.GetAddressOf()), nullptr, nullptr); break;
   case ShaderStage::Geometry: context->GSGetShader(reinterpret_cast<ID3D11GeometryShader**>(shader.GetAddressOf()), nullptr, nullptr); break;
   case ShaderStage::Pixel: context->PSGetShader(reinterpret_cast<ID3D11PixelShader**>(shader.GetAddressOf()), nullptr, nullptr); break;
   case ShaderStage::Compute: context->CSGetShader(reinterpret_cast<ID3D11ComputeShader**>(shader.GetAddressOf()), nullptr, nullptr); break;
   }
   return shader;
}

Range Clamp(Range range, uint32_t slots)
{
   range.first = std::min(range.first, slots);
   range.count = std::min(range.count, slots - range.first);
   return range;
}

// ComPtr arrays as the raw pointers the API takes (a ComPtr is one pointer)
template <typename T>
T** AsRaw(std::vector<ComPtr<T>>& items)
{
   static_assert(sizeof(ComPtr<T>) == sizeof(T*));
   return reinterpret_cast<T**>(items.data());
}

template <typename T>
T* const* AsRaw(const std::vector<ComPtr<T>>& items)
{
   return reinterpret_cast<T* const*>(items.data());
}

template <typename T, size_t N>
T* const* AsRaw(const std::array<ComPtr<T>, N>& items)
{
   return reinterpret_cast<T* const*>(items.data());
}

UINT GetUnorderedAccessSlots(Context* context)
{
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   return device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_1 ? D3D11_1_UAV_SLOT_COUNT : D3D11_PS_CS_UAV_REGISTER_COUNT;
}

Bound* GetRecord(Context* context)
{
   return Hooks::Lifetime::GetOrCreateStateCached<Bound>(context);
}

// The callback of the first stage of `stages` with one, in 1.0's order (pixel, then vertex)
Game::D3D11::DrawCallback FindCallback(const Shaders::Variants::D3D11::BoundShaders& bound, uint8_t stages)
{
   constexpr std::array ORDER = {ShaderStage::Pixel, ShaderStage::Vertex, ShaderStage::Geometry, ShaderStage::Hull, ShaderStage::Domain, ShaderStage::Compute};
   for (const ShaderStage stage : ORDER)
   {
      if (stages & GetStageBit(stage))
         return Game::Registry::FindDrawCallback(bound.shaders[size_t(stage)]->hash);
   }
   return nullptr;
}

// The callback running on this thread, which BindLumaShader reports the stages it bound to
struct Running
{
   Context* context = nullptr;
   uint8_t luma_stages = 0;
};

thread_local Running* running = nullptr;

// The variant callback first (1.0's OnDrawOrDispatchCustomPipeline), then the one by hash or the generic one
Game::DrawResult RunCallbacks(Context* context, const Game::D3D11::DrawCall* draw, const Game::D3D11::DispatchCall* dispatch, uint8_t stages)
{
   const Shaders::Variants::D3D11::BoundShaders* bound = Shaders::Variants::D3D11::GetBoundShaders(context);
   const uint8_t variant_stages = bound ? bound->variant_stages & stages : 0;
   const uint8_t callback_stages = bound ? bound->callback_stages & stages : 0;
   const Game::D3D11::DrawCallback variant_callback = variant_stages ? Game::Registry::GetVariantDrawCallback() : nullptr;
   const Game::D3D11::DrawCallback callback = callback_stages ? FindCallback(*bound, callback_stages) : Game::Registry::GetDrawOrDispatchCallback();
   if (!variant_callback && !callback)
      return Game::DrawResult::None;
   Game::D3D11::Call call = {.context = context, .draw = draw, .dispatch = dispatch, .variant_stages = variant_stages};
   for (size_t stage = 0; bound && stage < bound->shaders.size(); stage++)
      call.hashes[stage] = bound->shaders[stage] ? bound->shaders[stage]->hash : 0;
   if (variant_callback)
   {
      if (const Game::DrawResult result = variant_callback(call); result != Game::DrawResult::None)
         return result;
      // It may have put the game's shaders back (UseOriginal)
      call.variant_stages = bound->variant_stages & stages;
   }
   return callback ? callback(call) : Game::DrawResult::None;
}

CallbackResult RunCallbacksReporting(Context* context, const Game::D3D11::DrawCall* draw, const Game::D3D11::DispatchCall* dispatch, uint8_t stages)
{
   if (!IsEnabled())
      return {};
   Running current = {.context = context};
   Running* const previous = std::exchange(running, &current);
   const Game::DrawResult result = RunCallbacks(context, draw, dispatch, stages);
   running = previous;
   return {result, current.luma_stages};
}

// The width and height of the view's mip, 0 if it isn't a 2D texture's
std::pair<UINT, UINT> GetTargetSize(ID3D11RenderTargetView* target)
{
   ComPtr<ID3D11Resource> resource;
   target->GetResource(&resource);
   ComPtr<ID3D11Texture2D> texture;
   if (!resource || FAILED(resource.As(&texture)))
      return {0, 0};
   D3D11_TEXTURE2D_DESC desc;
   texture->GetDesc(&desc);
   D3D11_RENDER_TARGET_VIEW_DESC view_desc;
   target->GetDesc(&view_desc);
   const UINT mip = view_desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D ? view_desc.Texture2D.MipSlice : 0;
   return {std::max(desc.Width >> mip, 1u), std::max(desc.Height >> mip, 1u)};
}
} // namespace

void Save(Context* context, StateStack& stack)
{
   const StateMask& mask = stack.mask;
   ComPtr<Context1> context1;
   context->QueryInterface(IID_PPV_ARGS(&context1));
   for (size_t index = 0; index < STAGES.size(); index++)
   {
      const ShaderStage stage = ShaderStage(index);
      const uint8_t bit = GetStageBit(stage);
      const StageFunctions& functions = STAGES[index];
      StageState& state = stack.stages[index];
      if (mask.shaders & bit)
         state.shader = GetShader(context, stage);
      if (mask.shader_resources & bit)
      {
         const Range range = Clamp(mask.shader_resource_slots, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
         state.shader_resources.resize(range.count);
         (context->*functions.get_resources)(range.first, range.count, AsRaw(state.shader_resources));
      }
      if (mask.constant_buffers & bit)
      {
         const Range range = Clamp(mask.constant_buffer_slots, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);
         state.constant_buffers.resize(range.count);
         if (context1)
         {
            state.first_constants.resize(range.count);
            state.constant_counts.resize(range.count);
            (context1.Get()->*functions.get_buffers1)(range.first, range.count, AsRaw(state.constant_buffers), state.first_constants.data(),
               state.constant_counts.data());
         }
         else
            (context->*functions.get_buffers)(range.first, range.count, AsRaw(state.constant_buffers));
      }
      if (mask.samplers & bit)
      {
         const Range range = Clamp(mask.sampler_slots, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT);
         state.samplers.resize(range.count);
         (context->*functions.get_samplers)(range.first, range.count, AsRaw(state.samplers));
      }
   }
   if (mask.unordered_access & COMPUTE_STAGES)
   {
      StageState& state = stack.stages[size_t(ShaderStage::Compute)];
      state.unordered_access.resize(GetUnorderedAccessSlots(context));
      context->CSGetUnorderedAccessViews(0, UINT(state.unordered_access.size()), AsRaw(state.unordered_access));
   }
   if (mask.targets)
   {
      context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, reinterpret_cast<ID3D11RenderTargetView**>(stack.render_targets.data()),
         &stack.depth_stencil);
      stack.render_target_count = 0;
      for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
      {
         if (stack.render_targets[i])
            stack.render_target_count = i + 1;
      }
      // 1.0's FullGraphics: the output merger's views start after the last render target
      if (mask.unordered_access & PIXEL)
      {
         StageState& state = stack.stages[size_t(ShaderStage::Pixel)];
         state.unordered_access.resize(GetUnorderedAccessSlots(context) - stack.render_target_count);
         context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, stack.render_target_count, UINT(state.unordered_access.size()),
            AsRaw(state.unordered_access));
      }
   }
   if (mask.blend)
      context->OMGetBlendState(&stack.blend, stack.blend_factor.data(), &stack.sample_mask);
   if (mask.depth)
      context->OMGetDepthStencilState(&stack.depth, &stack.stencil_ref);
   if (mask.rasterizer)
      context->RSGetState(&stack.rasterizer);
   if (mask.viewports)
   {
      stack.viewport_count = UINT(stack.viewports.size());
      context->RSGetViewports(&stack.viewport_count, stack.viewports.data());
      stack.scissor_count = UINT(stack.scissor_rects.size());
      context->RSGetScissorRects(&stack.scissor_count, stack.scissor_rects.data());
   }
   if (mask.input_assembler)
   {
      context->IAGetPrimitiveTopology(&stack.topology);
      context->IAGetInputLayout(&stack.input_layout);
   }
}

void Restore(Context* context, const StateStack& stack)
{
   const StateMask& mask = stack.mask;
   // The targets first: as outputs they'd unbind the same resources from the inputs restored after them otherwise
   if (mask.targets)
   {
      const auto* targets = AsRaw(stack.render_targets);
      if (mask.unordered_access & PIXEL)
      {
         const std::vector<ComPtr<ID3D11UnorderedAccessView>>& views = stack.stages[size_t(ShaderStage::Pixel)].unordered_access;
         // Keeps the counters as they are
         const std::vector<UINT> counts(views.size(), UINT_MAX);
         context->OMSetRenderTargetsAndUnorderedAccessViews(stack.render_target_count, targets, stack.depth_stencil.Get(), stack.render_target_count,
            UINT(views.size()), AsRaw(views), counts.data());
      }
      else
         context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, stack.depth_stencil.Get());
   }
   if (mask.blend)
      context->OMSetBlendState(stack.blend.Get(), stack.blend_factor.data(), stack.sample_mask);
   if (mask.input_assembler)
   {
      context->IASetPrimitiveTopology(stack.topology);
      context->IASetInputLayout(stack.input_layout.Get());
   }
   if (mask.viewports)
   {
      context->RSSetScissorRects(stack.scissor_count, stack.scissor_rects.data());
      context->RSSetViewports(stack.viewport_count, stack.viewports.data());
   }
   if (mask.depth)
      context->OMSetDepthStencilState(stack.depth.Get(), stack.stencil_ref);
   if (mask.rasterizer)
      context->RSSetState(stack.rasterizer.Get());
   ComPtr<Context1> context1;
   if (mask.constant_buffers)
      context->QueryInterface(IID_PPV_ARGS(&context1));
   for (size_t index = 0; index < STAGES.size(); index++)
   {
      const ShaderStage stage = ShaderStage(index);
      const uint8_t bit = GetStageBit(stage);
      const StageFunctions& functions = STAGES[index];
      const StageState& state = stack.stages[index];
      if (mask.shader_resources & bit)
      {
         const Range range = Clamp(mask.shader_resource_slots, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
         (context->*functions.set_resources)(range.first, range.count, AsRaw(state.shader_resources));
      }
      if (mask.constant_buffers & bit)
      {
         const Range range = Clamp(mask.constant_buffer_slots, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);
         if (context1 && !state.first_constants.empty())
            (context1.Get()->*functions.set_buffers1)(range.first, range.count, AsRaw(state.constant_buffers), state.first_constants.data(),
               state.constant_counts.data());
         else
            (context->*functions.set_buffers)(range.first, range.count, AsRaw(state.constant_buffers));
      }
      if (mask.samplers & bit)
      {
         const Range range = Clamp(mask.sampler_slots, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT);
         (context->*functions.set_samplers)(range.first, range.count, AsRaw(state.samplers));
      }
      if (mask.shaders & bit)
         Shaders::D3D11::Bind(context, stage, state.shader.Get());
   }
   if (mask.unordered_access & COMPUTE_STAGES)
   {
      const std::vector<ComPtr<ID3D11UnorderedAccessView>>& views = stack.stages[size_t(ShaderStage::Compute)].unordered_access;
      const std::vector<UINT> counts(views.size(), UINT_MAX);
      context->CSSetUnorderedAccessViews(0, UINT(views.size()), AsRaw(views), counts.data());
   }
}

const Bound* GetBound(Context* context)
{
   return GetRecord(context);
}

void RecordViewports(Context* context, UINT count, const D3D11_VIEWPORT* viewports)
{
   if (Bound* bound = GetRecord(context))
   {
      bound->viewport_count = std::min(count, UINT(bound->viewports.size()));
      std::copy_n(viewports, bound->viewport_count, bound->viewports.begin());
   }
}

void RecordScissorRects(Context* context, UINT count, const D3D11_RECT* rects)
{
   if (Bound* bound = GetRecord(context))
   {
      bound->scissor_count = std::min(count, UINT(bound->scissor_rects.size()));
      std::copy_n(rects, bound->scissor_count, bound->scissor_rects.begin());
   }
}

void RecordBlendState(Context* context, ID3D11BlendState* state, const FLOAT blend_factor[4], UINT sample_mask)
{
   if (Bound* bound = GetRecord(context))
   {
      bound->blend = state;
      // Null is the API's default, all ones
      bound->blend_factor = blend_factor ? std::array{blend_factor[0], blend_factor[1], blend_factor[2], blend_factor[3]} : std::array{1.f, 1.f, 1.f, 1.f};
      bound->sample_mask = sample_mask;
   }
}

void RecordDepthStencilState(Context* context, ID3D11DepthStencilState* state, UINT stencil_ref)
{
   if (Bound* bound = GetRecord(context))
   {
      bound->depth = state;
      bound->stencil_ref = stencil_ref;
   }
}

void ForgetBoundState(Context* context)
{
   if (Bound* bound = GetRecord(context))
      *bound = {};
}

bool DrawFullscreen(Context* context, const FullscreenPass& pass)
{
   if (!IsEnabled() || !pass.target)
      return false;
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   const Shaders::D3D11::Live* vertex_shader = Resources::Database::D3D11::GetShader(device.Get(), pass.vertex_shader);
   const Shaders::D3D11::Live* pixel_shader = Resources::Database::D3D11::GetShader(device.Get(), pass.pixel_shader);
   const auto [width, height] = GetTargetSize(pass.target);
   if (!vertex_shader || !pixel_shader || !width)
      return false;

   StateStack stack;
   Save(context, stack);
   // 1.0 sets the blend factor's alpha to 0 without alpha, though the blend state rarely reads it
   constexpr std::array<FLOAT, 4> FACTOR = {1.f, 1.f, 1.f, 1.f};
   constexpr std::array<FLOAT, 4> FACTOR_NO_ALPHA = {1.f, 1.f, 1.f, 0.f};
   context->OMSetBlendState(pass.blend, pass.alpha ? FACTOR.data() : FACTOR_NO_ALPHA.data(), UINT_MAX);
   context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
   context->IASetInputLayout(nullptr);
   context->RSSetScissorRects(0, nullptr);
   const D3D11_VIEWPORT viewport = {0.f, 0.f, float(width), float(height), 0.f, 1.f};
   context->RSSetViewports(1, &viewport);
   context->RSSetState(nullptr);
   context->PSSetShaderResources(0, 1, &pass.source);
   if (pass.sampler)
      context->PSSetSamplers(0, 1, &pass.sampler);
   context->OMSetDepthStencilState(pass.depth, 0);
   context->OMSetRenderTargets(1, &pass.target, nullptr);
   Shaders::D3D11::Bind(context, ShaderStage::Vertex, vertex_shader->object.Get());
   Shaders::D3D11::Bind(context, ShaderStage::Pixel, pixel_shader->object.Get());
   // What Luma's shaders read (its cbuffers), where they declared them
   Resources::Database::D3D11::BindRequested(context, ShaderStage::Vertex, *vertex_shader->compiled, pass.custom);
   Resources::Database::D3D11::BindRequested(context, ShaderStage::Pixel, *pixel_shader->compiled, pass.custom);
#if DEVELOPMENT
   std::optional<Trace::Capture::D3D11::Recording> recording;
   if (Trace::Capture::IsCapturing())
      recording = Trace::Capture::D3D11::BeginPass(context, pass.name, ShaderStage::Pixel, pass.pixel_shader);
#endif
   context->Draw(4, 0);
#if DEVELOPMENT
   if (recording)
      Trace::Capture::D3D11::End(std::move(*recording));
#endif
   Restore(context, stack);
   return true;
}

bool Copy(Context* context, ID3D11ShaderResourceView* source, ID3D11RenderTargetView* target)
{
   return DrawFullscreen(context, {.name = "Luma Copy", .vertex_shader = "Luma_Copy_VS"_h, .pixel_shader = "Luma_Copy_PS"_h, .source = source, .target = target});
}

bool Scale(Context* context, ID3D11ShaderResourceView* source, ID3D11RenderTargetView* target)
{
   // The API returns the same object for the same desc: no state to keep
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   const D3D11_SAMPLER_DESC desc = {D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, 0.f, 1,
      D3D11_COMPARISON_NEVER, {}, 0.f, D3D11_FLOAT32_MAX};
   ComPtr<ID3D11SamplerState> linear_sampler;
   if (FAILED(device->CreateSamplerState(&desc, &linear_sampler)))
      return false;
   return DrawFullscreen(context, {.name = "Luma Scale", .vertex_shader = "Luma_Scale_VS"_h, .pixel_shader = "Luma_Scale_PS"_h, .source = source, .target = target, .sampler = linear_sampler.Get()});
}

CallbackResult RunDrawCallbacks(Context* context, const Game::D3D11::DrawCall& draw)
{
   return RunCallbacksReporting(context, &draw, nullptr, GRAPHICS_STAGES);
}

CallbackResult RunDispatchCallbacks(Context* context, const Game::D3D11::DispatchCall& dispatch)
{
   return RunCallbacksReporting(context, nullptr, &dispatch, COMPUTE_STAGES);
}

#if DEVELOPMENT
bool IsSkippedByTrace(Context* context, uint8_t stages)
{
   const Shaders::Variants::D3D11::BoundShaders* bound = Shaders::Variants::D3D11::GetBoundShaders(context);
   if (!bound)
      return false;
   for (size_t stage = 0; stage < bound->shaders.size(); stage++)
   {
      const Shaders::Database::Shader* shader = bound->shaders[stage];
      if ((stages & (1u << stage)) && shader && shader->skip_draws.load(std::memory_order_relaxed))
         return true;
   }
   return false;
}
#endif
} // namespace D3D11
} // namespace Draw

void Game::D3D11::BindLumaShader(ID3D11DeviceContext* context, uint64_t name_hash, const LumaCustomData& custom)
{
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   const Luma::Shaders::D3D11::Live* shader = Resources::Database::D3D11::GetShader(device.Get(), name_hash);
   const std::optional<uint32_t> entry = Resources::Database::Find(name_hash);
   if (!shader || !entry)
      return;
   const ShaderStage stage = Resources::Database::Get(*entry).stage;
   Luma::Shaders::D3D11::Bind(context, stage, shader->object.Get());
   // From a draw callback, the handler restores the game's buffers at Luma's slots after the call and binds no other requests
   if (Resources::Database::D3D11::BindRequested(context, stage, *shader->compiled, custom) && Draw::D3D11::running && Draw::D3D11::running->context == context)
      Draw::D3D11::running->luma_stages |= Draw::GetStageBit(stage);
}

void Game::D3D11::UseOriginal(const Call& call, ShaderStage stage)
{
   Luma::Shaders::Variants::D3D11::UseOriginal(call.context, stage);
}

void Game::D3D11::RunOriginal(const Call& call)
{
   using Function = Hooks::D3D11::Function;
   ID3D11DeviceContext* context = call.context;
   if (const DispatchCall* dispatch = call.dispatch)
   {
      if (dispatch->arguments)
         context->DispatchIndirect(dispatch->arguments, dispatch->arguments_offset);
      else
         context->Dispatch(dispatch->x, dispatch->y, dispatch->z);
      return;
   }
   const DrawCall& draw = *call.draw;
   switch (draw.function)
   {
   case Function::Draw: context->Draw(draw.vertex_or_index_count, draw.start_vertex_or_index); break;
   case Function::DrawInstanced: context->DrawInstanced(draw.vertex_or_index_count, draw.instance_count, draw.start_vertex_or_index, draw.start_instance); break;
   case Function::DrawIndexed: context->DrawIndexed(draw.vertex_or_index_count, draw.start_vertex_or_index, draw.base_vertex); break;
   case Function::DrawIndexedInstanced:
      context->DrawIndexedInstanced(draw.vertex_or_index_count, draw.instance_count, draw.start_vertex_or_index, draw.base_vertex, draw.start_instance);
      break;
   case Function::DrawAuto: context->DrawAuto(); break;
   case Function::DrawInstancedIndirect: context->DrawInstancedIndirect(draw.arguments, draw.arguments_offset); break;
   case Function::DrawIndexedInstancedIndirect: context->DrawIndexedInstancedIndirect(draw.arguments, draw.arguments_offset); break;
   default: ASSERT_ONCE_MSG(false, "Not a draw function"); break;
   }
}
} // namespace Luma
