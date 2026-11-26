#pragma once

#include <mutex>

#include "Common/Api.hpp"
#include "Resources/ResourceDatabase.hpp"
#include "Shaders/ShaderPatching.hpp"
#include "Shaders/Shaders.hpp"
#include "Shaders/ShaderVariants.hpp"

namespace Luma {
namespace Shaders {
// What a device's work made of the shaders, built for its API and released with it. Plain data: a job holds it weakly and
// publishes only while the device lives.
struct Store
{
   explicit Store(Api device_api) : api(device_api) {}

   const Api api;
   Variants::State variants;
   Resources::Database::ShaderStates luma_shaders;
   Patching::State patching;

   // Presenting thread, and the creation before the store is shared
   bool building = false;
   bool reload_pending = false;
   // Never held with another lock
   mutable std::mutex last_build_mutex;
   BuildInfo last_build;
};
} // namespace Shaders
} // namespace Luma
