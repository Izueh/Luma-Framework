#include "UI/Panels/TracePanel.hpp"

#if DEVELOPMENT
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include "Notifications/Notifications.hpp"
#include "Resources/ResourceChains.hpp"
#include "Shaders/ShaderCompiler.hpp"
#include "Shaders/ShaderDatabase.hpp"
#include "Shaders/ShaderDecompiler.hpp"
#include "Shaders/ShaderVariants.hpp"
#include "Shaders/ShaderStore.hpp"
#include "Trace/CbufferTracker.hpp"
#include "Trace/Cbuffers.hpp"
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
using Trace::Capture::Event;
using Trace::Capture::EventType;
using Trace::Capture::Outcome;
using Trace::Capture::ResourceUsage;
using Widgets::PropertyRow;
using Widgets::TableCell;
using Widgets::TableColumn;
using Widgets::TableRow;
using Widgets::Tone;

// The presenting device's variant of a shader, as the trace shows the frame it captured
std::shared_ptr<const Shaders::CompiledShader> GetShownCompiledShader(uint32_t hash, Shaders::Variants::Source source)
{
   const std::shared_ptr<Shaders::Store> store = Shaders::GetShownStore();
   return store ? Shaders::Variants::GetCompiledShader(*store, hash, source) : nullptr;
}

// Words that changed this many frames ago or less are highlighted in a tracked cbuffer
constexpr uint64_t RECENT_CHANGE_FRAMES = 60;

// The frame the list was built from
std::shared_ptr<const Trace::Capture::Frame> frame;
std::vector<Widgets::ListEntry> entries;
size_t selected = SIZE_MAX;
bool requested = false;

// The list's filters and options
Trace::Capture::Filter filter;
bool unique_only = false; // Only the first call of each primary shader
bool highlighted_only = false; // Only events accessing the tracked resource
bool show_thread = false; // Shows the command list and thread of each call
int32_t stage_filter = 0; // 0 = All, 1..6 = VS..CS
std::string last_tracked_resource;
// The events `unique_only` keeps, for the frame
std::vector<bool> first_of_shader;
// Expanded constant buffer drawer per stage (nullopt = collapsed by default)
std::array<std::optional<uint32_t>, Shaders::STAGE_COUNT> open_cb_slots = {};
// Show full raw float4 buffer instead of stopping at shader reflection
bool cb_full_raw = false;

// Shader inspector caches (disassembly & HLSL source)
int32_t inspector_stage = 0;
int32_t disasm_source = 0; // 0 Original, 1 Patched, 2 Replacement
int32_t hlsl_source = 0; // 0 Original (decompiled), 1 Patched (decompiled), 2 Replacement (file)
uint32_t last_hlsl_shader_hash = 0;
bool hlsl_inline_includes = false;
std::string cached_disasm_key;
std::string cached_disasm_text;
std::string cached_hlsl_key;
std::string cached_hlsl_text;

std::optional<Trace::DebugDraw::PassTarget> MakeBasePassTarget(const Event& event, size_t index)
{
   if (!frame)
      return std::nullopt;
   if (event.type == EventType::LumaPass)
   {
      uint32_t instance = 0;
      for (size_t i = 0; i < index && i < frame->events.size(); i++)
      {
         if (frame->events[i].type == EventType::LumaPass && frame->events[i].name == event.name)
            instance++;
      }
      return Trace::DebugDraw::PassTarget{.stage = ShaderStage::Pixel, .luma_pass = event.name, .instance = instance};
   }
   if (event.type == EventType::Draw || event.type == EventType::Dispatch)
   {
      const Trace::Capture::Shader* shader = Trace::Capture::GetPrimaryShader(event);
      if (!shader || shader->hash == 0 || !shader->luma_shader.empty())
         return std::nullopt;
      const uint32_t instance = Trace::Capture::GetInstance(*frame, index, shader->stage, shader->hash);
      return Trace::DebugDraw::PassTarget{.hash = shader->hash, .stage = shader->stage, .instance = instance};
   }
   return std::nullopt;
}

bool SelectPassDebugDraw(const Event& event, size_t index, bool keep_mode)
{
   std::optional<Trace::DebugDraw::PassTarget> target = MakeBasePassTarget(event, index);
   if (!target)
      return false;

   const auto has_slot = [&](Trace::DebugDraw::ViewMode mode, uint32_t slot, uint32_t& out_mip) -> bool {
      switch (mode)
      {
      case Trace::DebugDraw::ViewMode::RenderTarget:
         for (const Trace::Capture::View& v : event.render_targets)
         {
            if (v.slot == slot && v.resource.type != "Buffer")
            {
               out_mip = v.mip;
               return true;
            }
         }
         return false;
      case Trace::DebugDraw::ViewMode::ShaderResource:
         for (const Trace::Capture::View& v : event.shader_resources)
         {
            if (v.slot == slot && v.resource.type != "Buffer")
            {
               out_mip = v.mip;
               return true;
            }
         }
         return false;
      case Trace::DebugDraw::ViewMode::UnorderedAccess:
         for (const Trace::Capture::View& v : event.unordered_access)
         {
            if (v.slot == slot && v.resource.type != "Buffer")
            {
               out_mip = v.mip;
               return true;
            }
         }
         return false;
      case Trace::DebugDraw::ViewMode::Depth:
         out_mip = 0;
         return event.depth_stencil_view.has_value();
      default: return false;
      }
   };

   const Trace::DebugDraw::ViewMode prev_mode = Trace::DebugDraw::GetLastViewMode();
   const uint32_t prev_slot = Trace::DebugDraw::GetLastViewSlot();
   Trace::DebugDraw::ViewMode mode = prev_mode;
   uint32_t slot = prev_slot;
   uint32_t mip = 0;

   if (!keep_mode || mode == Trace::DebugDraw::ViewMode::Resource)
   {
      mode = target->stage == ShaderStage::Pixel ? Trace::DebugDraw::ViewMode::RenderTarget
         : target->stage == ShaderStage::Compute ? Trace::DebugDraw::ViewMode::UnorderedAccess
                                                 : Trace::DebugDraw::ViewMode::ShaderResource;
      slot = 0;
   }

   if (!has_slot(mode, slot, mip))
   {
      slot = 0;
      if (!has_slot(mode, slot, mip))
      {
         // Fall back across available views like 1.0 (RTV -> DSV -> UAV -> SRV)
         if (!event.render_targets.empty() && event.render_targets.front().resource.type != "Buffer")
         {
            mode = Trace::DebugDraw::ViewMode::RenderTarget;
            slot = event.render_targets.front().slot;
            mip = event.render_targets.front().mip;
         }
         else if (event.depth_stencil_view)
         {
            mode = Trace::DebugDraw::ViewMode::Depth;
            slot = 0;
            mip = 0;
         }
         else if (!event.unordered_access.empty() && event.unordered_access.front().resource.type != "Buffer")
         {
            mode = Trace::DebugDraw::ViewMode::UnorderedAccess;
            slot = event.unordered_access.front().slot;
            mip = event.unordered_access.front().mip;
         }
         else
         {
            bool found = false;
            for (const Trace::Capture::View& v : event.shader_resources)
            {
               if (v.resource.type != "Buffer")
               {
                  mode = Trace::DebugDraw::ViewMode::ShaderResource;
                  slot = v.slot;
                  mip = v.mip;
                  found = true;
                  break;
               }
            }
            if (!found)
               return false;
         }
      }
   }

   target->mode = mode;
   target->slot = slot;
   target->mip = mip;
   Trace::DebugDraw::SetPassTarget(std::move(*target));
   return true;
}

std::array<float, 4> ToColorArray(ImVec4 color)
{
   return {color.x, color.y, color.z, color.w};
}

std::string_view GetStageName(ShaderStage stage)
{
   constexpr std::string_view NAMES[] = {"VS", "HS", "DS", "GS", "PS", "CS"};
   return NAMES[size_t(stage)];
}

std::string_view GetStageTitle(ShaderStage stage)
{
   constexpr std::string_view NAMES[] = {"Vertex", "Hull", "Domain", "Geometry", "Pixel", "Compute"};
   return NAMES[size_t(stage)];
}

std::string_view GetOutcomeName(Outcome outcome)
{
   constexpr std::string_view NAMES[] = {"ran", "skipped by a draw callback", "replaced by a draw callback", "skipped (trace action)"};
   return NAMES[size_t(outcome)];
}

std::string GetTopologyName(uint32_t topology)
{
   constexpr std::array<std::string_view, 6> NAMES = {"undefined", "point list", "line list", "line strip", "triangle list", "triangle strip"};
   if (topology < NAMES.size())
      return std::string(NAMES[topology]);
   if (topology >= 33 && topology <= 64)
      return std::format("{} control point patch list", topology - 32);
   return std::format("{}", topology);
}

std::string FormatResourceSummary(const Trace::Capture::Resource& resource)
{
   const std::string_view mirror_mark = resource.mirror ? "^" : "";
   if (resource.type == "Buffer")
   {
      if (resource.size[0] >= 1024)
         return std::format("{}Buffer {:.1f}KB", mirror_mark, double(resource.size[0]) / 1024.0);
      return std::format("{}Buffer {}B", mirror_mark, resource.size[0]);
   }
   if (resource.size[2] > 1)
      return std::format("{}{} {}x{}x{}", mirror_mark, Utils::Format::GetName(resource.format), resource.size[0], resource.size[1], resource.size[2]);
   return std::format("{}{} {}x{}", mirror_mark, Utils::Format::GetName(resource.format), resource.size[0], resource.size[1]);
}

