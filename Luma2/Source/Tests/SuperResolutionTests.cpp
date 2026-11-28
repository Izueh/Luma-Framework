#include <doctest/doctest.h>

#include <array>
#include <cmath>

#include <d3d11.h>
#include <wrl/client.h>

#include "Effects/SR/SuperResolution.hpp"
#include "FrameState/FrameState.hpp"
#include "Resources/ResourceDatabase.hpp"

using Microsoft::WRL::ComPtr;
using namespace Luma;

TEST_CASE("SR pure math and auto-selection")
{
   using namespace Effects::SR;

   // Halton sequence produces values in [-0.5, 0.5]
   for (uint32_t i = 0; i < 16; i++)
   {
      const float x = HaltonSequence(i, 2);
      const float y = HaltonSequence(i, 3);
      CHECK(x >= -0.5f);
      CHECK(x <= 0.5f);
      CHECK(y >= -0.5f);
      CHECK(y <= 0.5f);
   }

   // Mip LOD bias: -1 at native for DLSS/FSR, 0 at native for XeSS
   CHECK(GetMipLODBias(Type::Dlss, 1080.f, 1080.f) == doctest::Approx(-1.f));
   CHECK(GetMipLODBias(Type::Fsr3, 1080.f, 1080.f) == doctest::Approx(-1.f));
   CHECK(GetMipLODBias(Type::XeSS, 1080.f, 1080.f) == doctest::Approx(0.f));
   CHECK(GetMipLODBias(Type::Dlss, 1080.f, 2160.f) == doctest::Approx(-2.f));
   CHECK(GetMipLODBias(Type::XeSS, 1080.f, 2160.f) == doctest::Approx(-1.f));

   // Auto preference order: DLSS -> FSR 4 -> XeSS -> FSR 3
   std::array<Capabilities, TYPE_COUNT> caps = {};
   caps[size_t(Type::Fsr3)] = {.initialized = true, .supported = true, .auto_selectable = true};
   CHECK(GetAutoType(caps) == Type::Fsr3);

   caps[size_t(Type::XeSS)] = {.initialized = true, .supported = true, .auto_selectable = false};
   CHECK(GetAutoType(caps) == Type::Fsr3); // XeSS not auto-selectable on non-Intel
   CHECK(ResolveActiveType(UserType::XeSS, caps) == Type::XeSS); // Still manually selectable

   caps[size_t(Type::Fsr4)] = {.initialized = true, .supported = true, .auto_selectable = true};
   CHECK(GetAutoType(caps) == Type::Fsr4);

   caps[size_t(Type::Dlss)] = {.initialized = true, .supported = true, .auto_selectable = true};
   CHECK(GetAutoType(caps) == Type::Dlss);
   CHECK(ResolveActiveType(UserType::None, caps) == Type::None);
}

TEST_CASE("Frame camera and pass markers reset on Present")
{
   ComPtr<ID3D11Device> device;
   const HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr);
   REQUIRE(SUCCEEDED(hr));
   FrameState::CreateDeviceState(device.Get());

   FrameState::Camera cam = {.jitter_pixels = {0.25f, -0.125f}, .vert_fov = 1.0f, .near_plane = 0.2f, .far_plane = 500.f, .camera_cut = true};
   FrameState::SetCamera(device.Get(), cam);
   FrameState::MarkPass(device.Get(), FrameState::PassMarker::Taa);
   FrameState::MarkPass(device.Get(), FrameState::PassMarker::Sr);
   CHECK(FrameState::HasPass(device.Get(), FrameState::PassMarker::Taa));
   CHECK(FrameState::HasPass(device.Get(), FrameState::PassMarker::Sr));
   CHECK(FrameState::GetCamera(device.Get()).camera_cut);

   FrameState::AdvanceFrame(device.Get());
   CHECK_FALSE(FrameState::HasPass(device.Get(), FrameState::PassMarker::Taa));
   CHECK_FALSE(FrameState::HasPass(device.Get(), FrameState::PassMarker::Sr));
   CHECK_FALSE(FrameState::GetCamera(device.Get()).camera_cut);
   CHECK(FrameState::GetCamera(device.Get()).vert_fov == doctest::Approx(1.0f));
   CHECK(FrameState::GetTiming(device.Get()).frame_index == 1);
}

TEST_CASE("ResourceDatabase registers SR runtime texture defaults")
{
   Resources::Database::RegisterRuntimeDefaults();
   CHECK(Resources::Database::Find(Resources::Database::Sr::COLOR).has_value());
   CHECK(Resources::Database::Find(Resources::Database::Sr::DEPTH).has_value());
   CHECK(Resources::Database::Find(Resources::Database::Sr::MOTION_VECTORS).has_value());
   CHECK(Resources::Database::Find(Resources::Database::Sr::OUTPUT).has_value());
}
