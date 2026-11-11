#pragma once

#include <cstddef>
#include <cstring>
#include <utility>

#include <d3d11_4.h>

#include "hooks/scope.hpp"
#include "utils/depth_guard.hpp"

namespace Luma {
namespace Hooks {
namespace Detours {
// The original of each detour
template <auto Detour>
inline decltype(Detour) original = nullptr;

// The outermost of nested hooked calls, not made by Luma
inline bool IsGameCall(const Utils::DepthGuard& guard)
{
   return guard.IsOutermost() && !Scope::IsActive();
}

template <typename Callback, typename... Args>
auto Report(Callback callback, Args&&... args)
{
   Scope::Guard scope;
   return callback(std::forward<Args>(args)...);
}

template <typename Callback>
struct DestroyedObject;
template <typename T>
struct DestroyedObject<void (*)(ID3D11Device*, const T*)>
{
   using Type = T;
};

// The tracker holds the object as the IUnknown it was tracked with: COM interfaces are single inheritance, so it's the same address
template <auto Callback>
void OnDestroyed(ID3D11Device* device, const IUnknown* object)
{
   Scope::Guard scope;
   Callback(device, static_cast<const typename DestroyedObject<decltype(Callback)>::Type*>(object));
}

// Each base desc is a prefix of its versioned one: a texture's adds TextureLayout, a view's union members add PlaneSlice
template <typename Desc1, typename Desc>
Desc1 ToDesc1(const Desc& desc)
{
   static_assert(sizeof(Desc) <= sizeof(Desc1));
   Desc1 desc1 = {};
   std::memcpy(&desc1, &desc, sizeof(Desc));
   return desc1;
}

template <typename Desc, typename Desc1>
Desc ToDesc(const Desc1& desc1)
{
   static_assert(sizeof(Desc) <= sizeof(Desc1));
   Desc desc;
   std::memcpy(&desc, &desc1, sizeof(Desc));
   return desc;
}

static_assert(offsetof(D3D11_TEXTURE2D_DESC1, TextureLayout) == sizeof(D3D11_TEXTURE2D_DESC));
static_assert(offsetof(D3D11_TEXTURE3D_DESC1, TextureLayout) == sizeof(D3D11_TEXTURE3D_DESC));
static_assert(offsetof(D3D11_TEX2D_SRV1, PlaneSlice) == sizeof(D3D11_TEX2D_SRV));
static_assert(offsetof(D3D11_TEX2D_ARRAY_SRV1, PlaneSlice) == sizeof(D3D11_TEX2D_ARRAY_SRV));
static_assert(offsetof(D3D11_TEX2D_RTV1, PlaneSlice) == sizeof(D3D11_TEX2D_RTV));
static_assert(offsetof(D3D11_TEX2D_ARRAY_RTV1, PlaneSlice) == sizeof(D3D11_TEX2D_ARRAY_RTV));
static_assert(offsetof(D3D11_TEX2D_UAV1, PlaneSlice) == sizeof(D3D11_TEX2D_UAV));
static_assert(offsetof(D3D11_TEX2D_ARRAY_UAV1, PlaneSlice) == sizeof(D3D11_TEX2D_ARRAY_UAV));
static_assert(offsetof(D3D11_SHADER_RESOURCE_VIEW_DESC1, Buffer) == offsetof(D3D11_SHADER_RESOURCE_VIEW_DESC, Buffer));
static_assert(offsetof(D3D11_RENDER_TARGET_VIEW_DESC1, Buffer) == offsetof(D3D11_RENDER_TARGET_VIEW_DESC, Buffer));
static_assert(offsetof(D3D11_UNORDERED_ACCESS_VIEW_DESC1, Buffer) == offsetof(D3D11_UNORDERED_ACCESS_VIEW_DESC, Buffer));
} // namespace Detours
} // namespace Hooks
} // namespace Luma
