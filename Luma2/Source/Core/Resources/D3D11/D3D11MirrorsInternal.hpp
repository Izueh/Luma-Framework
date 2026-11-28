#pragma once

#include <array>
#include <optional>

#include <d3d11_4.h>
#include <wrl/client.h>

#include "Resources/D3D11/D3D11Mirrors.hpp"
#include "Resources/ResourceChains.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/ResourceUpgrades.hpp"

namespace Luma {
namespace Resources {
namespace Mirrors {
namespace D3D11 {
namespace Internal {
using OwnedResource = Microsoft::WRL::ComPtr<ID3D11Resource>;
using ResourceMirrorState = MirrorState<ID3D11Resource, OwnedResource>;

struct ContextChainState
{
   std::array<bool, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> ps_srvs_upgraded = {};
   std::array<bool, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> cs_srvs_upgraded = {};
   std::array<bool, D3D11_1_UAV_SLOT_COUNT> ps_uavs_upgraded = {};
   std::array<bool, D3D11_1_UAV_SLOT_COUNT> cs_uavs_upgraded = {};
   bool any_ps_srvs_upgraded = false;
   bool any_cs_srvs_upgraded = false;
   bool any_ps_uavs_upgraded = false;
   bool any_cs_uavs_upgraded = false;
   ContextScalingSnapshot scaling = {};
};

std::optional<Upgrade::TextureInfo> DescribeResource(ID3D11Resource* resource);
Upgrade::FrameState GetFrameState(ID3D11Device* device);
bool MaterializeChainMirror(ID3D11Device* device, ID3D11Resource* resource, const Upgrade::TextureInfo& original_info, const Upgrade::TextureInfo& mirror_info,
   Chains::Origin origin);
void QueueDeferredChainMirror(ID3D11Device* device, ID3D11Resource* resource, const Upgrade::TextureInfo& original_info, const Upgrade::TextureInfo& mirror_info,
   Chains::Origin origin);
void TryPropagateDirectChain(ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source);
} // namespace Internal
} // namespace D3D11
} // namespace Mirrors
} // namespace Resources
} // namespace Luma
