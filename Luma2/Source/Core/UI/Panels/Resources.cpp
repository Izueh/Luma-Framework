#include "UI/Panels/Resources.hpp"

#if DEVELOPMENT
#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <vector>

#include "Resources/Mirrors.hpp"
#include "Resources/ResourceDatabase.hpp"
#include "Resources/Samplers.hpp"
#include "Resources/Upgrade.hpp"
#include "UI/Widgets.hpp"
#include "Utils/Format.hpp"

namespace Luma {
namespace UI {
namespace Panels {
namespace {
using Widgets::PropertyRow;
using Widgets::TableCell;
using Widgets::TableColumn;
using Widgets::TableRow;
using Widgets::Tone;

std::string DescribeSize(const Resources::Upgrade::TextureDesc& desc)
{
   switch (desc.dimension)
   {
   case Resources::Upgrade::Dimension::Texture1D: return std::format("{} (1D)", desc.width);
   case Resources::Upgrade::Dimension::Texture3D: return std::format("{}x{}x{} (3D)", desc.width, desc.height, desc.depth_or_layers);
   default:
      return desc.depth_or_layers > 1 ? std::format("{}x{} [{}]{}", desc.width, desc.height, desc.depth_or_layers, desc.cube ? " cube" : "")
                                      : std::format("{}x{}", desc.width, desc.height);
   }
}

std::string DescribeBinds(const Resources::Upgrade::TextureDesc& desc)
{
   std::string binds;
   const auto add = [&](bool bound, std::string_view name) {
      if (bound)
         binds += std::format("{}{}", binds.empty() ? "" : " ", name);
   };
   add(desc.render_target, "RT");
   add(desc.unordered_access, "UA");
   add(desc.depth_stencil, "DS");
   return binds;
}

std::string FormatBytes(uint64_t bytes)
{
   return bytes >= (1ull << 20) ? std::format("{:.1f} MB", double(bytes) / double(1ull << 20)) : std::format("{:.1f} KB", double(bytes) / 1024.0);
}

void DrawMirrors()
{
   const Resources::Mirrors::Stats stats = Resources::Mirrors::GetStats();
   const std::array<PropertyRow, 4> rows = {{
      {"Rules", Resources::Upgrade::IsNeeded() ? std::format("{} formats", Resources::Upgrade::GetRules().formats.size()) : std::string("none"),
         Resources::Upgrade::IsNeeded() ? Tone::Info : Tone::Warning},
      {"Mirrors", std::to_string(stats.mirrors)},
      {"Mirrored views", std::to_string(stats.views)},
      {"Mirror memory", FormatBytes(stats.bytes)},
   }};
   Widgets::PropertyGrid("MirrorTotals", rows);

   std::vector<Resources::Mirrors::Info> infos = Resources::Mirrors::GetInfos();
   std::ranges::sort(infos, std::greater{}, &Resources::Mirrors::Info::frame);
   constexpr std::array<TableColumn, 6> COLUMNS = {{{"Texture", 0.f}, {"Format", 0.f}, {"Mirror", 0.f}, {"Size", 0.f}, {"Binds", 64.f}, {"Frame", 64.f}}};
   std::vector<TableRow> table;
   table.reserve(infos.size());
   for (const Resources::Mirrors::Info& info : infos)
   {
      table.push_back({
         {info.original, Tone::Info, info.debug_name},
         {std::string(Utils::Format::GetName(info.original_desc.format))},
         {std::string(Utils::Format::GetName(info.mirror_desc.format)), Tone::Injected},
         {DescribeSize(info.original_desc)},
         {DescribeBinds(info.original_desc)},
         {std::to_string(info.frame)},
      });
   }
   Widgets::DataTable("Mirrors", COLUMNS, table, 400.f);
}

void DrawDatabase()
{
   constexpr std::array<TableColumn, 4> COLUMNS = {{{"Name", 0.f}, {"Kind", 96.f}, {"Owner", 64.f}, {"File", 0.f}}};
   std::vector<TableRow> table;
   for (uint32_t index = 0; index < Resources::Database::GetCount(); index++)
   {
      const Resources::Database::Entry& entry = Resources::Database::Get(index);
      constexpr std::array<std::string_view, 3> KINDS = {"shader", "cbuffer", "texture"};
      const size_t kind = std::min<size_t>(size_t(entry.kind), KINDS.size() - 1);
      table.push_back({{entry.name}, {std::string(KINDS[kind]), Tone::Info}, {entry.owner == Resources::Database::Owner::Core ? "Core" : "Game"}, {entry.file}});
   }
   Widgets::DataTable("Database", COLUMNS, table, 300.f);
}
} // namespace

void DrawResources()
{
   if (Widgets::Section("Texture Mirrors"))
   {
      DrawMirrors();
      Widgets::EndSection();
   }
   if (Widgets::Section("Samplers"))
   {
      const std::array<PropertyRow, 3> rows = {{
         {"Upgrade", Resources::Samplers::IsNeeded() ? "on (Game::Options::samplers)" : "off", Resources::Samplers::IsNeeded() ? Tone::Info : Tone::Warning},
         {"Swapped", Resources::Samplers::IsSwapping() ? "yes" : "no"},
         {"LOD bias offset", std::format("{:.2f}", Resources::Samplers::GetLodBiasOffset())},
      }};
      Widgets::PropertyGrid("Samplers", rows);
      Widgets::EndSection();
   }
   if (Widgets::Section("Luma's Resources"))
   {
      DrawDatabase();
      Widgets::EndSection();
   }
}
} // namespace Panels
} // namespace UI
} // namespace Luma
#endif
