#include "core.hpp"

#include <array>
#include <string_view>

namespace {
// Same footer as the Luma 1.0 FF7R mod
constexpr std::array<std::string_view, 9> CREDITS = {
   "Main:",
   "Izueh",
   "Pumbo",
   "Acknowledgments:",
   "ShortFuse",
   "Musa",
   "Third Party:",
   "ImGui",
   "RenoDX",
};
constexpr std::array<Luma::Game::Link, 5> LINKS = {{
   {"Buy Izueh a Coffee on ko-fi", "https://ko-fi.com/izueh"},
   {"Buy Pumbo a Coffee on buymeacoffee", "https://buymeacoffee.com/realfiloppi"},
   {"Buy Pumbo a Coffee on ko-fi", "https://ko-fi.com/realpumbo"},
   // Unique link for Luma by Pumbo (to track where people join from), not to be used for anything else
   {"Join our \"HDR Den\" Discord", "https://discord.gg/J9fM3EVuEZ"},
   {"Contribute on Github", "https://github.com/Filoppi/Luma-Framework"},
}};
} // namespace

void Luma::Game::Init()
{
   SetInfo({
      .name = "Final Fantasy VII Remake",
      .version = "2.0.0-stage0",
      .credits = CREDITS,
      .links = LINKS,
   });
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
   return Luma::Core::DllMain(module, reason, reserved);
}
