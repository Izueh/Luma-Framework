#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <d3d11.h>
#include <include/reshade.hpp>

struct CommandListData;   // instance_data.h
struct DeviceData;        // instance_data.h
struct DrawDispatchData;  // instance_data.h
struct TraceDrawCallData; // instance_data.h

// Frame capture entries (DEVELOPMENT only, the calls compile to nothing otherwise), implemented in "trace.inl".
// The capture scheduling and the dev UI trace view are in "core.hpp".
namespace Trace
{
#if DEVELOPMENT
   // Records the state a Luma pass left bound (views, cbuffers, samplers, viewports, blend, depth/stencil and rasterizer state), see "CustomDraw"
   void RecordCustomPass(ID3D11DeviceContext* native_device_context, DeviceData& device_data, const char* name, uint32_t shader_hash, reshade::api::shader_stage stage);
#endif

   // Runs a Luma pass ("draw") and records it like a game draw, with the state bound after it: restore the previous state outside of "draw".
   // "name" is kept by the capture (a string literal). "shader_hash" is the "native_shaders_definitions" key of its pixel (or compute) shader.
   template <typename TDraw>
   void CustomDraw(ID3D11DeviceContext* native_device_context, DeviceData& device_data, const char* name, TDraw&& draw, uint32_t shader_hash = 0)
   {
      draw();
#if DEVELOPMENT
      RecordCustomPass(native_device_context, device_data, name, shader_hash, reshade::api::shader_stage::pixel);
#endif
   }
   template <typename TDispatch>
   void CustomDispatch(ID3D11DeviceContext* native_device_context, DeviceData& device_data, const char* name, TDispatch&& dispatch, uint32_t shader_hash = 0)
   {
      dispatch();
#if DEVELOPMENT
      RecordCustomPass(native_device_context, device_data, name, shader_hash, reshade::api::shader_stage::compute);
#endif
   }

#if DEVELOPMENT
   // An entry with a name (and a target view or resource) only: a phase marker, or a pass without state of its own (e.g. SR).
   // "before_current_draw" places it before the entries of the game draw being processed.
   void Marker(ID3D11DeviceContext* native_device_context, const char* name, ID3D11DeviceChild* target = nullptr, bool before_current_draw = false);
   // Attaches a note to the draw being processed in the current capture (e.g. the game's motion vector decision).
   // Call it from "Game::OnDrawOrDispatch", where the shader entries of the current draw are the last ones of "cmd_list_data".
   // "value" is appended as "text=value" (formatted only while capturing, so it can be called on every draw).
   void Annotate(CommandListData& cmd_list_data, std::string_view text, std::optional<int64_t> value = std::nullopt);

   // Core: a game draw's entry for one of its shaders. Expects "s_mutex_generic", "s_mutex_dumping" and "device_data.mutex" (shared) locked.
   void AddDrawCall(std::vector<TraceDrawCallData>& trace_draw_calls_data, const DeviceData& device_data, ID3D11DeviceContext* native_device_context, uint64_t pipeline_handle, const DrawDispatchData& draw_dispatch_data);
   // Core: moves the entries queued on this thread for "native_device_context" into its trace (the other contexts' are dropped),
   // the "before_current_draw" ones at "current_draw_index". Expects the trace mutexes locked.
   void MoveQueued(std::vector<TraceDrawCallData>& trace_draw_calls_data, ID3D11DeviceContext* native_device_context, size_t current_draw_index = SIZE_MAX);
   void ClearQueued();
#else
   inline void Marker(ID3D11DeviceContext*, const char*, ID3D11DeviceChild* = nullptr, bool = false) {}
   inline void Annotate(CommandListData&, std::string_view, std::optional<int64_t> = std::nullopt) {}
#endif
} // namespace Trace