Widgets::ListEntry Describe(const Event& event, size_t index, std::string_view tracked_resource)
{
   const std::string thread = show_thread ? std::format(" [{}{}]", event.thread, event.deferred ? " deferred" : "") : std::string();
   const ResourceUsage usage = Trace::Capture::GetResourceUsage(event, tracked_resource);
   const auto apply_highlight = [&](ImVec4 base_color) -> std::array<float, 4> {
      if (usage == ResourceUsage::ReadWrite)
         return ToColorArray(Style::Colors::Trace::HIGHLIGHT_RW_BOTH);
      if (usage == ResourceUsage::Read || usage == ResourceUsage::Write)
         return ToColorArray(Style::Colors::Trace::HIGHLIGHT_RW_SINGLE);
      return ToColorArray(base_color);
   };
   const std::string_view usage_tag = usage == ResourceUsage::ReadWrite ? " [RW]" : usage == ResourceUsage::Write ? " [W]"
      : usage == ResourceUsage::Read                                                                              ? " [R]"
                                                                                                                  : "";

   if (event.type == EventType::Present)
      return {std::format("{:>5} Present", index), Tone::Info, ToColorArray(Style::Colors::Trace::PRESENT)};
   if (event.type == EventType::ThirdPartyWork)
   {
      if (const Trace::Capture::Shader* shader = Trace::Capture::GetPrimaryShader(event))
         return {std::format("{:>5} {} 0x{:08X}{}{} [{}]{}", index, GetStageName(shader->stage), shader->hash, shader->name.empty() ? "" : " ", shader->name, event.name, thread),
            Tone::Info, ToColorArray(Style::Colors::TEXT_DIM)};
      return {std::format("{:>5} {}{}", index, event.name, thread), Tone::Info, ToColorArray(Style::Colors::TEXT_DIM)};
   }
   if (event.type == EventType::LumaPass)
   {
      if (event.sr)
      {
         const Trace::Capture::SrInfo& sr = *event.sr;
         const bool native = sr.render_width == sr.output_width && sr.render_height == sr.output_height;
         std::string label = std::format("{:>5} {} [{}]", index, event.name,
            native ? std::format("Native {}x{}", sr.output_width, sr.output_height)
                   : std::format("{}x{} -> {}x{}", sr.render_width, sr.render_height, sr.output_width, sr.output_height));
         if (sr.dynamic_resolution)
            label += " [DRS]";
         if (sr.reset)
            label += " [Reset]";
         if (!sr.succeeded)
            label += " [Failed]";
         label += usage_tag;
         label += thread;
         return {std::move(label), sr.succeeded ? Tone::Injected : Tone::Error,
            apply_highlight(sr.succeeded ? Style::Colors::Trace::LUMA_PASS : Style::Colors::DANGER)};
      }
      return {std::format("{:>5} {} (Luma){}{}", index, event.name, usage_tag, thread), Tone::Injected, apply_highlight(Style::Colors::Trace::LUMA_PASS)};
   }

   // Copy, clear, CPU write or CPU read
   if (Trace::Capture::IsResourceEvent(event))
   {
      std::string op_name = event.name;
      if (event.cpu_access && !event.cpu_access->map_type.empty())
         op_name = std::format("{} ({})", event.name, event.cpu_access->map_type);
      const std::string target = event.target ? std::format(" {}", FormatResourceSummary(*event.target)) : std::string();
      return {std::format("{:>5} {}{}{}{}", index, op_name, target, usage_tag, thread), Tone::Info, apply_highlight(Style::Colors::Trace::RESOURCE_OP)};
   }

   // Labelled by its primary shader; the other stages are in its detail and match the filters too
   const Trace::Capture::Shader* shader = Trace::Capture::GetPrimaryShader(event);
   bool any_mirror = std::ranges::any_of(event.render_targets, [](const Trace::Capture::View& view) { return view.resource.mirror.has_value(); });
   if (!any_mirror)
      any_mirror = std::ranges::any_of(event.unordered_access, [](const Trace::Capture::View& view) { return view.resource.mirror.has_value(); });
   std::string label = std::format("{:>5} {}", index, any_mirror ? "^" : "");
   if (shader)
   {
      // Live, so a rename shows without a new capture (a Luma pass's shader has no hash to look up)
      const std::string name = shader->hash != 0 ? Shaders::Database::GetDisplayName(shader->hash) : shader->name;
      label += std::format("{} 0x{:08X}{}{}", GetStageName(shader->stage), shader->hash, name.empty() ? "" : " ", name);
   }
   else
      label += event.name;
   if (shader && shader->variant)
      label += std::format(" [{}]", Shaders::Variants::GetLabel(Shaders::Variants::GetChoice(*shader->variant)));
   if (Trace::Capture::IsScaledEvent(event))
      label += " [Scaled]";
   if (event.chain_seed)
      label += " [Seed]";
   if (event.cross_thread_scaling_overlap)
      label += " [Cross-Thread!]";
   label += usage_tag;
   label += thread;

   ImVec4 base = Style::Colors::Trace::PIXEL;
   if (event.outcome != Outcome::Ran)
      base = Style::Colors::Trace::CANCELLED;
   else if (shader && shader->variant)
      base = Style::Colors::Trace::REPLACED;
   else if (shader)
   {
      if (shader->stage == ShaderStage::Vertex)
         base = Style::Colors::Trace::VERTEX;
      else if (shader->stage == ShaderStage::Compute)
         base = Style::Colors::Trace::COMPUTE;
   }
   return {std::move(label), Tone::Info, apply_highlight(base)};
}

void Rebuild()
{
   entries.clear();
   first_of_shader.clear();
   last_tracked_resource = Trace::Capture::GetTrackedResource();
   std::unordered_set<uint64_t> seen;
   for (size_t i = 0; frame && i < frame->events.size(); i++)
   {
      const Event& event = frame->events[i];
      entries.push_back(Describe(event, i, last_tracked_resource));
      const Trace::Capture::Shader* shader = Trace::Capture::GetPrimaryShader(event);
      const uint64_t key = shader ? (uint64_t(shader->stage) << 32 | shader->hash) : UINT64_MAX;
      first_of_shader.push_back(!shader || seen.insert(key).second);
   }
}

bool IsListed(size_t index)
{
   const Event& event = frame->events[index];
   if (highlighted_only && !last_tracked_resource.empty() && Trace::Capture::GetResourceUsage(event, last_tracked_resource) == ResourceUsage::None)
      return false;
   return Trace::Capture::Matches(event, filter) && (!unique_only || first_of_shader[index]);
}

std::string DescribeResource(const Trace::Capture::Resource& resource)
{
   std::string text;
   if (resource.type == "Buffer")
      text = std::format("{} {}", FormatResourceSummary(resource), resource.id);
   else
      text = std::format("{}{} {} {}", resource.mirror ? "^" : "", resource.type, Utils::Format::GetName(resource.format), resource.id);
   if (!resource.debug_name.empty())
      text += std::format(" \"{}\"", resource.debug_name);
   return text;
}

std::string GetSize(const Trace::Capture::View& view)
{
   if (view.resource.type == "Buffer")
      return std::format("{}B", view.resource.size[0]);
   return view.size[2] > 1 ? std::format("{}x{}x{}", view.size[0], view.size[1], view.size[2]) : std::format("{}x{}", view.size[0], view.size[1]);
}

// "RGBA", "RG" or "none"
std::string GetWriteMask(uint8_t mask)
{
   std::string text;
   for (uint32_t i = 0; i < 4; i++)
   {
      if (mask & (1u << i))
         text += "RGBA"[i];
   }
   return text.empty() ? std::string("none") : text;
}

// "SRV3", "SRV3 space1" (D3D12)
std::string FormatRegister(std::string_view prefix, uint32_t slot, uint32_t space)
{
   return space == 0 ? std::format("{}{}", prefix, slot) : std::format("{}{} space{}", prefix, slot, space);
}

std::string FormatByteSize(uint32_t bytes)
{
   if (bytes >= 1024)
      return std::format("{:.1f}KB", double(bytes) / 1024.0);
   return std::format("{}B", bytes);
}

// Dimmed label + crisp value pair that wraps cleanly as a unit inside a Resource Card
struct CardField
{
   std::string_view label;
   std::string value;
   Tone tone = Tone::Info;
};

