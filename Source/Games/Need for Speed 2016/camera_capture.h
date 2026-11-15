#pragma once

#include <mutex>

#include "camera.h"
#include "log.h"

// Captures the main camera when the game binds it for a draw, straight from the CPU memory the game uploaded it to. Its constant buffer
// is renamed (discarded) mid frame, so by the resolve the camera can be gone, and some views draw nothing that binds it after the scene.
class CameraCapture
{
public:
   // A constant buffer binding: the buffer, and where its constants start in it
   struct Binding
   {
      const ID3D11Resource* buffer = nullptr;
      uint64_t offset = 0; // In bytes
   };

   // The large dynamic constant buffers the game uploads its per frame constants to
   static bool MayHoldCamera(const D3D11_BUFFER_DESC& desc);
   // The camera's constant buffer, if this vertex shader binding call sets it (no buffer otherwise)
   static Binding FindBinding(UINT start, UINT count, ID3D11Buffer* const* buffers, const UINT* first_constant);

   // From any thread
   void OnBufferMapped(const ID3D11Resource* buffer, const void* data, uint64_t size);
   void OnBufferDestroyed(const ID3D11Resource* buffer);
   // From the immediate context, so the game's render thread, where the camera is also used (the resolve and present)
   void OnCameraBound(Binding binding, RenderSize render_size);

   // The main camera, as last bound this frame (null if none was)
   const Camera* GetCamera() const
   {
      if (!has_camera)
         return nullptr;
      return &camera;
   }
   void EndFrame();

private:
   struct MappedBuffer
   {
      const ID3D11Resource* buffer;
      const uint8_t* data;
      uint64_t size;
   };
   static constexpr UINT kCameraSlot = 2; // In the scene's vertex shaders
   // "first_constant" counts float4 constants
   static constexpr UINT kConstantSize = 16;
   static constexpr size_t kMaxMappedBuffers = 64;

   // The caller holds the mutex
   MappedBuffer* FindMappedBuffer(const ID3D11Resource* buffer);

   // The mutex guards the mapped buffers. The camera is only used on the render thread.
   std::mutex mutex;
   MappedBuffer mapped_buffers[kMaxMappedBuffers] = {};
   size_t mapped_count = 0;
   bool logged_overflow = false;
   Camera camera = {};
   bool has_camera = false;
};

// The game suballocates its per frame constants from a few big dynamic buffers (64 KB and up), so small ones are skipped before any lock
inline bool CameraCapture::MayHoldCamera(const D3D11_BUFFER_DESC& desc)
{
   return (desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) && desc.Usage == D3D11_USAGE_DYNAMIC && desc.ByteWidth >= 65536;
}

// Runs for every vertex shader constant buffer binding the game makes, so integer checks only. A call can cover a range of slots, and
// "VSSetConstantBuffers1" binds a range of a buffer.
inline CameraCapture::Binding CameraCapture::FindBinding(UINT start, UINT count, ID3D11Buffer* const* buffers, const UINT* first_constant)
{
   if (!buffers || kCameraSlot < start || kCameraSlot >= start + count)
      return {};
   const UINT index = kCameraSlot - start;
   Binding binding = {.buffer = buffers[index]};
   if (first_constant)
      binding.offset = uint64_t(first_constant[index]) * kConstantSize;
   return binding;
}

// The latest map wins: a discard returns the buffer's new memory, which the later uploads go to
inline void CameraCapture::OnBufferMapped(const ID3D11Resource* buffer, const void* data, uint64_t size)
{
   const MappedBuffer map = {buffer, static_cast<const uint8_t*>(data), size};
   const std::lock_guard lock(mutex);
   if (MappedBuffer* known = FindMappedBuffer(buffer))
   {
      *known = map;
   }
   else if (mapped_count < kMaxMappedBuffers)
   {
      mapped_buffers[mapped_count++] = map;
   }
   else if (!logged_overflow)
   {
      logged_overflow = true;
      LogDiagnostic(LogLevel::Warning, "over {} large constant buffers were mapped in a frame, so the camera can be missed", kMaxMappedBuffers);
   }
}

// A destroyed buffer's address can be reused by a new one
inline void CameraCapture::OnBufferDestroyed(const ID3D11Resource* buffer)
{
   const std::lock_guard lock(mutex);
   if (MappedBuffer* known = FindMappedBuffer(buffer))
      *known = mapped_buffers[--mapped_count];
}

// Keeps the bound block if it's the main camera, read from where its buffer was last mapped (the game wrote it there before binding it)
inline void CameraCapture::OnCameraBound(Binding binding, RenderSize render_size)
{
   const std::lock_guard lock(mutex);
   const MappedBuffer* map = FindMappedBuffer(binding.buffer);
   // The binding's offset is into the buffer, so the block has to be within what was mapped
   if (!map || binding.offset + sizeof(Camera) > map->size)
      return;
   const auto& bound = *reinterpret_cast<const Camera*>(map->data + binding.offset);
   if (!bound.IsMain(render_size))
      return;
   camera = bound;
   has_camera = true;
}

// At present: the mapped memory is only valid until the buffers are mapped again, and each frame binds its own camera
inline void CameraCapture::EndFrame()
{
   const std::lock_guard lock(mutex);
   mapped_count = 0;
   has_camera = false;
}

// A linear search: only a handful of large buffers are mapped in a frame
inline CameraCapture::MappedBuffer* CameraCapture::FindMappedBuffer(const ID3D11Resource* buffer)
{
   for (size_t i = 0; i < mapped_count; i++)
      if (mapped_buffers[i].buffer == buffer)
         return &mapped_buffers[i];
   return nullptr;
}
