#include "UI/Icons.hpp"

#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <span>
#include <system_error>

#include <Windows.h>
#include <imgui.h>

#include "UI/Style.hpp"
#include "Utils/Log.hpp"
#include "Utils/Modules.hpp"
#include "Utils/Strings.hpp"

namespace Luma {
namespace UI {
namespace Fonts {
namespace {
std::span<const std::byte> GetResource(const wchar_t* name)
{
   const HMODULE module = Utils::Modules::GetSelf();
   HRSRC resource = FindResourceW(module, name, MAKEINTRESOURCEW(10)); // RT_RCDATA
   HGLOBAL loaded = resource ? LoadResource(module, resource) : nullptr;
   const void* data = loaded ? LockResource(loaded) : nullptr;
   if (!data)
      return {};
   return {static_cast<const std::byte*>(data), SizeofResource(module, resource)};
}

std::filesystem::path GetWindowsFont(const wchar_t* name)
{
   wchar_t windows[MAX_PATH] = {};
   if (!GetWindowsDirectoryW(windows, MAX_PATH))
      return {};
   return std::filesystem::path(windows) / L"Fonts" / name;
}
} // namespace

void Load()
{
   ImGuiIO& io = ImGui::GetIO();
   io.Fonts->Clear();
   ImFontConfig text_config;
   text_config.OversampleH = 2;
   std::error_code error;
   const std::filesystem::path text_font = GetWindowsFont(L"segoeui.ttf");
   // Sizes are dynamic (ImGui 1.92): the size here is only the default
   if (std::filesystem::exists(text_font, error))
      io.Fonts->AddFontFromFileTTF(Utils::Strings::ToUtf8(text_font).c_str(), Style::Sizes::DEFAULT_FONT, &text_config);
   else
      io.Fonts->AddFontDefault();

   // In the dll's resources (UI/Fonts.rc), mapped for as long as the dll is loaded: the atlas can keep pointing at them
   ImFontConfig icon_config;
   icon_config.MergeMode = true;
   icon_config.FontDataOwnedByAtlas = false;
   // Font Awesome's glyphs fill the em, a little larger than letters: scaled to sit with the text, the same width each
   icon_config.ExtraSizeScale = 0.85f;
   icon_config.GlyphMinAdvanceX = Style::Sizes::DEFAULT_FONT;
   for (const wchar_t* name : {L"FontAwesomeSolid", L"FontAwesomeBrands"})
   {
      const std::span<const std::byte> data = GetResource(name);
      if (data.empty())
      {
         Utils::Log::Error("UI: font resource {} is missing, icons won't show", Utils::Strings::ToUtf8(name));
         continue;
      }
      // ImGui only reads it, but wants a non-const pointer
      io.Fonts->AddFontFromMemoryTTF(const_cast<std::byte*>(data.data()), int(data.size()), Style::Sizes::DEFAULT_FONT, &icon_config);
   }
}
} // namespace Fonts
} // namespace UI
} // namespace Luma