void DrawResourceCard(std::string_view badge, std::span<const CardField> fields, const Trace::Capture::Resource& resource, bool allow_debug_draw,
   const std::optional<Trace::DebugDraw::PassTarget>& pass_target = std::nullopt)
{
   const float unit = Style::Scale(Style::Sizes::UNIT);
   const ImGuiStyle& style = ImGui::GetStyle();
   const float rounding = Style::Scale(Style::Sizes::CONTROL_ROUNDING);
   const float avail_width = ImGui::GetContentRegionAvail().x;
   ImDrawList* draw_list = ImGui::GetWindowDrawList();

   ImGui::PushID(badge.data(), badge.data() + badge.size());
   ImGui::PushID(resource.id.c_str());

   // Background rendered in channel 0 behind the card contents in channel 1
   draw_list->ChannelsSplit(2);
   draw_list->ChannelsSetCurrent(1);
   const ImVec2 card_start = ImGui::GetCursorScreenPos();
   ImGui::SetCursorScreenPos(ImVec2(card_start.x + unit * 2.f, card_start.y + unit * 1.5f));
   ImGui::BeginGroup();

   const bool highlighted = Trace::Capture::GetTrackedResource() == resource.id;
   const bool debugging = allow_debug_draw && (pass_target ? Trace::DebugDraw::IsPassTarget(*pass_target) : Trace::DebugDraw::GetTarget() == resource.id);
   const std::string hl_label = highlighted ? std::format("{} Unhighlight", Icons::STOP) : std::format("{} Highlight", Icons::TRACK);
   const std::string dd_label = debugging ? std::format("{} Stop", Icons::STOP) : std::format("{} Draw", Icons::DISPLAY);
   const bool can_debug_draw = allow_debug_draw && resource.type != "Buffer";

   // Top row: slot badge + right-aligned inline Highlight / Debug Draw buttons
   const float hl_w = ImGui::CalcTextSize(hl_label.c_str()).x + style.FramePadding.x * 2.f;
   const float dd_w = can_debug_draw ? ImGui::CalcTextSize(dd_label.c_str()).x + style.FramePadding.x * 2.f + style.ItemSpacing.x : 0.f;
   const float actions_w = hl_w + dd_w;
   const float inner_width = std::max(avail_width - unit * 4.f, Style::Scale(120.f));

   Widgets::Tag(badge, resource.mirror ? Tone::Injected : Tone::Success);
   float line_used = ImGui::GetItemRectSize().x;
   bool first_line = true;
   const float first_line_limit = std::max(inner_width - actions_w - unit * 3.f, Style::Scale(60.f));

   // Draw fields inline next to the badge, wrapping onto new lines cleanly without entering the top-right button area
   for (const CardField& field : fields)
   {
      if (field.value.empty())
         continue;
      const float label_w = ImGui::CalcTextSize(field.label.data(), field.label.data() + field.label.size()).x;
      ImGui::PushFont(Fonts::GetMonospace(), 0.f);
      const float val_w = ImGui::CalcTextSize(field.value.c_str()).x;
      ImGui::PopFont();
      const float field_w = label_w + style.ItemInnerSpacing.x + val_w;
      const float max_row = first_line ? first_line_limit : inner_width;
      if (line_used > 0.f && line_used + unit * 2.5f + field_w <= max_row)
      {
         ImGui::SameLine(0.f, unit * 2.5f);
         line_used += unit * 2.5f + field_w;
      }
      else
      {
         first_line = false;
         line_used = field_w;
      }
      ImGui::AlignTextToFramePadding();
      Widgets::TextDim(field.label);
      ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
      ImGui::PushFont(Fonts::GetMonospace(), 0.f);
      Widgets::Text(field.value, field.tone);
      ImGui::PopFont();
   }

   // Action buttons: place on the top-right if they fit without overlapping, otherwise on their own line
   const ImVec2 after_fields = ImGui::GetCursorScreenPos();
   if (inner_width > actions_w + Style::Scale(80.f))
   {
      ImGui::SetCursorScreenPos(ImVec2(card_start.x + avail_width - unit * 2.f - actions_w, card_start.y + unit * 1.5f));
   }
   ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x * 0.85f, style.FramePadding.y * 0.65f));
   if (Widgets::Button(hl_label, highlighted ? Widgets::ButtonKind::Primary : Widgets::ButtonKind::Secondary,
          "Highlights all trace events reading or writing this resource"))
      Trace::Capture::SetTrackedResource(highlighted ? std::string() : resource.id);
   if (can_debug_draw)
   {
      ImGui::SameLine();
      if (Widgets::Button(dd_label, debugging ? Widgets::ButtonKind::Primary : Widgets::ButtonKind::Secondary,
             "Renders this texture live in the display composition pass"))
      {
         if (debugging)
            Trace::DebugDraw::Clear();
         else if (pass_target)
            Trace::DebugDraw::SetPassTarget(*pass_target);
         else
            Trace::DebugDraw::SetTarget(resource.id);
      }
   }
   ImGui::PopStyleVar();
   const float actions_bottom = ImGui::GetItemRectMax().y;
   if (after_fields.y > actions_bottom)
      ImGui::SetCursorScreenPos(ImVec2(card_start.x + unit * 2.f, after_fields.y));
   else
      ImGui::SetCursorScreenPos(ImVec2(card_start.x + unit * 2.f, actions_bottom + style.ItemSpacing.y * 0.5f));

   // Bottom line: Resource: <type> <format> <id> "<debug_name>" (click ID to copy)
   Widgets::TextDim("Resource:");
   ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
   const std::string res_desc = DescribeResource(resource);
   const float res_max_width = std::max(unit * 8.f, card_start.x + avail_width - unit * 2.f - ImGui::GetCursorScreenPos().x);
   ImGui::PushFont(Fonts::GetMonospace(), 0.f);
   Widgets::TextEllipsis(res_desc, res_max_width, resource.mirror ? Tone::Injected : Tone::Info,
      std::format("Click to copy resource address ({})", resource.id));
   if (ImGui::IsItemClicked())
      ImGui::SetClipboardText(resource.id.c_str());
   ImGui::PopFont();

   // Explicit mirror / scaling / chain origin line when upgraded or pending
   if (resource.mirror_info)
   {
      const Trace::Capture::MirrorInfo& mi = *resource.mirror_info;
      ImGui::AlignTextToFramePadding();
      Widgets::TextDim("Mirror:");
      ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
      if (mi.pending)
      {
         Widgets::Tag("Pending (next frame)", Tone::Warning, "Discovered on a deferred context or D3D12 command list; activates at next present");
      }
      else
      {
         const std::string mirror_text = mi.is_scaled
            ? std::format("{} {}x{} ({:.2f}x{:.2f})", Utils::Format::GetName(mi.format), mi.size[0], mi.size[1], mi.scale[0], mi.scale[1])
            : std::format("{} {}x{}", Utils::Format::GetName(mi.format), mi.size[0], mi.size[1]);
         ImGui::PushFont(Fonts::GetMonospace(), 0.f);
         Widgets::Text(mirror_text, Tone::Injected);
         ImGui::PopFont();
         ImGui::SameLine(0.f, unit * 1.5f);
         const std::string origin_tag = mi.origin.shader_hash != 0
            ? std::format("{} 0x{:08X}", Resources::Chains::GetOriginName(mi.origin.kind), mi.origin.shader_hash)
            : std::string(Resources::Chains::GetOriginName(mi.origin.kind));
         Widgets::Tag(origin_tag, mi.is_scaled ? Tone::Success : Tone::Injected,
            mi.origin.shader_hash != 0 ? "Click to copy seed shader hash" : "How this mirror was created");
         if (mi.origin.shader_hash != 0 && ImGui::IsItemClicked())
            ImGui::SetClipboardText(std::format("0x{:08X}", mi.origin.shader_hash).c_str());
      }
   }

   ImGui::EndGroup();
   const float card_bottom = ImGui::GetItemRectMax().y + unit * 1.5f;

   draw_list->ChannelsSetCurrent(0);
   const ImVec2 card_max(card_start.x + avail_width, card_bottom);
   const ImVec4 bg = highlighted ? Style::WithAlpha(Style::Colors::ACCENT, 0.10f) : Style::WithAlpha(Style::Colors::PANEL, 0.65f);
   const ImVec4 border = highlighted ? Style::WithAlpha(Style::Colors::ACCENT, 0.6f) : Style::Colors::BORDER;
   draw_list->AddRectFilled(card_start, card_max, ImGui::GetColorU32(bg), rounding);
   draw_list->AddRect(card_start, card_max, ImGui::GetColorU32(border), rounding);
   draw_list->ChannelsMerge();

   ImGui::SetCursorScreenPos(card_start);
   ImGui::Dummy(ImVec2(avail_width, card_bottom - card_start.y + unit * 0.5f));

   ImGui::PopID();
   ImGui::PopID();
}

void DrawViews(const Event& event, size_t index, std::string_view slot_prefix, std::span<const Trace::Capture::View> views, Trace::DebugDraw::ViewMode mode)
{
   const std::optional<Trace::DebugDraw::PassTarget> base = MakeBasePassTarget(event, index);
   for (const Trace::Capture::View& view : views)
   {
      const std::string badge = view.role.empty() ? FormatRegister(slot_prefix, view.slot, view.space) : view.role;
      const std::array<CardField, 3> fields = {{
         {"Format:", std::string(Utils::Format::GetName(view.format)), view.resource.mirror ? Tone::Injected : Tone::Info},
         {"Size:", GetSize(view)},
         {"Mip:", std::to_string(view.mip)},
      }};
      std::optional<Trace::DebugDraw::PassTarget> target = base;
      if (target)
      {
         target->mode = mode;
         target->slot = view.slot;
         target->mip = view.mip;
      }
      DrawResourceCard(badge, fields, view.resource, true, target);
   }
}

void DrawTargets(const Event& event, size_t index)
{
   const std::optional<Trace::DebugDraw::PassTarget> base = MakeBasePassTarget(event, index);
   for (const Trace::Capture::View& view : event.render_targets)
   {
      const std::string badge = view.role.empty() ? std::format("RTV{}", view.slot) : view.role;
      const std::array<CardField, 4> fields = {{
         {"Format:", std::string(Utils::Format::GetName(view.format)), view.resource.mirror ? Tone::Injected : Tone::Info},
         {"Size:", GetSize(view)},
         {"Write:", GetWriteMask(view.write_mask)},
         {"Blend:", view.blend.empty() ? std::string("off") : view.blend},
      }};
      std::optional<Trace::DebugDraw::PassTarget> target = base;
      if (target)
      {
         target->mode = Trace::DebugDraw::ViewMode::RenderTarget;
         target->slot = view.slot;
         target->mip = view.mip;
      }
      DrawResourceCard(badge, fields, view.resource, true, target);
   }
   if (event.depth_stencil_view)
   {
      const Trace::Capture::View& view = *event.depth_stencil_view;
      const Trace::Capture::DepthStencil& depth = event.depth_stencil;
      const std::string state = depth.depth_enable ? std::format("{}{}", depth.depth_func, depth.depth_write ? ", writes" : ", read only") : std::string("off");
      const std::array<CardField, 3> fields = {{
         {"Format:", std::string(Utils::Format::GetName(view.format)), view.resource.mirror ? Tone::Injected : Tone::Info},
         {"Size:", GetSize(view)},
         {"Depth:", state},
      }};
      std::optional<Trace::DebugDraw::PassTarget> target = base;
      if (target)
      {
         target->mode = Trace::DebugDraw::ViewMode::Depth;
         target->slot = 0;
         target->mip = 0;
      }
      DrawResourceCard("DSV", fields, view.resource, true, target);
   }
}

// The variant picker: what the hash's binds take from now on, until set back to Auto
void DrawVariantPicker(const Trace::Capture::Shader& shader, Shaders::Database::Shader& live)
{
   const std::vector<Shaders::Variants::Choice> choices = Shaders::Variants::GetChoices(shader.variants);
   std::vector<std::string_view> labels;
   for (const Shaders::Variants::Choice choice : choices)
      labels.push_back(Shaders::Variants::GetLabel(choice));
   int32_t index = int32_t(Shaders::Variants::FindChoice(choices, live.forced_variant.load()));
   if (labels.size() > Widgets::MAX_SEGMENTS)
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.f);
   const bool changed = labels.size() <= Widgets::MAX_SEGMENTS ? Widgets::Segmented("Variant", index, labels) : Widgets::Combo("Variant", index, labels);
   if (changed)
   {
      live.forced_variant = choices[size_t(index)];
      Shaders::Variants::MarkChoiceChanged();
   }
   ImGui::SetItemTooltip("What this shader's binds take from now on: Auto is the highest-priority variant, Original the game's shader");
}

