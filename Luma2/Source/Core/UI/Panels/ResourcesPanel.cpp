#include "UI/Panels/ResourcesPanel.hpp"

#if DEVELOPMENT
#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <vector>

#include <imgui.h>

#include "Resources/ResourceDatabase.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/ResourceUpgrades.hpp"
#include "Resources/SamplerMirrors.hpp"
#include "Trace/DebugDraw.hpp"
#include "Trace/FrameCapture.hpp"
#include "UI/Icons.hpp"
#include "UI/Style.hpp"
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

size_t selected_mirror = SIZE_MAX;
bool samplers_open = true;
bool database_open = true;

std::string DescribeSize(const Resources::Upgrade::TextureInfo& desc)
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

std::string DescribeBinds(const Resources::Upgrade::TextureInfo& desc)
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

   if (!infos.empty())
   {
      std::vector<Widgets::ListEntry> list;
      list.reserve(infos.size());
      for (const Resources::Mirrors::Info& info : infos)
      {
         const std::string name = info.debug_name.empty() ? info.original : std::format("{} {}", info.original, info.debug_name);
         list.push_back({std::format("^ {} ({} -> {}) {}", name, Utils::Format::GetName(info.original_desc.format), Utils::Format::GetName(info.mirror_desc.format),
                            DescribeSize(info.original_desc)),
            Tone::Info});
      }
      // Expand into remaining viewport height when sections below are collapsed, pushing collapsed headers to the bottom
      const float header_height = ImGui::GetFrameHeightWithSpacing() + Style::Scale(Style::Sizes::UNIT * 2.f);
      float reserved_below = header_height * 2.f;
      if (samplers_open)
         reserved_below += ImGui::GetTextLineHeightWithSpacing() * 4.5f;
      if (database_open)
         reserved_below += ImGui::GetTextLineHeightWithSpacing() * 10.f;
      const float list_height = std::max(ImGui::GetTextLineHeightWithSpacing() * 8.f, ImGui::GetContentRegionAvail().y - reserved_below);
      Widgets::ListDetail(
         "MirrorsList", list, selected_mirror,
         [&](size_t index) {
            const Resources::Mirrors::Info& info = infos[index];
            const bool highlighted = Trace::Capture::GetTrackedResource() == info.original;
            const bool debugging = Trace::DebugDraw::GetTarget() == info.original;
            if (Widgets::Button(highlighted ? std::format("{} Unhighlight in Trace", Icons::STOP) : std::format("{} Highlight in Trace", Icons::TRACK),
                   highlighted ? Widgets::ButtonKind::Primary : Widgets::ButtonKind::Secondary, "Highlights draws using this texture in the Trace tab"))
               Trace::Capture::SetTrackedResource(highlighted ? std::string() : info.original);
            ImGui::SameLine();
            if (Widgets::Button(debugging ? std::format("{} Stop Debug Draw", Icons::STOP) : std::format("{} Debug Draw", Icons::DISPLAY),
                   debugging ? Widgets::ButtonKind::Primary : Widgets::ButtonKind::Secondary, "Renders a live preview of this texture through display composition"))
               debugging ? Trace::DebugDraw::Clear() : Trace::DebugDraw::SetTarget(info.original);

            std::vector<PropertyRow> detail = {
               {"Original", info.original},
               {"Original format", std::string(Utils::Format::GetName(info.original_desc.format))},
               {"Mirror format", std::string(Utils::Format::GetName(info.mirror_desc.format)), Tone::Injected},
               {"Size", DescribeSize(info.original_desc)},
               {"Binds", DescribeBinds(info.original_desc)},
               {"Last frame", std::to_string(info.frame)},
            };
            if (!info.debug_name.empty())
               detail.push_back({"Debug name", info.debug_name});
            Widgets::PropertyGrid("MirrorDetail", detail);
         },
         list_height);
   }
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
   const float table_height = std::max(ImGui::GetTextLineHeightWithSpacing() * 6.f, ImGui::GetContentRegionAvail().y - Style::Scale(Style::Sizes::UNIT * 2.f));
   Widgets::DataTable("Database", COLUMNS, table, table_height);
}
} // namespace

void DrawResources()
{
   if (Widgets::Section("Texture Mirrors"))
   {
      DrawMirrors();
      Widgets::EndSection();
   }
   samplers_open = Widgets::Section("Samplers");
   if (samplers_open)
   {
      const std::array<PropertyRow, 3> rows = {{
         {"Upgrade", Resources::Samplers::IsNeeded() ? "on (Game::Options::samplers)" : "off", Resources::Samplers::IsNeeded() ? Tone::Info : Tone::Warning},
         {"Swapped", Resources::Samplers::IsSwapping() ? "yes" : "no"},
         {"LOD bias offset", std::format("{:.2f}", Resources::Samplers::GetLodBiasOffset())},
      }};
      Widgets::PropertyGrid("Samplers", rows);
      Widgets::EndSection();
   }
   database_open = Widgets::Section("Luma's Resources");
   if (database_open)
   {
      DrawDatabase();
      Widgets::EndSection();
   }
}
} // namespace Panels
} // namespace UI
} // namespace Luma
#endif
