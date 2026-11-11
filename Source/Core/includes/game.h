#pragma once

#include "patch.hpp"

using namespace Shader;

// "Sub" device data per game, subclassable.
// Make sure all variables are default initialized.
struct GameDeviceData
{
   ManagedResources managed_resources;
};

struct GameInfo
{
   std::string title; // Public title (e.g., "Prey (2017)")
   std::string internal_name; // Internal short name (e.g. "White Knuckle"->"WK"), can be used by shaders etc
   uint32_t id = 0; // Internal ID (enum like). 0 is unknown/generic
   std::string shader_define;
   std::vector<std::string> mod_authors; // List of authors, in importance or arbitrary order
};

// Macro to take the "id" variable name and store it as a shader define name (to avoid defining them twice)
#define MAKE_GAME_INFO(title, internal_name, id, mod_authors) { title, internal_name, id, #id, mod_authors }

// Per game implementation
class Game
{
public:
   virtual ~Game() = default; // Avoids warnings. Using the destructor is not suggested if not to clear up persistently allocated memory.

   // The mod dll loaded (before init). Avoid locking mutexes in here.
   virtual void OnLoad(std::filesystem::path& file_path, bool failed = false) {}
   // The mod initialized. Avoid locking mutexes in here.
   virtual void OnInit(bool async) {}
   virtual void OnInitDevice(ID3D11Device* native_device, DeviceData& device_data) {}
   virtual void OnInitSwapchain(IDXGISwapChain* swapchain) {}
   // You can create and "append" your custom device data here
   virtual void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data)
   {
      // Example data
      device_data.game = new GameDeviceData;
   }
   // You can destroy your custom device data here
   virtual void OnDestroyDeviceData(DeviceData& device_data)
   {
      delete device_data.game;
      device_data.game = nullptr;
   }

   virtual bool OverrideCopyResource(ID3D11Device* native_device, DeviceData& device_data, uint64_t& dst_resource, uint64_t& src_resource) { return false; }
   virtual bool OverrideCopyTextureRegion(ID3D11Device* native_device, DeviceData& device_data, uint64_t& dst_resource, uint32_t dst_subresource, const D3D11_BOX* dst_box, uint64_t& src_resource, uint32_t src_subresource, const D3D11_BOX* src_box) { return false; }

   // ============================================================================
   // Shader patching providers. Select which ones a game uses with the
   // LUMA_PATCH_* macros (see patch.hpp). Both methods are available in sync
   // and async variants; a game can mix them (e.g. bytecode sync + recipe
   // async). Sync providers run either at pipeline creation time (in-place,
   // default) or at pipeline init with a pipeline clone (LUMA_PATCH_SYNC_MODE_CLONE).
   // Async providers run on a background thread; once a sync provider reaches a
   // definitive outcome (patched or no-patch-needed) for a shader, the same
   // method's async provider is skipped for it.
   // ============================================================================

   // Bytecode method: operates on the SHEX/SHDR bytecode chunk of the shader
   // ("code"/"size") with the full DXBC container available as "shader_object".
   // Return the modified bytecode; update "size" with its new size (must stay
   // DWORD-aligned). "shader_hash" is the luma hash of the original shader.
   virtual std::unique_ptr<std::byte[]> PatchShaderBytecodeSync(const std::byte* code, size_t& size, D3D11_SHADER_VERSION_TYPE type, uint64_t shader_hash = -1, const std::byte* shader_object = nullptr, size_t shader_object_size = 0) { return nullptr; }
   virtual std::unique_ptr<std::byte[]> PatchShaderBytecodeAsync(const std::byte* code, size_t& size, D3D11_SHADER_VERSION_TYPE type, uint64_t shader_hash = -1, const std::byte* shader_object = nullptr, size_t shader_object_size = 0) { return nullptr; }

   // Called for a draw or dispatch with a custom pipeline clone selected, after the recipes' resources were bound
   // (they come from "managed_resources", a clone missing any is swapped for its original shader for this draw).
   // Call "Shader::UsePipelineVariant()" (here or in any draw callback) to pick the original or the clone yourself.
   virtual DrawOrDispatchOverrideType OnDrawOrDispatchCustomPipeline(ID3D11DeviceContext* native_device_context,
      CommandListData& cmd_list_data, DeviceData& device_data,
      const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes,
      std::function<void()>* original_draw_dispatch_func = nullptr)
   {
      return DrawOrDispatchOverrideType::None;
   }

   // Called for every game's valid draw call (any type),
   // this is where you can override passes, add new ones, cancel other ones etc.
   // Requires "ENABLE_POST_DRAW_DISPATCH_CALLBACK" to be enabled, given it has a performance impact.
   // Return true to cancel the original call.
   // Use "original_draw_dispatch_func" to call the original draw/dispatch within your function (you have to cancel it manually if you do so, as that's not tracked). This is useful if you want to add any post draw/dispatch behaviour. "ENABLE_POST_DRAW_DISPATCH_CALLBACK" is required for the it to work. Note that some advanced debugging behaviours might conflict if you replace the draw call.
   // Not called for draws/dispatches that matched a callback in "draw_callbacks_by_shader_hashes"/"dispatch_callbacks_by_shader_hashes" (which have this same signature).
   virtual DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, ShaderStages stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func = nullptr) { return DrawOrDispatchOverrideType::None; }
   // This is called every frame just before sending out the final image to the display (the swapchain).
   // You can reliable reset any per frame setting here.
   virtual void OnPresent(ID3D11Device* native_device, DeviceData& device_data) {}
   virtual void UpdateLumaInstanceDataCB(CB::LumaInstanceDataPadded& data, CommandListData& cmd_list_data, DeviceData& device_data) {}
   // Retrieves the game's "global" (main, per view, ...) cbuffer data
   virtual bool UpdateGlobalCB(const void* global_buffer_data_ptr, ID3D11Device* device) { return false; }

   // Load configs on boot
   virtual void LoadConfigs() {}
   // Triggered when the user swaps between SDR and HDR output (in the game settings, not the display state)
   virtual void OnDisplayModeChanged() {}
   // Called when users change the "advanced" settings shader defines (e.g. you can grey out certain settings based on other settings here)
   virtual void OnShaderDefinesChanged() {}
   // Draw and save your settings here
   virtual void DrawImGuiSettings(DeviceData& device_data) {}