void DrawShader(const Trace::Capture::Shader& shader)
{
   ImGui::PushID(int(shader.stage));
   Widgets::RowLabel(std::format("{} shader", GetStageTitle(shader.stage)));
   if (!shader.luma_shader.empty())
   {
      Widgets::Text(std::format("Luma's \"{}\"", shader.luma_shader), Tone::Injected);
      ImGui::PopID();
      return;
   }
   const std::string display_name = Shaders::Database::GetDisplayName(shader.hash);
   Widgets::HashChip(shader.hash, shader.stage, display_name);
   if (!display_name.empty())
   {
      ImGui::SameLine();
      Widgets::TextDim(display_name);
   }
   ImGui::SameLine();
   if (shader.variant)
      Widgets::Tag(Shaders::Variants::GetLabel(Shaders::Variants::GetChoice(*shader.variant)), Tone::Success, "Its variant ran in this call");
   else if (!shader.variants.empty())
      Widgets::Tag("Original", Tone::Warning, "It has a variant, but the game's shader ran in this call (forced, or the variant wasn't ready)");
   else
      Widgets::Tag("Game", Tone::Info, "No variant: the game's shader");
   Shaders::Database::Shader* live = Shaders::Database::Find(shader.hash);
   if (live)
   {
      Widgets::RowLabel("Custom name");
      std::string custom_name = display_name;
      if (Widgets::TextInput("ShaderCustomName", custom_name))
         Shaders::Database::SetCustomName(shader.hash, std::move(custom_name));
      ImGui::SetItemTooltip("Names this shader in the trace until the game restarts. Empty goes back to the game's name.");
      Widgets::RowLabel("");
      if (!shader.variants.empty())
      {
         DrawVariantPicker(shader, *live);
         ImGui::SameLine();
      }
      if (Widgets::Button("Dump", Widgets::ButtonKind::Secondary, "Writes its bytecode to Luma/Dump"))
      {
         const std::filesystem::path file = Shaders::Database::Dump(*live);
         Notifications::Post(file.empty() ? Notifications::Kind::Error : Notifications::Kind::Success,
            file.empty() ? "The shader couldn't be dumped" : std::format("Dumped {}", file.filename().string()));
      }
      ImGui::SameLine();
      const bool supports_purple = shader.stage == ShaderStage::Pixel || shader.stage == ShaderStage::Compute;
      int32_t action = live->skip_draws ? 1 : (supports_purple && live->draw_purple ? 2 : 0);
      constexpr std::array<std::string_view, 3> ACTIONS_FULL = {"Normal", "Skip", "Purple"};
      constexpr std::array<std::string_view, 2> ACTIONS_BASIC = {"Normal", "Skip"};
      const std::span<const std::string_view> choices = supports_purple ? std::span<const std::string_view>(ACTIONS_FULL) : std::span<const std::string_view>(ACTIONS_BASIC);
      if (Widgets::Segmented("ShaderAction", action, choices))
      {
         live->skip_draws = (action == 1);
         live->draw_purple = (action == 2);
      }
      ImGui::SetItemTooltip("Normal: run shader | Skip: skip its draws | Purple: draw magenta debug shader");
   }
   ImGui::PopID();
}

// The cbuffer's contents as the shader sees them (a D3D11.1 range from its first constant, 16 bytes each)
std::span<const std::byte> GetBoundContents(const Trace::Capture::ConstantBuffer& buffer)
{
   std::span<const std::byte> contents = buffer.contents;
   if (buffer.constant_count && !buffer.contents_ranged)
   {
      const size_t first = std::min<size_t>(size_t(buffer.first_constant) * 16, contents.size());
      contents = contents.subspan(first, std::min<size_t>(size_t(buffer.constant_count) * 16, contents.size() - first));
   }
   return contents;
}

bool ChangedRecently(const Trace::Track::Reading* reading, uint32_t offset, uint32_t size)
{
   if (!reading)
      return false;
   for (uint32_t word = offset / 4; word < (offset + std::max(size, 1u) + 3) / 4 && word < reading->changed.size(); word++)
   {
      if (reading->changed[word] && reading->frame - reading->changed[word] <= RECENT_CHANGE_FRAMES)
         return true;
   }
   return false;
}

std::string FormatFloat(float value)
{
   if (std::isnan(value))
      return "NaN";
   if (std::isinf(value))
      return value > 0.f ? "Inf" : "-Inf";
   return std::format("{:.7g}", value);
}

struct ContentsTable
{
   const std::byte* data = nullptr;
   size_t size = 0;
   uint32_t hash = 0;
   uint32_t slot = 0;
   uint64_t reading_frame = 0;
   bool full_raw = false;
   std::string luma_name;
   std::string caption;
   bool typed = false;
   std::vector<TableRow> rows;
};

constexpr uint64_t TRACKED_REFRESH_FRAMES = 10;
std::array<ContentsTable, Shaders::STAGE_COUNT> contents_tables;

void BuildContents(ContentsTable& table, std::span<const std::byte> contents, const Trace::Track::Reading* reading)
{
   table.rows.clear();
   const Trace::Cbuffers::Description description = Trace::Cbuffers::Describe(table.hash, table.slot, contents, table.full_raw, table.luma_name);
   table.typed = description.source != "raw";
   if (table.typed)
   {
      table.caption = std::format("\"{}\", layout from the {}", description.name, description.source == "reflection" ? "shader's reflection" : "replacement's reflection");
      for (const Trace::Cbuffers::Field& field : description.fields)
      {
         const bool changed = ChangedRecently(reading, field.offset, field.size);
         table.rows.push_back({{std::format("{}", field.offset)}, {field.name}, {field.type}, {field.value, changed ? Tone::Warning : Tone::Info, changed ? "Changed in the last second" : ""}});
      }
      return;
   }
   table.caption = table.full_raw ? std::format("Full raw buffer ({} bytes, float4 rows): hover a value for int/hex", contents.size())
                                  : "No layout (stripped, not replaced): float4 rows, hover a value for it as an integer";
   std::vector<TableRow>& rows = table.rows;
   for (uint32_t offset = 0; offset < contents.size(); offset += 16)
   {
      TableRow& row = rows.emplace_back();
      row.push_back({std::format("c{}", offset / 16)});
      for (uint32_t component = 0; component < 4; component++)
      {
         const uint32_t at = offset + component * 4;
         if (at + 4 > contents.size())
         {
            row.push_back({});
            continue;
         }
         uint32_t word = 0;
         std::memcpy(&word, contents.data() + at, sizeof(word));
         const bool changed = ChangedRecently(reading, at, 4);
         row.push_back({FormatFloat(std::bit_cast<float>(word)), changed ? Tone::Warning : Tone::Info, std::format("int {}, uint {}, 0x{:08X}{}", int32_t(word), word, word, changed ? ", changed in the last second" : "")});
      }
   }
}

void DrawContents(ShaderStage stage, uint32_t hash, uint32_t slot, std::span<const std::byte> contents, const Trace::Track::Reading* reading,
   std::string_view luma_name = {})
{
   ContentsTable& table = contents_tables[size_t(stage)];
   const uint64_t reading_frame = reading ? reading->frame : 0;
   const bool stale_reading = reading ? (table.reading_frame == 0 || reading_frame < table.reading_frame || reading_frame - table.reading_frame >= TRACKED_REFRESH_FRAMES)
                                      : (table.data != contents.data() || table.reading_frame != 0);
   if (stale_reading || table.size != contents.size() || table.hash != hash || table.slot != slot || table.full_raw != cb_full_raw || table.luma_name != luma_name)
   {
      table.data = contents.data();
      table.size = contents.size();
      table.hash = hash;
      table.slot = slot;
      table.reading_frame = reading_frame;
      table.full_raw = cb_full_raw;
      table.luma_name = luma_name;
      BuildContents(table, contents, reading);
   }
   const float max_height = ImGui::GetTextLineHeightWithSpacing() * 16.f;
   Widgets::TextDim(table.caption);
   if (table.typed)
   {
      constexpr std::array<TableColumn, 4> COLUMNS = {{{"Offset", 52.f}, {"Name", 0.f, false}, {"Type", 80.f}, {"Value", 0.f}}};
      Widgets::DataTable("Fields", COLUMNS, table.rows, max_height);
      return;
   }
   constexpr std::array<TableColumn, 5> COLUMNS = {{{"Register", 64.f}, {"x", 0.f}, {"y", 0.f}, {"z", 0.f}, {"w", 0.f}}};
   Widgets::DataTable("Rows", COLUMNS, table.rows, max_height);
}

