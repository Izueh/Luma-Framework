#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string_view>
#include <thread>
#include <vector>

#include <d3d11.h>
#include <d3d11shader.h>
#include <include/reshade.hpp>

#include "com_ptr.h"

struct CommandListData;   // instance_data.h
struct DeviceData;        // instance_data.h
struct DrawDispatchData;  // instance_data.h
struct TraceDrawCallData; // instance_data.h
namespace Shader
{
   struct CachedShader; // shaders.h
} // namespace Shader

// Frame capture entries (DEVELOPMENT only, the calls compile to nothing otherwise), implemented in "trace.inl".
// The capture scheduling and the dev UI trace view are in "core.hpp".
namespace Trace
{
#if DEVELOPMENT
   // Needs to match in shader
   enum class DebugDrawTextureOptionsMask : uint32_t
   {
      None = 0,
      Fullscreen = 1 << 0,
      RenderResolutionScale = 1 << 1,
      ShowAlpha = 1 << 2,
      PreMultiplyAlpha = 1 << 3,
      InvertColors = 1 << 4,
      LinearToGamma = 1 << 5,
      GammaToLinear = 1 << 6,
      FlipY = 1 << 7,
      Saturate = 1 << 8,
      RedOnly = 1 << 9,
      BackgroundPassthrough = 1 << 10,
      TextureMultiSample = 1 << 11,
      TextureArray = 1 << 12,
      // If this is true and Texture3D is also true, the texture is a cube. If both are false it's 1D.
      Texture2D = 1 << 13,
      Texture3D = 1 << 14,
      Abs = 1 << 15,
      Zoom4x = 1 << 16,
      Bilinear = 1 << 17,
      Tonemap = 1 << 18,
      SRGB = 1 << 19,
      UVToPixelSpace = 1 << 20,
      Denormalize = 1 << 21,
   };
   enum class DebugDrawMode : uint32_t
   {
      Custom,
      // TODO: rename all of these to "View", or not
      RenderTarget,
      UnorderedAccessView,
      ShaderResource,
      Depth,
      Stencil,
   };
   inline constexpr const char* debug_draw_mode_strings[] = {
       "Custom",
       "Render Target",
       "Unordered Access View",
       "Shader Resource",
       "Depth",
       "Stencil",
   };

   // A capture is scheduled for the next present and runs until the one after. "running" is atomic so hot paths can check it without "s_mutex"
   // (and recheck it under the lock).
   inline std::shared_mutex s_mutex; // For "running", "scheduled" and "captured_count"
   inline std::atomic<bool> running = false; // For this frame
   inline bool scheduled = false; // For next frame
   inline uint32_t captured_count = 0; // Not exactly necessary but... it might help

   // The dev UI capture list filters
   inline bool show_command_list_info = false;
   inline bool ignore_vertex_shaders = true;
   inline bool ignore_buffer_writes = true;
   inline bool ignore_bindings = true;
   inline bool ignore_custom_draws = true; // Luma passes ("Trace::Draw()")
   inline bool ignore_non_bound_shader_referenced_resources = true;

   // Textures debug drawing
   inline uint32_t debug_draw_shader_hash = 0;
   inline char debug_draw_shader_hash_string[8 + 1] = {}; // A shader hash and its terminator
   inline uint64_t debug_draw_pipeline = 0; // In DX11 shaders and pipelines are the same thing so this is kinda useless (excluding the rare case where multiple pipelines (compiled shader objects) were based on the same shader, and hence had the same hash)
   inline int32_t debug_draw_pipeline_target_instance = -1;
   inline std::thread::id debug_draw_pipeline_target_thread = std::thread::id(); // Filter target instances by thread, which would hopefully reliably match the command lists, over multiple frames
   inline uint32_t debug_draw_custom_pass = 0; // A Luma pass instead of a game pipeline, by the hash of its "Trace::Draw()" name

   inline std::atomic<int32_t> debug_draw_pipeline_instance = 0; // Theoretically should be within "CommandListData" but this should work for most cases
   inline std::atomic<int32_t> debug_draw_custom_pass_instance = 0;

   inline DebugDrawMode debug_draw_mode = DebugDrawMode::Custom;
   inline bool debug_draw_freeze_inputs = false; // Allows freezing the inputs/state of this pass, allowing you to edit the shader and seeing the results live. This is bundled with the textures debug draw feature because generally you don't need to freeze the inputs of any prior pass if not the very same one you are analyzing (or iterating upon). If the game stopped drawing the shader, debugging would also stop.
   inline int32_t debug_draw_view_index = 0;
   inline bool debug_draw_disable_blends = false; // Allows disabling HW blends so we can debug the raw pixel shader output
   inline uint32_t debug_draw_options = (uint32_t)DebugDrawTextureOptionsMask::Fullscreen | (uint32_t)DebugDrawTextureOptionsMask::BackgroundPassthrough | (uint32_t)DebugDrawTextureOptionsMask::Tonemap;
   inline int32_t debug_draw_mip = 0;
   inline bool debug_draw_pipeline_instance_filter_by_thread = false; // Not true by default as it seems like quite a few games set up multiple render threads, so this conflicts between frames
   inline bool debug_draw_auto_clear_texture = false;
   inline bool debug_draw_replaced_pass = false; // Whether we print the debugging of the original or replaced pass (the resources bindings etc might be different, though this won't forcefully run the original pass if it was skipped by the game's mod custom code, so this isn't fully reliable)
   inline bool debug_draw_auto_gamma = true;