#if DEVELOPMENT
   // Same as "DrawImGuiSettings()" but for development settings
   virtual void DrawImGuiDevSettings(DeviceData& device_data) {}
#endif // DEVELOPMENT
#if DEVELOPMENT || TEST
   // You can print game specific information here (e.g. the weapon FOV, once you got access to the projection matrix)
   virtual void PrintImGuiInfo(const DeviceData& device_data) {}
#endif
   // About and Credits section
   virtual void PrintImGuiAbout() {}

   // In case you knew, you can specify whether the game was paused here
   virtual bool IsGamePaused(const DeviceData& device_data) const { return false; }
   // Values between 0 and 0.25 are usually good
   virtual float GetTonemapUIBackgroundAmount(const DeviceData& device_data) const { return 0.f; }
   // In case your SR implementation had any extra resources, you can clean them up here
   virtual void CleanExtraSRResources(DeviceData& device_data) {}

   // Native D3D11/DXGI callbacks. Overriding one is enough: "SetGame<T>()" detects the override at compile time and
   // installs the hooks it needs (see "LUMA_GAME_HOOKS" below). Each one only runs for the parts that asked for its hook.
   // Order: the game wraps Core. A "Pre" runs before Core's, and Core sees any argument the game edited.
   // A "Pre" returning true skips the call (Core and the original), as if it never happened.
   // A "Post" runs after Core's, so Core's tracking is up to date.
   // Calls the game issues from inside a callback aren't seen by Core (or by any callback): to change a call, edit its
   // arguments instead of skipping it and issuing a new one.
   // Extra hooks to install without overriding anything (rarely needed).
   Luma::Hooks::D3D11::Functions native_hooks;

   virtual void PreCreateDevice(UINT& flags, const D3D_FEATURE_LEVEL*& feature_levels, UINT& feature_level_count) {}
   virtual void PostCreateDevice(ID3D11Device* device) {}
   virtual void OnDeviceDestroyed(ID3D11Device* device) {}
   virtual void PreCreateSwapChain(DXGI_SWAP_CHAIN_DESC& desc) {}
   virtual void PreCreateSwapChainForHwnd(DXGI_SWAP_CHAIN_DESC1& desc, DXGI_SWAP_CHAIN_FULLSCREEN_DESC& fullscreen_desc, HWND hwnd) {}
   virtual void PostCreateSwapChain(IDXGISwapChain* swapchain, ID3D11Device* device) {}
   virtual void OnSwapChainDestroyed(ID3D11Device* device, const IDXGISwapChain* swapchain) {}
   virtual void PreResizeBuffers(IDXGISwapChain* swapchain, UINT& count, UINT width, UINT height, DXGI_FORMAT& format, UINT& flags, const UINT* node_masks = nullptr, IUnknown* const* present_queues = nullptr) {}
   virtual void PostResizeBuffers(IDXGISwapChain* swapchain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags) {}
   virtual bool PreSetFullscreenState(IDXGISwapChain* swapchain, BOOL fullscreen, IDXGIOutput* output) { return false; }
   virtual void PrePresent(IDXGISwapChain* swapchain) {}
   virtual void PostPresent(IDXGISwapChain* swapchain) {}
   virtual void PreCreateBuffer(ID3D11Device* device, D3D11_BUFFER_DESC& desc, const D3D11_SUBRESOURCE_DATA*& initial_data) {}
   virtual void PostCreateBuffer(ID3D11Device* device, const D3D11_BUFFER_DESC& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Buffer* buffer) {}
   virtual void PreCreateTexture1D(ID3D11Device* device, D3D11_TEXTURE1D_DESC& desc, const D3D11_SUBRESOURCE_DATA*& initial_data) {}
   virtual void PostCreateTexture1D(ID3D11Device* device, const D3D11_TEXTURE1D_DESC& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture1D* texture) {}
   virtual void PreCreateTexture2D(ID3D11Device* device, D3D11_TEXTURE2D_DESC1& desc, const D3D11_SUBRESOURCE_DATA*& initial_data) {}
   virtual void PostCreateTexture2D(ID3D11Device* device, const D3D11_TEXTURE2D_DESC1& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture2D* texture) {}
   virtual void PreCreateTexture3D(ID3D11Device* device, D3D11_TEXTURE3D_DESC1& desc, const D3D11_SUBRESOURCE_DATA*& initial_data) {}
   virtual void PostCreateTexture3D(ID3D11Device* device, const D3D11_TEXTURE3D_DESC1& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture3D* texture) {}
   virtual void PostOpenSharedResource(ID3D11Device* device, ID3D11Resource* resource) {}
   virtual void OnResourceDestroyed(ID3D11Device* device, const ID3D11Resource* resource) {}
   virtual void PreCreateShaderResourceView(ID3D11Device* device, ID3D11Resource* resource, std::optional<D3D11_SHADER_RESOURCE_VIEW_DESC1>& desc) {}
   virtual void PostCreateShaderResourceView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_SHADER_RESOURCE_VIEW_DESC1* desc, ID3D11ShaderResourceView* view) {}
   virtual void PreCreateRenderTargetView(ID3D11Device* device, ID3D11Resource* resource, std::optional<D3D11_RENDER_TARGET_VIEW_DESC1>& desc) {}
   virtual void PostCreateRenderTargetView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_RENDER_TARGET_VIEW_DESC1* desc, ID3D11RenderTargetView* view) {}
   virtual void PreCreateUnorderedAccessView(ID3D11Device* device, ID3D11Resource* resource, std::optional<D3D11_UNORDERED_ACCESS_VIEW_DESC1>& desc) {}
   virtual void PostCreateUnorderedAccessView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC1* desc, ID3D11UnorderedAccessView* view) {}
   virtual void PreCreateDepthStencilView(ID3D11Device* device, ID3D11Resource* resource, std::optional<D3D11_DEPTH_STENCIL_VIEW_DESC>& desc) {}
   virtual void PostCreateDepthStencilView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc, ID3D11DepthStencilView* view) {}
   virtual void OnViewDestroyed(ID3D11Device* device, const ID3D11View* view) {}
   virtual void PreCreateSamplerState(ID3D11Device* device, D3D11_SAMPLER_DESC& desc) {}
   virtual void PostCreateSamplerState(ID3D11Device* device, const D3D11_SAMPLER_DESC& desc, ID3D11SamplerState* sampler) {}
   virtual void OnSamplerDestroyed(ID3D11Device* device, const ID3D11SamplerState* sampler) {}
   // "CreateBlendState" reports the DESC1 with logic ops disabled
   virtual void PreCreateBlendState(ID3D11Device* device, D3D11_BLEND_DESC1& desc) {}
   virtual void PostCreateBlendState(ID3D11Device* device, const D3D11_BLEND_DESC1& desc, ID3D11BlendState* blend_state) {}
   virtual void PreCreateVertexShader(ID3D11Device* device, const void*& bytecode, SIZE_T& length) {}
   virtual void PostCreateVertexShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11VertexShader* shader) {}
   virtual void PreCreateHullShader(ID3D11Device* device, const void*& bytecode, SIZE_T& length) {}
   virtual void PostCreateHullShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11HullShader* shader) {}
   virtual void PreCreateDomainShader(ID3D11Device* device, const void*& bytecode, SIZE_T& length) {}
   virtual void PostCreateDomainShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11DomainShader* shader) {}
   virtual void PreCreateGeometryShader(ID3D11Device* device, const void*& bytecode, SIZE_T& length) {}
   virtual void PostCreateGeometryShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11GeometryShader* shader) {}
   virtual void PreCreateGeometryShaderWithStreamOutput(ID3D11Device* device, const void*& bytecode, SIZE_T& length) {}
   virtual void PostCreateGeometryShaderWithStreamOutput(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11GeometryShader* shader) {}
   virtual void PreCreatePixelShader(ID3D11Device* device, const void*& bytecode, SIZE_T& length) {}
   virtual void PostCreatePixelShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11PixelShader* shader) {}
   virtual void PreCreateComputeShader(ID3D11Device* device, const void*& bytecode, SIZE_T& length) {}
   virtual void PostCreateComputeShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11ComputeShader* shader) {}
   virtual void OnShaderDestroyed(ID3D11Device* device, const ID3D11DeviceChild* shader) {}
   virtual void PostCreateInputLayout(ID3D11Device* device, const D3D11_INPUT_ELEMENT_DESC* elements, UINT count, const void* bytecode, SIZE_T length, ID3D11InputLayout* layout) {}
   virtual void OnInputLayoutDestroyed(ID3D11Device* device, const ID3D11InputLayout* layout) {}
   virtual bool PreVSSetShader(ID3D11DeviceContext* context, ID3D11VertexShader*& shader) { return false; }
   virtual bool PreHSSetShader(ID3D11DeviceContext* context, ID3D11HullShader*& shader) { return false; }
   virtual bool PreDSSetShader(ID3D11DeviceContext* context, ID3D11DomainShader*& shader) { return false; }
   virtual bool PreGSSetShader(ID3D11DeviceContext* context, ID3D11GeometryShader*& shader) { return false; }
   virtual bool PrePSSetShader(ID3D11DeviceContext* context, ID3D11PixelShader*& shader) { return false; }
   virtual bool PreCSSetShader(ID3D11DeviceContext* context, ID3D11ComputeShader*& shader) { return false; }
   virtual bool PreVSSetShaderResources(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11ShaderResourceView** views) { return false; }
   virtual bool PreHSSetShaderResources(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11ShaderResourceView** views) { return false; }
   virtual bool PreDSSetShaderResources(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11ShaderResourceView** views) { return false; }
   virtual bool PreGSSetShaderResources(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11ShaderResourceView** views) { return false; }
   virtual bool PrePSSetShaderResources(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11ShaderResourceView** views) { return false; }
   virtual bool PreCSSetShaderResources(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11ShaderResourceView** views) { return false; }
   virtual bool PreVSSetConstantBuffers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11Buffer** buffers, const UINT* first_constant = nullptr, const UINT* num_constants = nullptr) { return false; }
   virtual bool PreHSSetConstantBuffers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11Buffer** buffers, const UINT* first_constant = nullptr, const UINT* num_constants = nullptr) { return false; }
   virtual bool PreDSSetConstantBuffers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11Buffer** buffers, const UINT* first_constant = nullptr, const UINT* num_constants = nullptr) { return false; }
   virtual bool PreGSSetConstantBuffers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11Buffer** buffers, const UINT* first_constant = nullptr, const UINT* num_constants = nullptr) { return false; }
   virtual bool PrePSSetConstantBuffers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11Buffer** buffers, const UINT* first_constant = nullptr, const UINT* num_constants = nullptr) { return false; }
   virtual bool PreCSSetConstantBuffers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11Buffer** buffers, const UINT* first_constant = nullptr, const UINT* num_constants = nullptr) { return false; }
   virtual bool PreVSSetSamplers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11SamplerState** samplers) { return false; }
   virtual bool PreHSSetSamplers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11SamplerState** samplers) { return false; }
   virtual bool PreDSSetSamplers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11SamplerState** samplers) { return false; }
   virtual bool PreGSSetSamplers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11SamplerState** samplers) { return false; }
   virtual bool PrePSSetSamplers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11SamplerState** samplers) { return false; }
   virtual bool PreCSSetSamplers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11SamplerState** samplers) { return false; }
   virtual bool PreCSSetUnorderedAccessViews(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11UnorderedAccessView** views, const UINT* initial_counts) { return false; }
   virtual bool PreOMSetRenderTargets(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView** views, ID3D11DepthStencilView*& depth_stencil) { return false; }
   virtual bool PreOMSetRenderTargetsAndUnorderedAccessViews(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView** views, ID3D11DepthStencilView*& depth_stencil, UINT uav_start, UINT uav_count, ID3D11UnorderedAccessView** uavs, const UINT* initial_counts) { return false; }
   virtual void PostRSSetViewports(ID3D11DeviceContext* context, UINT count, const D3D11_VIEWPORT* viewports) {}
   virtual void PostRSSetScissorRects(ID3D11DeviceContext* context, UINT count, const D3D11_RECT* rects) {}
   virtual void PostOMSetBlendState(ID3D11DeviceContext* context, ID3D11BlendState* state, const FLOAT blend_factor[4], UINT sample_mask) {}
   virtual void PostOMSetDepthStencilState(ID3D11DeviceContext* context, ID3D11DepthStencilState* state, UINT stencil_ref) {}
   virtual void PostIASetVertexBuffers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11Buffer* const* buffers, const UINT* strides, const UINT* offsets) {}
   virtual void PostIASetIndexBuffer(ID3D11DeviceContext* context, ID3D11Buffer* buffer, DXGI_FORMAT format, UINT offset) {}
   virtual void PostClearState(ID3D11DeviceContext* context) {}
   virtual void PostMap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE& mapped) {}
   virtual bool PreUnmap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource) { return false; }
   virtual void PostUnmap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource) {}
   virtual bool PreUpdateSubresource(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, const D3D11_BOX* box, const void* data, UINT row_pitch, UINT depth_pitch, UINT copy_flags = 0) { return false; }
   virtual bool PreCopyResource(ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source) { return false; }
   virtual bool PreCopySubresourceRegion(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, UINT x, UINT y, UINT z, ID3D11Resource* source, UINT source_subresource, const D3D11_BOX* box, UINT copy_flags = 0) { return false; }
   virtual bool PreResolveSubresource(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, ID3D11Resource* source, UINT source_subresource, DXGI_FORMAT format) { return false; }
   virtual bool PreClearRenderTargetView(ID3D11DeviceContext* context, ID3D11RenderTargetView* view, const FLOAT color[4]) { return false; }
   virtual bool PreCopyStructureCount(ID3D11DeviceContext* context, ID3D11Buffer* dest, UINT dest_offset, ID3D11UnorderedAccessView* source) { return false; }
   virtual bool PreClearDepthStencilView(ID3D11DeviceContext* context, ID3D11DepthStencilView* view, UINT flags, FLOAT depth, UINT8 stencil) { return false; }
   virtual bool PreClearUnorderedAccessViewUint(ID3D11DeviceContext* context, ID3D11UnorderedAccessView* view, const UINT values[4]) { return false; }
   virtual bool PreClearUnorderedAccessViewFloat(ID3D11DeviceContext* context, ID3D11UnorderedAccessView* view, const FLOAT values[4]) { return false; }
   virtual bool PreClearView(ID3D11DeviceContext* context, ID3D11View* view, const FLOAT color[4], const D3D11_RECT* rects, UINT rect_count) { return false; }
   virtual bool PreDraw(ID3D11DeviceContext* context, UINT vertex_count, UINT start_vertex) { return false; }
   virtual bool PreDrawInstanced(ID3D11DeviceContext* context, UINT vertex_count, UINT instance_count, UINT start_vertex, UINT start_instance) { return false; }
   virtual bool PreDrawIndexed(ID3D11DeviceContext* context, UINT index_count, UINT start_index, INT base_vertex) { return false; }
   virtual bool PreDrawIndexedInstanced(ID3D11DeviceContext* context, UINT index_count, UINT instance_count, UINT start_index, INT base_vertex, UINT start_instance) { return false; }
   virtual bool PreDrawAuto(ID3D11DeviceContext* context) { return false; }
   virtual bool PreDrawInstancedIndirect(ID3D11DeviceContext* context, ID3D11Buffer* arguments, UINT offset) { return false; }
   virtual bool PreDrawIndexedInstancedIndirect(ID3D11DeviceContext* context, ID3D11Buffer* arguments, UINT offset) { return false; }
   virtual bool PreDispatch(ID3D11DeviceContext* context, UINT x, UINT y, UINT z) { return false; }
   virtual bool PreDispatchIndirect(ID3D11DeviceContext* context, ID3D11Buffer* arguments, UINT offset) { return false; }
   virtual void PostCreateDeferredContext(ID3D11Device* device, ID3D11DeviceContext* context) {}
   virtual void OnDeferredContextDestroyed(ID3D11Device* device, const ID3D11DeviceContext* context) {}
   virtual void PostFinishCommandList(ID3D11DeviceContext* context, BOOL restore, ID3D11CommandList* list) {}
   virtual void OnCommandListDestroyed(ID3D11Device* device, const ID3D11CommandList* list) {}
   virtual bool PreExecuteCommandList(ID3D11DeviceContext* context, ID3D11CommandList* list, BOOL restore) { return false; }
   virtual bool PreFlush(ID3D11DeviceContext* context) { return false; }
};