void DrawConstantBuffers(const Event& event, size_t index)
{
   const float unit = Style::Scale(Style::Sizes::UNIT);
   const ImGuiStyle& style = ImGui::GetStyle();
   const float rounding = Style::Scale(Style::Sizes::CONTROL_ROUNDING);
   const std::optional<Trace::Track::Target> tracked_target = Trace::Track::GetTarget();
   const std::string highlighted_res = Trace::Capture::GetTrackedResource();

   for (const Trace::Capture::Shader& shader : event.shaders)
   {
      std::vector<const Trace::Capture::ConstantBuffer*> buffers;
      for (const Trace::Capture::ConstantBuffer& buffer : event.constant_buffers)
      {
         if (buffer.stage == shader.stage)
            buffers.push_back(&buffer);
      }
      if (buffers.empty())
         continue;

      ImGui::PushID(int(shader.stage));
      std::optional<uint32_t>& open_slot = open_cb_slots[size_t(shader.stage)];
      const bool trackable = shader.luma_shader.empty() && shader.hash != 0;

      // Compact stage tag + wrapping CB mini-cards (3-4 per row)
      ImGui::AlignTextToFramePadding();
      Widgets::Tag(GetStageName(shader.stage), Tone::Info);
      float line_used = ImGui::GetItemRectSize().x;

      const Trace::Capture::ConstantBuffer* active_drawer = nullptr;
      bool active_tracking = false;
      for (const Trace::Capture::ConstantBuffer* buffer : buffers)
      {
         ImGui::PushID(int(buffer->slot));
         const bool is_luma = !buffer->luma_name.empty();
         const std::string slot_name = FormatRegister("CB", buffer->slot, buffer->space);
         const std::string chip_title = is_luma ? std::format("{} {}", slot_name, buffer->luma_name) : std::format("{} {}", slot_name, FormatByteSize(buffer->size));
         const bool viewing = open_slot && *open_slot == buffer->slot;
         const Trace::Track::Target target = {shader.hash, shader.stage, buffer->slot,
            trackable && !is_luma ? Trace::Capture::GetInstance(*frame, index, shader.stage, shader.hash) : 0};
         const bool tracking = trackable && !is_luma && tracked_target == target;
         const bool highlighted = !is_luma && !buffer->id.empty() && highlighted_res == buffer->id;

         if (viewing || tracking)
         {
            active_drawer = buffer;
            active_tracking = tracking;
         }

         const float card_h = ImGui::GetFrameHeight();
         const float icon_btn_w = std::ceil(card_h * 1.15f);
         const int num_btns = 1 + (trackable && !is_luma ? 1 : 0) + (!is_luma ? 1 : 0);
         const float title_w = ImGui::CalcTextSize(chip_title.c_str()).x;
         const float card_w = unit * 2.f + title_w + style.ItemInnerSpacing.x + float(num_btns) * (icon_btn_w + 1.f);
         line_used = Widgets::WrapSameLine(line_used, card_w);

         ImDrawList* draw_list = ImGui::GetWindowDrawList();
         draw_list->ChannelsSplit(2);
         draw_list->ChannelsSetCurrent(1);
         const ImVec2 start = ImGui::GetCursorScreenPos();
         ImGui::SetCursorScreenPos(ImVec2(start.x + unit * 1.5f, start.y));
         ImGui::BeginGroup();

         ImGui::AlignTextToFramePadding();
         Widgets::Text(chip_title, is_luma ? Tone::Injected : (viewing || tracking || highlighted ? Tone::Success : Tone::Info));
         if (ImGui::IsItemHovered())
         {
            const std::string range = buffer->constant_count ? std::format(", constants {}..{}", buffer->first_constant, buffer->first_constant + buffer->constant_count) : "";
            ImGui::SetTooltip("%s (%u bytes%s, %s)", is_luma ? buffer->luma_name.c_str() : slot_name.c_str(), buffer->size, range.c_str(), buffer->id.c_str());
         }

         const auto draw_cb_btn = [&](const char* icon, const char* id_suffix, bool active, const char* tooltip) {
            const ImVec2 btn_min = ImGui::GetCursorScreenPos();
            const ImVec2 btn_max(btn_min.x + icon_btn_w, btn_min.y + card_h);
            const bool clicked = ImGui::InvisibleButton(std::format("##{}", id_suffix).c_str(), ImVec2(icon_btn_w, card_h));
            const bool hovered = ImGui::IsItemHovered();
            const bool held = ImGui::IsItemActive();
            const ImVec4 fill = active ? (held ? Style::Colors::ACCENT_ACTIVE : hovered ? Style::Colors::ACCENT_HOVERED
                                                                                        : Style::Colors::ACCENT)
                                       : (held ? Style::Colors::CONTROL_ACTIVE : hovered ? Style::Colors::CONTROL_HOVERED
                                                                                         : Style::Colors::CONTROL);
            draw_list->AddRectFilled(btn_min, btn_max, ImGui::GetColorU32(fill), rounding);
            const ImVec2 glyph = ImGui::CalcTextSize(icon);
            const ImVec2 glyph_pos(std::floor(btn_min.x + (icon_btn_w - glyph.x) * 0.5f), std::floor(btn_min.y + (card_h - glyph.y) * 0.5f));
            draw_list->AddText(glyph_pos, ImGui::GetColorU32(active ? Style::Colors::TEXT_BRIGHT : Style::Colors::TEXT), icon);
            if (hovered)
               ImGui::SetTooltip("%s", tooltip);
            return clicked;
         };

         // View button (fills card vertically so it stays perfectly centered)
         ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
         if (draw_cb_btn(Icons::VIEW, "View", viewing, viewing ? "Hide constant buffer contents" : "View constant buffer contents"))
         {
            if (viewing)
               open_slot.reset();
            else
               open_slot = buffer->slot;
         }
         // Track button (live per-frame readback + opens drawer)
         if (trackable && !is_luma)
         {
            ImGui::SameLine(0.f, 1.f);
            if (draw_cb_btn(tracking ? Icons::STOP : Icons::RELOAD, "Track", tracking,
                   tracking ? "Stop live per-frame tracking" : "Track this constant buffer live every frame"))
            {
               if (tracking)
               {
                  Trace::Track::SetTarget(std::nullopt);
               }
               else
               {
                  Trace::Track::SetTarget(target);
                  open_slot = buffer->slot;
               }
            }
         }
         // Highlight button (highlights all trace events using this buffer)
         if (!is_luma)
         {
            ImGui::SameLine(0.f, 1.f);
            if (draw_cb_btn(highlighted ? Icons::STOP : Icons::TRACK, "Highlight", highlighted,
                   highlighted ? "Clear trace highlight for this buffer" : "Highlight all trace events using this constant buffer"))
               Trace::Capture::SetTrackedResource(highlighted ? std::string() : buffer->id);
         }

         ImGui::EndGroup();
         const ImVec2 end = ImGui::GetItemRectMax();
         draw_list->ChannelsSetCurrent(0);
         const ImVec2 bg_max(end.x, start.y + card_h);
         const ImVec4 bg = (viewing || tracking || highlighted) ? Style::WithAlpha(Style::Colors::ACCENT, 0.14f) : Style::WithAlpha(Style::Colors::PANEL, 0.65f);
         const ImVec4 border = (viewing || tracking || highlighted) ? Style::WithAlpha(Style::Colors::ACCENT, 0.65f) : Style::Colors::BORDER;
         draw_list->AddRectFilled(start, bg_max, ImGui::GetColorU32(bg), rounding);
         draw_list->AddRect(start, bg_max, ImGui::GetColorU32(border), rounding);
         draw_list->ChannelsMerge();
         ImGui::SetCursorScreenPos(start);
         ImGui::Dummy(ImVec2(bg_max.x - start.x, card_h));
         ImGui::PopID();
      }

      // Expandable drawer (hidden by default; shown only when View or Track is active for a CB in this stage)
      if (active_drawer)
      {
         const Trace::Capture::ConstantBuffer& buffer = *active_drawer;
         const std::string slot_name = FormatRegister("CB", buffer.slot, buffer.space);
         const std::string range = buffer.constant_count ? std::format(", constants {}..{}", buffer.first_constant, buffer.first_constant + buffer.constant_count) : "";
         ImGui::Indent(unit * 1.5f);
         ImGui::AlignTextToFramePadding();
         Widgets::Text(std::format("{} {}{}", GetStageName(shader.stage), slot_name,
                          buffer.luma_name.empty() ? std::string() : std::format(" ({})", buffer.luma_name)),
            buffer.luma_name.empty() ? Tone::Info : Tone::Injected);
         ImGui::SameLine();
         Widgets::TextDim(std::format("{} bytes{}, {}", buffer.size, range, buffer.id));
         ImGui::SameLine();
         Widgets::FilterChip("Full Buffer (Raw)", cb_full_raw, "Shows raw float4 rows across the entire captured buffer instead of stopping at shader reflection");
         ImGui::SameLine();
         if (Widgets::Button(std::format("{} Close", Icons::CLOSE), Widgets::ButtonKind::Secondary, "Closes this constant buffer table"))
            open_slot.reset();

         if (active_tracking)
         {
            const std::optional<Trace::Track::Reading> reading = Trace::Track::GetReading();
            if (!reading)
            {
               Widgets::Text("Tracking: not read yet (the call didn't happen since, or its deferred command list wasn't executed)", Tone::Warning);
            }
            else
            {
               Widgets::Text(std::format("{}: frame {} of the tracking, {} call{} of the shader last frame{}{}", reading->stale ? "Stale, not read last frame" : "Live", reading->frame,
                                reading->calls, reading->calls == 1 ? "" : "s",
                                reading->buffer != buffer.id ? std::format(", now buffer {}", reading->buffer) : std::string(),
                                reading->deferred ? " (copied on a deferred context, read after its command list executed)" : ""),
                  reading->stale ? Tone::Warning : Tone::Success);
               DrawContents(shader.stage, shader.hash, buffer.slot, reading->contents, &*reading);
            }
         }
         else if (buffer.contents.empty())
         {
            Widgets::TextDim(!event.list.empty() ? "Contents not read: not in an upload heap (D3D12), or the capture's copy limit"
                  : event.deferred               ? "Contents not read: recorded on a deferred context (Track reads them)"
                                                 : "Contents not read (the capture's copy limit)");
         }
         else
         {
            DrawContents(shader.stage, shader.hash, buffer.slot, GetBoundContents(buffer), nullptr, buffer.luma_name);
         }
         ImGui::Unindent(unit * 1.5f);
         ImGui::Spacing();
      }
      ImGui::PopID();
   }
}

void DrawStates(const Event& event)
{
   const Trace::Capture::DepthStencil& depth = event.depth_stencil;
   const Trace::Capture::Rasterizer& rasterizer = event.rasterizer;
   if (Widgets::Section("Blend", false))
   {
      const std::vector<PropertyRow> rows = {
         {"State", event.render_targets.empty() ? "no render target" : "per target, in Output Targets"},
         {"Blend factor", std::format("{} {} {} {}", event.blend_factor[0], event.blend_factor[1], event.blend_factor[2], event.blend_factor[3])},
         {"Alpha to coverage", event.alpha_to_coverage ? "on" : "off"},
      };
      Widgets::PropertyGrid("Blend", rows);
      Widgets::EndSection();
   }
   if (Widgets::Section("Depth Stencil", false))
   {
      const std::vector<PropertyRow> rows = {
         {"State bound", depth.state_bound ? "yes" : "no (the default)"},
         {"Depth", depth.depth_enable ? std::format("{}{}", depth.depth_func, depth.depth_write ? ", writes" : ", read only") : std::string("off")},
         {"Stencil", depth.stencil_enable ? std::format("ref {}, read 0x{:02X}, write 0x{:02X}", depth.stencil_ref, depth.read_mask, depth.write_mask) : std::string("off")},
         {"Front face", depth.stencil_enable ? depth.front : std::string("-")},
         {"Back face", depth.stencil_enable ? depth.back : std::string("-")},
      };
      Widgets::PropertyGrid("Depth", rows);
      Widgets::EndSection();
   }
   if (Widgets::Section("Rasterizer", false))
   {
      const std::vector<PropertyRow> rows = {
         {"State bound", rasterizer.state_bound ? "yes" : "no (the default)"},
         {"Cull", rasterizer.cull},
         {"Fill", rasterizer.fill},
         {"Front", rasterizer.front_ccw ? "counter clockwise" : "clockwise"},
         {"Depth bias", std::format("{}, slope {}", rasterizer.depth_bias, rasterizer.slope_scaled_depth_bias)},
         {"Depth clip", rasterizer.depth_clip ? "on" : "off"},
         {"Scissor", rasterizer.scissor_enable ? "on" : "off"},
         {"Multisample", rasterizer.multisample ? "on" : "off"},
      };
      Widgets::PropertyGrid("Rasterizer", rows);
      Widgets::EndSection();
   }
   if ((!event.viewports.empty() || !event.scissor_rects.empty()) && Widgets::Section(event.viewports_scaled || event.scissors_scaled ? "Viewports (Scaled)" : "Viewports", event.viewports_scaled || event.scissors_scaled))
   {
      if (event.viewports_scaled)
      {
         Widgets::Tag("Scaled", Tone::Injected, "Luma rescaled the active viewports for this draw");
         ImGui::SameLine();
         Widgets::TextDim("Active (rescaled) viewports:");
      }
      constexpr std::array<TableColumn, 7> VIEWPORT_COLUMNS = {{{"#", 28.f}, {"X", 0.f}, {"Y", 0.f}, {"Width", 0.f}, {"Height", 0.f}, {"Min depth", 0.f}, {"Max depth", 0.f}}};
      std::vector<TableRow> rows;
      for (size_t i = 0; i < event.viewports.size(); i++)
      {
         TableRow& row = rows.emplace_back();
         row.push_back({std::to_string(i)});
         for (const float value : event.viewports[i])
            row.push_back({std::format("{}", value), event.viewports_scaled ? Tone::Injected : Tone::Info});
      }
      Widgets::DataTable("Viewports", VIEWPORT_COLUMNS, rows);
      if (event.viewports_scaled && !event.original_viewports.empty())
      {
         Widgets::Caption("Game viewports (before scaling)");
         std::vector<TableRow> orig_rows;
         for (size_t i = 0; i < event.original_viewports.size(); i++)
         {
            TableRow& row = orig_rows.emplace_back();
            row.push_back({std::to_string(i)});
            for (const float value : event.original_viewports[i])
               row.push_back({std::format("{}", value)});
         }
         Widgets::DataTable("OriginalViewports", VIEWPORT_COLUMNS, orig_rows);
      }
      if (!event.scissor_rects.empty())
      {
         Widgets::Caption(event.scissors_scaled ? "Active (rescaled) scissor rectangles" : "Scissor rectangles");
         constexpr std::array<TableColumn, 5> SCISSOR_COLUMNS = {{{"#", 28.f}, {"Left", 0.f}, {"Top", 0.f}, {"Right", 0.f}, {"Bottom", 0.f}}};
         std::vector<TableRow> scissors;
         for (size_t i = 0; i < event.scissor_rects.size(); i++)
         {
            TableRow& row = scissors.emplace_back();
            row.push_back({std::to_string(i)});
            for (const int32_t value : event.scissor_rects[i])
               row.push_back({std::to_string(value), event.scissors_scaled ? Tone::Injected : Tone::Info});
         }
         Widgets::DataTable("Scissors", SCISSOR_COLUMNS, scissors);
         if (event.scissors_scaled && !event.original_scissor_rects.empty())
         {
            Widgets::Caption("Game scissor rectangles (before scaling)");
            std::vector<TableRow> orig_scissors;
            for (size_t i = 0; i < event.original_scissor_rects.size(); i++)
            {
               TableRow& row = orig_scissors.emplace_back();
               row.push_back({std::to_string(i)});
               for (const int32_t value : event.original_scissor_rects[i])
                  row.push_back({std::to_string(value)});
            }
            Widgets::DataTable("OriginalScissors", SCISSOR_COLUMNS, orig_scissors);
         }
      }
      Widgets::EndSection();
   }
}

