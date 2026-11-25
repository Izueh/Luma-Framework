#include "Resources/D3D12/D3D12ConstantCapture.hpp"

#include <cstring>

#include "Resources/D3D12/D3D12Descriptors.hpp"
#include "Resources/D3D12/D3D12ResourceDatabase.hpp"

namespace Luma {
namespace Resources {
namespace ConstantCapture {
namespace D3D12 {
bool ReadBoundCBuffer(ID3D12GraphicsCommandList* list, ShaderStage stage, UINT slot, std::span<std::byte> destination)
{
   if (!list || destination.empty())
      return false;
   const std::optional<Database::D3D12::BoundCBuffer> bound = Database::D3D12::FindBoundCBuffer(list, stage, slot);
   if (!bound || bound->address == 0)
      return false;
   if (bound->size != 0 && bound->size < destination.size())
      return false;
   const std::optional<Descriptors::D3D12::ResourceInfo> info = Descriptors::D3D12::FindBuffer(bound->address);
   if (!info || !info->mapped || bound->address < info->address)
      return false;
   const uint64_t offset = bound->address - info->address;
   if (offset + destination.size() > info->desc.Width)
      return false;
   std::memcpy(destination.data(), info->mapped + offset, destination.size());
   return true;
}
} // namespace D3D12
} // namespace ConstantCapture
} // namespace Resources
} // namespace Luma