// Every native callback virtual of "Game", with the hooks it needs. Device and swapchain creation (and destruction)
// callbacks are not listed, their hooks are always installed.
#define LUMA_GAME_HOOKS(X)                                                                                                 \
   X(PrePresent, Present)                                                                                                  \
   X(PostPresent, Present)                                                                                                 \
   X(PreResizeBuffers, ResizeBuffers)                                                                                      \
   X(PostResizeBuffers, ResizeBuffers)                                                                                     \
   X(PreSetFullscreenState, SetFullscreenState)                                                                            \
   X(PreCreateBuffer, CreateBuffer)                                                                                        \
   X(PostCreateBuffer, CreateBuffer)                                                                                       \
   X(PreCreateTexture1D, CreateTexture1D)                                                                                  \
   X(PostCreateTexture1D, CreateTexture1D)                                                                                 \
   X(PreCreateTexture2D, CreateTexture2D)                                                                                  \
   X(PostCreateTexture2D, CreateTexture2D)                                                                                 \
   X(PreCreateTexture3D, CreateTexture3D)                                                                                  \
   X(PostCreateTexture3D, CreateTexture3D)                                                                                 \
   X(PostOpenSharedResource, OpenSharedResource)                                                                           \
   X(OnResourceDestroyed, CreateBuffer, CreateTexture1D, CreateTexture2D, CreateTexture3D, OpenSharedResource)            \
   X(PreCreateShaderResourceView, CreateShaderResourceView)                                                                \
   X(PostCreateShaderResourceView, CreateShaderResourceView)                                                               \
   X(PreCreateRenderTargetView, CreateRenderTargetView)                                                                    \
   X(PostCreateRenderTargetView, CreateRenderTargetView)                                                                   \
   X(PreCreateUnorderedAccessView, CreateUnorderedAccessView)                                                              \
   X(PostCreateUnorderedAccessView, CreateUnorderedAccessView)                                                             \
   X(PreCreateDepthStencilView, CreateDepthStencilView)                                                                    \
   X(PostCreateDepthStencilView, CreateDepthStencilView)                                                                   \
   X(OnViewDestroyed, CreateShaderResourceView, CreateRenderTargetView, CreateUnorderedAccessView, CreateDepthStencilView) \
   X(PreCreateSamplerState, CreateSamplerState)                                                                            \
   X(PostCreateSamplerState, CreateSamplerState)                                                                           \
   X(OnSamplerDestroyed, CreateSamplerState)                                                                               \
   X(PreCreateBlendState, CreateBlendState)                                                                                \
   X(PostCreateBlendState, CreateBlendState)                                                                               \
   X(PreCreateVertexShader, CreateVertexShader)                                                                            \
   X(PostCreateVertexShader, CreateVertexShader)                                                                           \
   X(PreCreateHullShader, CreateHullShader)                                                                                \
   X(PostCreateHullShader, CreateHullShader)                                                                               \
   X(PreCreateDomainShader, CreateDomainShader)                                                                            \
   X(PostCreateDomainShader, CreateDomainShader)                                                                           \
   X(PreCreateGeometryShader, CreateGeometryShader)                                                                        \
   X(PostCreateGeometryShader, CreateGeometryShader)                                                                       \
   X(PreCreateGeometryShaderWithStreamOutput, CreateGeometryShaderWithStreamOutput)                                        \
   X(PostCreateGeometryShaderWithStreamOutput, CreateGeometryShaderWithStreamOutput)                                       \
   X(PreCreatePixelShader, CreatePixelShader)                                                                              \
   X(PostCreatePixelShader, CreatePixelShader)                                                                             \
   X(PreCreateComputeShader, CreateComputeShader)                                                                          \
   X(PostCreateComputeShader, CreateComputeShader)                                                                         \
   X(OnShaderDestroyed, CreateVertexShader, CreateHullShader, CreateDomainShader, CreateGeometryShader,                    \
      CreateGeometryShaderWithStreamOutput, CreatePixelShader, CreateComputeShader)                                        \
   X(PostCreateInputLayout, CreateInputLayout)                                                                             \
   X(OnInputLayoutDestroyed, CreateInputLayout)                                                                            \
   X(PreVSSetShader, VSSetShader)                                                                                          \
   X(PreHSSetShader, HSSetShader)                                                                                          \
   X(PreDSSetShader, DSSetShader)                                                                                          \
   X(PreGSSetShader, GSSetShader)                                                                                          \
   X(PrePSSetShader, PSSetShader)                                                                                          \
   X(PreCSSetShader, CSSetShader)                                                                                          \
   X(PreVSSetShaderResources, VSSetShaderResources)                                                                        \
   X(PreHSSetShaderResources, HSSetShaderResources)                                                                        \
   X(PreDSSetShaderResources, DSSetShaderResources)                                                                        \
   X(PreGSSetShaderResources, GSSetShaderResources)                                                                        \
   X(PrePSSetShaderResources, PSSetShaderResources)                                                                        \
   X(PreCSSetShaderResources, CSSetShaderResources)                                                                        \
   X(PreVSSetConstantBuffers, VSSetConstantBuffers)                                                                        \
   X(PreHSSetConstantBuffers, HSSetConstantBuffers)                                                                        \
   X(PreDSSetConstantBuffers, DSSetConstantBuffers)                                                                        \
   X(PreGSSetConstantBuffers, GSSetConstantBuffers)                                                                        \
   X(PrePSSetConstantBuffers, PSSetConstantBuffers)                                                                        \
   X(PreCSSetConstantBuffers, CSSetConstantBuffers)                                                                        \
   X(PreVSSetSamplers, VSSetSamplers)                                                                                      \
   X(PreHSSetSamplers, HSSetSamplers)                                                                                      \
   X(PreDSSetSamplers, DSSetSamplers)                                                                                      \
   X(PreGSSetSamplers, GSSetSamplers)                                                                                      \
   X(PrePSSetSamplers, PSSetSamplers)                                                                                      \
   X(PreCSSetSamplers, CSSetSamplers)                                                                                      \
   X(PreCSSetUnorderedAccessViews, CSSetUnorderedAccessViews)                                                              \
   X(PreOMSetRenderTargets, OMSetRenderTargets)                                                                            \
   X(PreOMSetRenderTargetsAndUnorderedAccessViews, OMSetRenderTargetsAndUnorderedAccessViews)                              \
   X(PostRSSetViewports, RSSetViewports)                                                                                   \
   X(PostRSSetScissorRects, RSSetScissorRects)                                                                             \
   X(PostOMSetBlendState, OMSetBlendState)                                                                                 \
   X(PostOMSetDepthStencilState, OMSetDepthStencilState)                                                                   \
   X(PostIASetVertexBuffers, IASetVertexBuffers)                                                                           \
   X(PostIASetIndexBuffer, IASetIndexBuffer)                                                                               \
   X(PostClearState, ClearState)                                                                                           \
   X(PostMap, Map)                                                                                                         \
   X(PreUnmap, Unmap)                                                                                                      \
   X(PostUnmap, Unmap)                                                                                                     \
   X(PreUpdateSubresource, UpdateSubresource)                                                                              \
   X(PreCopyResource, CopyResource)                                                                                        \
   X(PreCopySubresourceRegion, CopySubresourceRegion)                                                                      \
   X(PreResolveSubresource, ResolveSubresource)                                                                            \
   X(PreClearRenderTargetView, ClearRenderTargetView)                                                                      \
   X(PreCopyStructureCount, CopyStructureCount)                                                                            \
   X(PreClearDepthStencilView, ClearDepthStencilView)                                                                      \
   X(PreClearUnorderedAccessViewUint, ClearUnorderedAccessViewUint)                                                        \
   X(PreClearUnorderedAccessViewFloat, ClearUnorderedAccessViewFloat)                                                      \
   X(PreClearView, ClearView)                                                                                              \
   X(PreDraw, Draw)                                                                                                        \
   X(PreDrawInstanced, DrawInstanced)                                                                                      \
   X(PreDrawIndexed, DrawIndexed)                                                                                          \
   X(PreDrawIndexedInstanced, DrawIndexedInstanced)                                                                        \
   X(PreDrawAuto, DrawAuto)                                                                                                \
   X(PreDrawInstancedIndirect, DrawInstancedIndirect)                                                                      \
   X(PreDrawIndexedInstancedIndirect, DrawIndexedInstancedIndirect)                                                        \
   X(PreDispatch, Dispatch)                                                                                                \
   X(PreDispatchIndirect, DispatchIndirect)                                                                                \
   X(PostCreateDeferredContext, CreateDeferredContext)                                                                     \
   X(OnDeferredContextDestroyed, CreateDeferredContext)                                                                    \
   X(PostFinishCommandList, FinishCommandList)                                                                             \
   X(OnCommandListDestroyed, FinishCommandList)                                                                            \
   X(PreExecuteCommandList, ExecuteCommandList)                                                                            \
   X(PreFlush, Flush)

// The hooks needed by the callbacks "T" overrides. "&T::X" has the type of the class that last declared "X", so it
// differs from "&Game::X" exactly when "T" (or a base between it and "Game") overrides it.
// An override must be public, or this fails to compile.
template <typename T>
Luma::Hooks::D3D11::Functions DetectGameHooks()
{
   static_assert(std::is_base_of_v<Game, T>);
   using Luma::Hooks::D3D11::Function;
   using enum Luma::Hooks::D3D11::Function;
   Luma::Hooks::D3D11::Functions functions;
#define LUMA_DETECT_GAME_HOOK(Virtual, ...)                                         \
   if constexpr (!std::is_same_v<decltype(&T::Virtual), decltype(&Game::Virtual)>) \
   {                                                                                \
      for (Function function : {__VA_ARGS__})                                       \
         functions.set(size_t(function));                                           \
   }
   LUMA_GAME_HOOKS(LUMA_DETECT_GAME_HOOK)
#undef LUMA_DETECT_GAME_HOOK
   return functions;
}
