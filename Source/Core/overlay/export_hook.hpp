#pragma once

#include "hooks/hooks.hpp"

namespace Luma {
namespace Overlay {
// A hook on an export of one of the game's dlls. `original` receives the function that calls the original.
template <typename Function, typename Detour>
Hooks::Hook MakeExportHook(const wchar_t* module, const char* name, Detour detour, Function& original)
{
   return Hooks::Hook{Hooks::ExportTarget{module, name}, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(&original)};
}
} // namespace Overlay
} // namespace Luma