// Shader Inspector sub-tabs: Disassembly, Decompiled, HLSL
const Trace::Capture::Shader* PickInspectedShader(const Event& event)
{
   std::vector<const Trace::Capture::Shader*> inspectable;
   for (const Trace::Capture::Shader& shader : event.shaders)
   {
      if (shader.hash != 0 && shader.luma_shader.empty())
         inspectable.push_back(&shader);
   }
   if (inspectable.empty())
      return nullptr;
   if (inspector_stage < 0 || size_t(inspector_stage) >= inspectable.size())
      inspector_stage = 0;
   if (inspectable.size() > 1)
   {
      std::vector<std::string> stage_labels;
      for (const Trace::Capture::Shader* shader : inspectable)
         stage_labels.push_back(std::format("{} 0x{:08X}", GetStageName(shader->stage), shader->hash));
      std::vector<std::string_view> views(stage_labels.begin(), stage_labels.end());
      Widgets::Segmented("InspectStage", inspector_stage, views);
   }
   return inspectable[size_t(inspector_stage)];
}

void DrawDisassemblyTab(const Event& event)
{
   const Trace::Capture::Shader* shader = PickInspectedShader(event);
   if (!shader)
   {
      Widgets::TextDim("No game shader bound for this event.");
      return;
   }
   const Shaders::Database::Shader* live = Shaders::Database::Find(shader->hash);
   constexpr std::array<std::string_view, 3> SOURCES = {"Original", "Patched", "Replacement"};
   Widgets::Segmented("DisasmSource", disasm_source, SOURCES);

   const std::string key = std::format("{:08X}_{}", shader->hash, disasm_source);
   if (cached_disasm_key != key)
   {
      cached_disasm_key = key;
      std::span<const uint8_t> bytecode;
      std::shared_ptr<const Shaders::CompiledShader> variant;
      if (disasm_source == 0 && live)
         bytecode = live->bytecode;
      else if (disasm_source == 1)
      {
         variant = GetShownCompiledShader(shader->hash, Shaders::Variants::Source::Patch);
         if (variant)
            bytecode = variant->bytecode;
      }
      else if (disasm_source == 2)
      {
         variant = GetShownCompiledShader(shader->hash, Shaders::Variants::Source::File);
         if (variant)
            bytecode = variant->bytecode;
      }
      if (bytecode.empty())
         cached_disasm_text = std::format("No {} bytecode available for 0x{:08X}.", SOURCES[size_t(disasm_source)], shader->hash);
      else
         cached_disasm_text = Shaders::Compiler::Disassemble(bytecode).value_or("Disassembly failed (DXBC requires D3DCompiler_47.dll; DXIL requires dxcompiler.dll).");
   }
   ImGui::SameLine();
   if (Widgets::Button(std::format("{} Copy", Icons::COPY), Widgets::ButtonKind::Secondary, "Copies the disassembly text to the clipboard"))
      ImGui::SetClipboardText(cached_disasm_text.c_str());

   if (ImGui::BeginChild("TraceDisasm", ImVec2(0.f, 0.f), ImGuiChildFlags_Borders))
      Widgets::Code(cached_disasm_text);
   ImGui::EndChild();
}

void DrawHlslTab(const Event& event)
{
   const Trace::Capture::Shader* shader = PickInspectedShader(event);
   if (!shader)
   {
      Widgets::TextDim("No game shader bound for this event.");
      return;
   }
   // Auto-default when switching shaders: Replacement (file) -> Patched -> Original
   if (last_hlsl_shader_hash != shader->hash)
   {
      last_hlsl_shader_hash = shader->hash;
      const bool has_replacement = std::ranges::any_of(shader->variants, [](Shaders::Variants::Source src) { return src == Shaders::Variants::Source::File; });
      const bool has_patched = std::ranges::any_of(shader->variants, [](Shaders::Variants::Source src) { return src == Shaders::Variants::Source::Patch; });
      hlsl_source = has_replacement ? 2 : (has_patched ? 1 : 0);
   }
   constexpr std::array<std::string_view, 3> SOURCES = {"Original", "Patched", "Replacement"};
   Widgets::Segmented("HlslSource", hlsl_source, SOURCES);

   if (hlsl_source == 2)
   {
      std::optional<Shaders::Variants::Variant> replacement;
      const std::shared_ptr<Shaders::Store> store = Shaders::GetShownStore();
      for (Shaders::Variants::Variant& variant : store ? Shaders::Variants::GetAll(*store) : std::vector<Shaders::Variants::Variant>())
      {
         if (variant.hash == shader->hash && variant.source == Shaders::Variants::Source::File)
         {
            replacement = std::move(variant);
            break;
         }
      }
      if (!replacement || replacement->file.empty())
      {
         Widgets::TextDim(std::format("No HLSL replacement file exists for 0x{:08X}.", shader->hash));
         return;
      }
      Widgets::Property("File", replacement->file);
      if (!replacement->messages.empty())
         Widgets::Code(replacement->messages);
      bool toggled = hlsl_inline_includes;
      if (Widgets::FilterChip("Inline Includes", toggled, "Expands #include directives with active shader defines"))
         hlsl_inline_includes = toggled;
      const std::string key = std::format("{}_{}", replacement->file, hlsl_inline_includes ? 1 : 0);
      if (cached_hlsl_key != key)
      {
         cached_hlsl_key = key;
         cached_hlsl_text = Shaders::Decompiler::ReadReplacementSource(replacement->file, hlsl_inline_includes).value_or("Source file not found.");
      }
      ImGui::SameLine();
      if (Widgets::Button(std::format("{} Copy", Icons::COPY), Widgets::ButtonKind::Secondary, "Copies the HLSL text to the clipboard"))
         ImGui::SetClipboardText(cached_hlsl_text.c_str());
      if (ImGui::BeginChild("TraceHlsl", ImVec2(0.f, 0.f), ImGuiChildFlags_Borders))
         Widgets::Code(cached_hlsl_text);
      ImGui::EndChild();
      return;
   }

   const Shaders::Database::Shader* live = Shaders::Database::Find(shader->hash);
   const std::string_view tag = hlsl_source == 0 ? "original" : "patched";
   const Shaders::Decompiler::Result result = Shaders::Decompiler::GetResult(shader->hash, tag);
   ImGui::SameLine();
   if (result.state != Shaders::Decompiler::State::Running &&
      Widgets::Button(result.state == Shaders::Decompiler::State::Ready ? std::format("{} Re-decompile", Icons::RELOAD) : std::format("{} Decompile", Icons::CODE),
         Widgets::ButtonKind::Primary, "Runs 3DMigoto cmd_Decompiler.exe (DXBC) or RenoDX decomp.exe --flatten --use-do-while (DXIL) asynchronously"))
   {
      Shaders::Bytecode bytes;
      if (hlsl_source == 0 && live)
         bytes = live->bytecode;
      else if (hlsl_source == 1)
      {
         if (const auto variant = GetShownCompiledShader(shader->hash, Shaders::Variants::Source::Patch))
            bytes = variant->bytecode;
      }
      if (!bytes.empty())
         Shaders::Decompiler::Request(shader->hash, tag, std::move(bytes));
      else
         Notifications::Post(Notifications::Kind::Warning, std::format("No {} bytecode available for 0x{:08X}", SOURCES[size_t(hlsl_source)], shader->hash));
   }

   if (result.state == Shaders::Decompiler::State::Running)
   {
      Widgets::ProgressBar(-1.f, "Decompiling...");
      return;
   }
   if (result.state == Shaders::Decompiler::State::Failed)
   {
      Widgets::Banner(Tone::Error, result.error);
      return;
   }
   if (result.state == Shaders::Decompiler::State::Idle)
   {
      Widgets::TextDim("Click Decompile to run the external decompiler asynchronously (results are cached under Luma/Cache/Decomp/).");
      return;
   }
   ImGui::SameLine();
   if (Widgets::Button(std::format("{} Copy", Icons::COPY), Widgets::ButtonKind::Secondary, "Copies the decompiled HLSL to the clipboard"))
      ImGui::SetClipboardText(result.hlsl.c_str());
   Widgets::TextDim(std::format("Decompiled via {}{}", result.tool, result.cached ? " (cached)" : ""));
   if (ImGui::BeginChild("TraceDecomp", ImVec2(0.f, 0.f), ImGuiChildFlags_Borders))
      Widgets::Code(result.hlsl);
   ImGui::EndChild();
}