   // Constant Buffers tracking
   inline uint64_t track_buffer_pipeline = 0; // Can be any type of shader
   inline int32_t track_buffer_pipeline_target_instance = -1;
   inline int32_t track_buffer_index = 0;

   inline std::atomic<int32_t> track_buffer_pipeline_instance = 0; // Theoretically should be within "CommandListData" but this should work for most cases

   // Core: at the ReShade present, ends the running capture or starts the scheduled one (in the immediate command list)
   void OnPresent(CommandListData& cmd_list_data);
   // Copies the view of the pass that just drew (still bound) selected by "debug_draw_mode" and "debug_draw_view_index" into "DeviceData::debug_draw_texture"
   bool CopyDebugDrawTexture(DebugDrawMode debug_draw_mode, int32_t debug_draw_view_index, ID3D11DeviceContext* native_device_context, DeviceData& device_data, bool is_dispatch);
   bool CopyDebugDrawTexture(DebugDrawMode debug_draw_mode, int32_t debug_draw_view_index, reshade::api::command_list* cmd_list, bool is_dispatch = false);
   // Reads a constant buffer back (immediate contexts only) or copies it to read it later ("CopyBuffer()"), for the constant buffer tracking
   bool MapBufferData(com_ptr<ID3D11Buffer>& cb, ID3D11DeviceContext* native_device_context, std::vector<float>& buffer_data, UINT byte_width);
   bool CopyBuffer(com_ptr<ID3D11Buffer> cb, ID3D11DeviceContext* native_device_context, std::vector<float>& buffer_data, com_ptr<ID3D11Buffer>& cb_copy);

   // Records a draw (or dispatch) the mod issued itself (a Luma pass) like a game draw: the views, cbuffers, samplers and render state bound for it,
   // and its arguments. Call it right after the "Draw()" or "Dispatch()", before unbinding or restoring anything: the debug draw (and MCP
   // readbacks) of the pass copy its output here too.
   // "name" is kept by the capture (a string literal), without one the pass goes by its shader's name. The bound pixel (or compute) shader is recorded
   // if it's one of Luma's ("native_shaders_definitions"), with the slots it uses.
   // From a draw callback, the entry goes before the game draw's ones if the draw went through (it ran after), after them otherwise (the pass replaced it).
   void Draw(ID3D11DeviceContext* native_device_context, const char* name, UINT vertex_count);
   void Dispatch(ID3D11DeviceContext* native_device_context, const char* name, UINT thread_group_count_x, UINT thread_group_count_y, UINT thread_group_count_z);
   // An entry with a name (and a target view or resource) only: a phase marker, or a pass without state of its own (e.g. SR)
   void Marker(ID3D11DeviceContext* native_device_context, const char* name, ID3D11DeviceChild* target = nullptr);
   // From a draw callback that changed what the game draw binds (e.g. a texture swapped for its own) and lets it draw: records the views, cbuffers
   // and samplers that were added, replaced or unbound on the draw's entries, next to the ones the game bound.
   void UpdateDraw(CommandListData& cmd_list_data, DeviceData& device_data, ID3D11DeviceContext* native_device_context);
   // Attaches a note to the draw being processed in the current capture (e.g. the game's motion vector decision).
   // Call it from "Game::OnDrawOrDispatch", where the shader entries of the current draw are the last ones of "cmd_list_data".
   // "value" is appended as "text=value" (formatted only while capturing, so it can be called on every draw).
   void Annotate(CommandListData& cmd_list_data, std::string_view text, std::optional<int64_t> value = std::nullopt);

   // Core: reads the slots a shader reads or writes from its reflections (RTVs, SRVs, UAVs, cbuffers and samplers), the ones a draw's entry records.
   // Game shaders are reflected at creation, Luma's when a pass is recorded.
   void ReflectShaderSlots(ID3D11ShaderReflection* shader_reflector, const D3D11_SHADER_DESC& shader_desc, Shader::CachedShader& cached_shader, bool& found_any_rtvs, bool& found_any_other_bindings);
   // Core: a game draw's entry for one of its shaders. Expects "s_mutex_generic", "s_mutex_dumping" and "device_data.mutex" (shared) locked.
   void AddDrawCall(std::vector<TraceDrawCallData>& trace_draw_calls_data, const DeviceData& device_data, ID3D11DeviceContext* native_device_context, uint64_t pipeline_handle, const DrawDispatchData& draw_dispatch_data);
   // Core: moves the entries queued on this thread for "native_device_context" into its trace at "index" (the other contexts' are dropped),
   // and returns how many. Expects the trace mutexes locked.
   size_t MoveQueued(std::vector<TraceDrawCallData>& trace_draw_calls_data, ID3D11DeviceContext* native_device_context, size_t index = SIZE_MAX);
   // Core: called by the draw hooks with what the draw callbacks returned, flags the entries of the draw ("trace_current_draw") if they didn't let it through
   void EndDraw(CommandListData& cmd_list_data, bool cancelled_or_replaced);
   void ClearQueued();
#else
   inline void Draw(ID3D11DeviceContext*, const char*, UINT) {}
   inline void Dispatch(ID3D11DeviceContext*, const char*, UINT, UINT, UINT) {}
   inline void Marker(ID3D11DeviceContext*, const char*, ID3D11DeviceChild* = nullptr) {}
   inline void UpdateDraw(CommandListData&, DeviceData&, ID3D11DeviceContext*) {}
   inline void Annotate(CommandListData&, std::string_view, std::optional<int64_t> = std::nullopt) {}
#endif
} // namespace Trace
