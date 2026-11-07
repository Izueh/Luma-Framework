#include <doctest/doctest.h>

#include "hooks/scope.hpp"
#include "hooks/slots.h"

using namespace Luma;

// The D3D11 GetDevice hooks use one slot for children and contexts
TEST_CASE("Hooks: D3D11 contexts and children have GetDevice in the same slot")
{
   CHECK(LumaSlot_ID3D11DeviceContext_GetDevice == LumaSlot_ID3D11DeviceChild_GetDevice);
}

TEST_CASE("Hooks: the Luma scope nests per thread")
{
   CHECK_FALSE(Hooks::Scope::IsActive());
   {
      Hooks::Scope::Guard outer;
      {
         Hooks::Scope::Guard inner;
         CHECK(Hooks::Scope::IsActive());
      }
      CHECK(Hooks::Scope::IsActive());
   }
   CHECK_FALSE(Hooks::Scope::IsActive());
}