// A copy's, clear's or CPU access's whole resource
void DrawResource(std::string_view label, const Trace::Capture::Resource& resource)
{
   std::vector<CardField> fields;
   if (resource.type == "Buffer")
   {
      fields.push_back({"Size:", std::format("{} bytes", resource.size[0])});
   }
   else
   {
      fields.push_back({"Format:", std::string(Utils::Format::GetName(resource.format)), resource.mirror ? Tone::Injected : Tone::Info});
      fields.push_back({"Size:", std::format("{}x{}x{}", resource.size[0], resource.size[1], resource.size[2])});
      fields.push_back({"Mips:", std::to_string(resource.size[3])});
   }
   DrawResourceCard(label, fields, resource, true);
}

void DrawResourceEvent(const Event& event, size_t index)
{
   const std::string_view heading = event.type == EventType::Copy ? "Copy" : event.type == EventType::Clear ? "Clear"
      : event.type == EventType::CpuRead                                                                    ? "CPU Read"
                                                                                                            : "CPU Write";
   if (Widgets::Section(heading))
   {
      std::vector<PropertyRow> rows = {
         {"Index", std::to_string(index)},
         {"Function", event.name},
         {"Thread", event.list.empty() ? std::format("{}{}", event.thread, event.deferred ? " (deferred context)" : " (immediate context)") : std::format("{} (command list {}, queue {})", event.thread, event.list, event.queue)},
      };
      if (const std::optional<Trace::Capture::Copy>& copy = event.copy)
      {
         if (copy->kind != Trace::Capture::CopyKind::Resource && copy->kind != Trace::Capture::CopyKind::BufferRegion)
            rows.push_back({"Subresources", std::format("{} -> {}", copy->source_subresource, copy->destination_subresource)});
         if (copy->kind == Trace::Capture::CopyKind::BufferRegion)
         {
            rows.push_back({"Destination offset", std::format("{} bytes", copy->destination_offset[0])});
            if (copy->source_box)
            {
               const uint64_t start = (*copy->source_box)[0];
               const uint64_t end = (*copy->source_box)[3];
               rows.push_back({"Source range", std::format("{}..{} ({} bytes)", start, end, end >= start ? end - start : 0)});
            }
         }
         else if (copy->kind != Trace::Capture::CopyKind::Resource && copy->kind != Trace::Capture::CopyKind::Resolve)
         {
            rows.push_back({"Destination offset", std::format("{}, {}, {}", copy->destination_offset[0], copy->destination_offset[1], copy->destination_offset[2])});
            if (copy->source_box)
            {
               const std::array<uint64_t, 6>& box = *copy->source_box;
               rows.push_back({"Source box", std::format("{}, {}, {} to {}, {}, {}", box[0], box[1], box[2], box[3], box[4], box[5])});
            }
         }
         if (copy->kind == Trace::Capture::CopyKind::Resolve)
            rows.push_back({"Resolve format", std::string(Utils::Format::GetName(copy->resolve_format))});
      }
      if (const std::optional<Trace::Capture::CpuAccess>& access = event.cpu_access)
      {
         if (!access->map_type.empty())
            rows.push_back({"Map type", access->map_type});
         rows.push_back({"Subresource", std::to_string(access->subresource)});
         if (access->destination_box)
         {
            const std::array<uint32_t, 6>& box = *access->destination_box;
            rows.push_back({"Destination box", std::format("{}, {}, {} to {}, {}, {}", box[0], box[1], box[2], box[3], box[4], box[5])});
         }
         if (access->row_pitch)
            rows.push_back({"Row pitch", std::format("{} bytes", access->row_pitch)});
         if (access->depth_pitch)
            rows.push_back({"Depth pitch", std::format("{} bytes", access->depth_pitch)});
      }
      if (event.type == EventType::Clear)
      {
         const std::array<float, 4>& values = event.clear_values;
         rows.push_back({"Values", std::format("{} {} {} {}", values[0], values[1], values[2], values[3])});
      }
      Widgets::PropertyGrid("Call", rows);
      Widgets::EndSection();
   }
   if (event.source && Widgets::Section("Source"))
   {
      DrawResource("Source", *event.source);
      Widgets::EndSection();
   }
   if (event.target && Widgets::Section(event.type == EventType::Copy ? "Destination" : "Resource"))
   {
      DrawResource("Target", *event.target);
      Widgets::EndSection();
   }
}

void DrawEventState(const Event& event, size_t index)
{
   if (!event.shaders.empty())
   {
      Widgets::SubHeader("Shaders");
      for (const Trace::Capture::Shader& shader : event.shaders)
         DrawShader(shader);
   }
   const bool is_dispatch = event.type == EventType::Dispatch;
   const bool has_no_call = event.type == EventType::Present || event.type == EventType::ThirdPartyWork;
   Widgets::SubHeader(event.sr                     ? "Super Resolution"
         : is_dispatch                             ? "Dispatch"
         : event.type == EventType::Present        ? "Present"
         : event.type == EventType::ThirdPartyWork ? "Third-Party Work"
                                                   : "Draw");
   std::vector<PropertyRow> rows = {
      {"Index", std::to_string(index)},
      {"Function", event.name},
      {"Outcome", std::string(GetOutcomeName(event.outcome)), event.outcome == Outcome::Ran ? Tone::Info : Tone::Warning},
      {"Thread", event.list.empty() ? std::format("{}{}", event.thread, event.deferred ? " (deferred context)" : " (immediate context)") : std::format("{} (command list {}, queue {})", event.thread, event.list, event.queue)},
   };
   if (event.sr)
   {
      const Trace::Capture::SrInfo& sr = *event.sr;
      const float scale = sr.output_height > 0 ? float(sr.render_height) / float(sr.output_height) : 1.f;
      rows.push_back({"Backend", sr.provider_version.empty() ? sr.backend : std::format("{} ({})", sr.backend, sr.provider_version), Tone::Injected});
      rows.push_back({"Resolution", std::format("{}x{} -> {}x{} ({:.2f}x){}", sr.render_width, sr.render_height, sr.output_width, sr.output_height, scale, sr.dynamic_resolution ? " [DRS]" : "")});
      rows.push_back({"Jitter", std::format("{:.5f}, {:.5f} ({} phases)", sr.jitter_x, sr.jitter_y, sr.jitter_phases)});
      rows.push_back({"Camera", std::format("FOV {:.4f} rad, Near {:.4g}, Far {:.4g}{}", sr.vert_fov, sr.near_plane, sr.far_plane, sr.inverted_depth ? " (inverted)" : "")});
      rows.push_back({"Color / Exposure", std::format("{}, Auto Exposure: {}, Pre-Exposure: {:.4g}", sr.hdr ? "HDR" : "LDR", sr.auto_exposure ? "on" : "off", sr.pre_exposure)});
      rows.push_back({"Motion Vectors", std::format("Scale ({:.4g}, {:.4g}), Jittered: {}", sr.mvs_x_scale, sr.mvs_y_scale, sr.mvs_jittered ? "yes" : "no")});
      if (sr.sharpness >= 0.f)
         rows.push_back({"Sharpness", std::format("{:.3f}", sr.sharpness)});
      if (sr.backend == "FSR 3" || sr.backend == "FSR 4")
         rows.push_back({"Velocity Factor", std::format("{:.3f}", sr.velocity_factor)});
      rows.push_back({"Reset History", sr.reset ? "yes" : "no", sr.reset ? Tone::Warning : Tone::Info});
      if (sr.copy_back)
         rows.push_back({"Output Staging", "UAV temp + copy back", Tone::Info});
   }
   else if (is_dispatch)
      rows.push_back({"Groups", std::format("{} x {} x {}", event.dispatch[0], event.dispatch[1], event.dispatch[2])});
   else if (!has_no_call)
   {
      rows.push_back({event.indexed ? "Indices" : "Vertices", std::to_string(event.vertex_or_index_count)});
      rows.push_back({"Instances", std::to_string(event.instance_count)});
      rows.push_back({event.indexed ? "Start index" : "Start vertex", std::to_string(event.start_vertex_or_index)});
      if (event.indexed)
         rows.push_back({"Base vertex", std::to_string(event.base_vertex)});
      rows.push_back({"Start instance", std::to_string(event.start_instance)});
      rows.push_back({"Topology", GetTopologyName(event.topology)});
   }
   if (event.indirect)
      rows.push_back({"Indirect", "arguments in a buffer"});
   Widgets::PropertyGrid("Call", rows);
   if (has_no_call)
      return;
   if (!event.constant_buffers.empty() && Widgets::Section("Constant Buffers"))
   {
      DrawConstantBuffers(event, index);
      Widgets::EndSection();
   }
   if (!event.shader_resources.empty() && Widgets::Section("Shader Resources"))
   {
      DrawViews(event, index, "SRV", event.shader_resources, Trace::DebugDraw::ViewMode::ShaderResource);
      Widgets::EndSection();
   }
   if (!event.unordered_access.empty() && Widgets::Section("Unordered Access"))
   {
      DrawViews(event, index, "UAV", event.unordered_access, Trace::DebugDraw::ViewMode::UnorderedAccess);
      Widgets::EndSection();
   }
   if ((!event.render_targets.empty() || event.depth_stencil_view) && Widgets::Section("Output Targets"))
   {
      DrawTargets(event, index);
      Widgets::EndSection();
   }
   if (!event.samplers.empty() && Widgets::Section("Samplers", false))
   {
      constexpr std::array<TableColumn, 4> COLUMNS = {{{"Slot", 44.f}, {"Filter", 0.f}, {"Address", 0.f}, {"Mip bias", 64.f}}};
      std::vector<TableRow> sampler_rows;
      for (const Trace::Capture::Sampler& sampler : event.samplers)
         sampler_rows.push_back({{FormatRegister("S", sampler.slot, sampler.space)}, {sampler.filter}, {sampler.address_names}, {std::format("{}", sampler.mip_lod_bias)}});
      Widgets::DataTable("Samplers", COLUMNS, sampler_rows);
      Widgets::EndSection();
   }
   if (!is_dispatch)
      DrawStates(event);
}

