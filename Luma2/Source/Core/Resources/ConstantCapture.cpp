#include "Resources/ConstantCapture.hpp"

#include <atomic>
#include <vector>

namespace Luma {
namespace Resources {
namespace ConstantCapture {
namespace {
struct RegisteredCallback
{
   uint32_t byte_size = 0;
   BufferWriteCallback callback = nullptr;
};

Options options;
std::vector<RegisteredCallback> write_callbacks;
std::atomic<bool> paused_for_frame = false;
} // namespace

void SetOptions(const Options& value)
{
   options = value;
}

const Options& GetOptions()
{
   return options;
}

void RegisterWriteCallback(uint32_t byte_size, BufferWriteCallback callback)
{
   if (callback)
      write_callbacks.push_back({byte_size, callback});
}

bool HasWriteCallbacks()
{
   return !write_callbacks.empty();
}

void DispatchWrite(Device device, uint32_t byte_size, std::span<const std::byte> bytes)
{
   for (const RegisteredCallback& entry : write_callbacks)
   {
      if (entry.byte_size == 0 || entry.byte_size == byte_size)
         entry.callback(device, bytes);
   }
}

void PauseForFrame()
{
   paused_for_frame.store(true, std::memory_order_relaxed);
}

bool IsPausedForFrame()
{
   return paused_for_frame.load(std::memory_order_relaxed);
}

void AdvanceFrame()
{
   paused_for_frame.store(false, std::memory_order_relaxed);
}

bool IsNeeded()
{
   return options.enable || !write_callbacks.empty();
}

Hooks::D3D11::Functions GetD3D11Functions()
{
   Hooks::D3D11::Functions functions;
   if (write_callbacks.empty())
      return functions;
   using enum Hooks::D3D11::Function;
   for (const Hooks::D3D11::Function fn : {Map, Unmap, UpdateSubresource})
      functions.set(size_t(fn));
   return functions;
}

Hooks::D3D12::Functions GetD3D12Functions()
{
   Hooks::D3D12::Functions functions;
   if (!IsNeeded())
      return functions;
   using enum Hooks::D3D12::Function;
   for (const Hooks::D3D12::Function fn : {CreateResource, CreateDescriptorHeap, CreateView, CopyDescriptors, SetRootSignature, SetRootArguments})
      functions.set(size_t(fn));
   return functions;
}
} // namespace ConstantCapture
} // namespace Resources
} // namespace Luma