void DrawEvent(const Event& event, size_t index)
{
   if (Trace::Capture::IsResourceEvent(event))
      return DrawResourceEvent(event, index);
   if (event.type == EventType::Present || (event.type == EventType::ThirdPartyWork && event.shaders.empty()))
      return DrawEventState(event, index);
   if (ImGui::BeginTabBar("EventDetailTabs"))
   {
      if (ImGui::BeginTabItem("State"))
      {
         DrawEventState(event, index);
         ImGui::EndTabItem();
      }
      if (!event.shaders.empty() && ImGui::BeginTabItem("Disassembly"))
      {
         DrawDisassemblyTab(event);
         ImGui::EndTabItem();
      }
      if (!event.shaders.empty() && ImGui::BeginTabItem("HLSL"))
      {
         DrawHlslTab(event);
         ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
   }
}

void DrawFilters()
{
   ImGui::AlignTextToFramePadding();
   Widgets::TextDim(Icons::FILTER);
   ImGui::SetItemTooltip("Trace list filters");
   ImGui::SameLine();
   const float icon_width = ImGui::GetItemRectSize().x + ImGui::GetStyle().ItemSpacing.x;
   const float input_width = Style::Scale(Style::Sizes::LIST_WIDTH);
   ImGui::SetNextItemWidth(input_width);
   ImGui::InputTextWithHint("##Filter", std::format("{} Hash, name, or resource", Icons::SEARCH).c_str(), &filter.text);
   ImGui::SameLine();
   constexpr std::array<std::string_view, 7> STAGES = {"All Stages", "VS", "HS", "DS", "GS", "PS", "CS"};
   ImGui::SetNextItemWidth(Style::Scale(92.f));
   if (Widgets::Combo("StageFilter", stage_filter, STAGES))
      filter.stage = stage_filter == 0 ? std::nullopt : std::optional(ShaderStage(stage_filter - 1));
   float line_used = icon_width + input_width + Style::Scale(92.f) + ImGui::GetStyle().ItemSpacing.x * 2.f;
   const auto chip = [&](std::string_view label, bool& value, std::string_view tooltip) {
      line_used = Widgets::WrapSameLine(line_used, ImGui::CalcTextSize(label.data(), label.data() + label.size()).x + ImGui::GetStyle().FramePadding.x * 2.f);
      return Widgets::FilterChip(label, value, tooltip);
   };
   chip("VS-only", filter.include_vertex_only, "Also lists draws without a pixel shader (depth-only passes), by their vertex shader.");
   chip("Luma", filter.luma_passes, "Lists Luma's own passes");
   chip("Replaced", filter.replaced_only, "Only calls whose shader has a variant");
   chip("Upgraded", filter.upgraded_only, "Only events reading or writing a mirrored resource");
   chip("Scaled", filter.scaled_only, "Only events with scaled mirrors, rescaled viewports/scissors, or scaled copies");
   chip("Cancelled", filter.cancelled_only, "Only calls skipped or replaced by callbacks or trace actions");
   chip("Unique", unique_only, "Only the first call of each shader");
   chip("Copies", filter.copies, "Lists CopyResource, CopySubresourceRegion, and ResolveSubresource");
   chip("Clears", filter.clears, "Lists ClearRenderTargetView, ClearDepthStencilView, and ClearUnorderedAccessView");
   chip("CPU Writes", filter.cpu_writes, "Lists Map (write) and UpdateSubresource");
   chip("CPU Reads", filter.cpu_reads, "Lists Map (read)");
   chip("Buffers", filter.buffer_writes, "Also lists CPU reads/writes to buffers (cbuffer uploads, many per frame).");
   chip("Third-party", filter.third_party, "Lists game draws intercepted by ReShade add-ons and third-party present work.");
   if (!last_tracked_resource.empty())
      chip("Highlighted", highlighted_only, "Only events reading or writing the currently tracked resource");
   if (chip("Threads", show_thread, "Shows each call's thread and deferred context"))
      Rebuild();
}

void DrawDebugDrawBar()
{
   if (!Trace::DebugDraw::IsActive())
      return;
   if (Widgets::Banner(Tone::Injected, std::format("Debug Draw active on {}", Trace::DebugDraw::DescribeTarget()), "Stop", "Stops live texture preview"))
   {
      Trace::DebugDraw::Clear();
      return;
   }
   Trace::DebugDraw::Options opts = Trace::DebugDraw::GetOptions();
   bool changed = false;
   ImGui::AlignTextToFramePadding();
   const std::string debug_label = std::format("{} Debug Draw:", Icons::DISPLAY);
   Widgets::Text(debug_label, Tone::Injected);
   ImGui::SetItemTooltip("Display options for the active Debug Draw texture preview");
   float line_used = ImGui::GetItemRectSize().x;
   const auto flag_chip = [&](std::string_view label, bool& value, std::string_view tooltip) {
      line_used = Widgets::WrapSameLine(line_used, ImGui::CalcTextSize(label.data(), label.data() + label.size()).x + ImGui::GetStyle().FramePadding.x * 2.f);
      changed |= Widgets::FilterChip(label, value, tooltip);
   };
   flag_chip("Fullscreen", opts.fullscreen, "Stretches the preview across the full screen");
   flag_chip("Alpha", opts.show_alpha, "Displays the alpha channel");
   flag_chip("Tonemap", opts.tonemap, "Applies Reinhard tonemapping to HDR values");
   flag_chip("Lin->Gamma", opts.linear_to_gamma, "Encodes linear values to gamma");
   flag_chip("Gamma->Lin", opts.gamma_to_linear, "Decodes gamma values to linear");
   flag_chip("Zoom 4x", opts.zoom_4x, "4x zoom");
   flag_chip("Flip Y", opts.flip_y, "Flips the preview vertically");
   flag_chip("Abs", opts.do_abs, "Displays absolute value (useful for motion vectors / signed normals)");
   flag_chip("Red Only", opts.red_only, "Replicates red channel to RGB (useful for depth / single-channel textures)");
   flag_chip("Bilinear", opts.bilinear, "Uses bilinear filtering instead of point sampling");
   ImGui::SameLine();
   ImGui::SetNextItemWidth(Style::Scale(80.f));
   if (Widgets::Slider("DebugMip", opts.mip_level, 0, 12))
      changed = true;
   ImGui::SetItemTooltip("Mip level");
   if (changed)
      Trace::DebugDraw::SetOptions(opts);
}

void DrawTrackedBanners()
{
   if (const std::string tracked_res = Trace::Capture::GetTrackedResource(); !tracked_res.empty())
   {
      if (Widgets::Banner(Tone::Info, std::format("Highlighting resource {} (pink = read or write, red = read+write)", tracked_res), "Clear", "Clears resource highlight"))
      {
         Trace::Capture::SetTrackedResource({});
         highlighted_only = false;
      }
   }
   const std::optional<Trace::Track::Target> target = Trace::Track::GetTarget();
   if (!target)
      return;
   const std::string text = std::format("Tracking CB{} of {} 0x{:08X} (call {} of it each frame)", target->slot, GetStageName(target->stage), target->hash, target->instance);
   if (Widgets::Banner(Tone::Info, text, "Stop", "Stops reading the slot every frame"))
      Trace::Track::SetTarget(std::nullopt);
}

void DrawRowAction(size_t index)
{
   if (!frame || index >= frame->events.size())
      return;
   const Event& event = frame->events[index];
   const std::optional<Trace::DebugDraw::PassTarget> base = MakeBasePassTarget(event, index);
   if (!base)
      return;
   const bool active = Trace::DebugDraw::IsSamePass(*base);
   const float btn_w = ImGui::CalcTextSize(Icons::SEARCH).x + ImGui::GetStyle().FramePadding.x * 2.f;
   const float right_x = ImGui::GetWindowContentRegionMax().x - btn_w;
   ImGui::SameLine(right_x);
   ImGui::SetNextItemAllowOverlap();
   if (active)
      ImGui::PushStyleColor(ImGuiCol_Button, Style::Colors::ACCENT);
   if (ImGui::SmallButton(Icons::SEARCH))
      SelectPassDebugDraw(event, index, true);
   if (active)
      ImGui::PopStyleColor();
   ImGui::SetItemTooltip("Debug Draw this pass (preserves last view mode/slot)");
}
} // namespace

void DrawTrace()
{
   if (Widgets::Button(std::format("{} Capture Frame", Icons::CAPTURE), Widgets::ButtonKind::Primary, "Records every draw and dispatch of the next frame"))
   {
      Trace::Capture::Request();
      requested = true;
   }
   ImGui::SameLine();
   const bool has_anything_to_clear = frame != nullptr || Trace::DebugDraw::IsActive() || !Trace::Capture::GetTrackedResource().empty();
   if (has_anything_to_clear &&
      Widgets::Button(std::format("{} Clear Capture & Debug", Icons::DELETE_), Widgets::ButtonKind::Secondary,
         "Clears the captured frame, tracked resource highlight, and active Debug Draw preview."))
   {
      Trace::Capture::ClearLast();
      Trace::Capture::SetTrackedResource({});
      Trace::DebugDraw::Clear();
      highlighted_only = false;
      requested = false;
   }
   ImGui::SameLine();
   const std::shared_ptr<const Trace::Capture::Frame> last = Trace::Capture::GetLast();
   if (requested && last != frame)
      requested = false;
   if (requested)
      Widgets::TextDim("Capturing the next frame...");
   else if (last)
      Widgets::TextDim(std::format("Capture {}: {} calls", last->id, last->events.size() - 1));
   else
      Widgets::TextDim("No capture yet");
   if (last != frame)
   {
      frame = last;
      selected = SIZE_MAX;
      open_cb_slots = {};
      contents_tables = {};
      Rebuild();
   }
   else if (frame && last_tracked_resource != Trace::Capture::GetTrackedResource())
   {
      Rebuild();
   }
   DrawTrackedBanners();
   if (!frame)
   {
      DrawDebugDrawBar();
      return;
   }
   DrawFilters();
   DrawDebugDrawBar();
   Widgets::ListDetail("Events", entries, selected, [](size_t index) { DrawEvent(frame->events[index], index); }, 0.f, &IsListed, &DrawRowAction);
}
} // namespace Panels
} // namespace UI
} // namespace Luma
#endif
