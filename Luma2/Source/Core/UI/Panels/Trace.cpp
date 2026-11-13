#include "UI/Panels/Trace.hpp"

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

#include "Shaders/Database.hpp"
#include "Trace/Capture.hpp"
#include "Trace/Cbuffers.hpp"
#include "Trace/Track.hpp"
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
using Widgets::PropertyRow;
using Widgets::TableCell;
using Widgets::TableColumn;
using Widgets::TableRow;
using Widgets::Tone;

// Words that changed this many frames ago or less are highlighted in a tracked cbuffer
constexpr uint64_t RECENT_CHANGE_FRAMES = 60;

// The frame the list was built from
std::shared_ptr<const Trace::Capture::Frame> frame;
std::vector<Widgets::ListEntry> entries;
size_t selected = SIZE_MAX;
bool requested = false;

// The list's filters (1.0's "Ignore Vertex Shaders" and "Ignore Custom Draws" as their opposites) and its options
Trace::Capture::Filter filter;
bool unique_only = false; // 1.0's "List Unique Shaders Only": the first call of each primary shader
bool show_thread = false; // 1.0's "Show Command List / Thread Info"
// The events `unique_only` keeps, for the frame
std::vector<bool> first_of_shader;
// The constant buffer slot shown per stage, kept from one call to the next
std::array<uint32_t, 6> selected_slots = {};

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

Widgets::ListEntry Describe(const Event& event, size_t index)
{
   const std::string thread = show_thread ? std::format(" [{}{}]", event.thread, event.deferred ? " deferred" : "") : std::string();
   if (event.type == EventType::Present)
      return {std::format("{:>5} Present", index), Tone::Info};
   if (event.type == EventType::LumaPass)
      return {std::format("{:>5} {} (Luma){}", index, event.name, thread), Tone::Injected};
   // Labelled by its primary shader; the other stages are in its detail and match the filters too
   const Trace::Capture::Shader* shader = Trace::Capture::GetPrimaryShader(event);
   std::string label = std::format("{:>5} ", index);
   if (shader)
      label += std::format("{} 0x{:08X}{}{}", GetStageName(shader->stage), shader->hash, shader->name.empty() ? "" : " ", shader->name);
   else
      label += event.name;
   label += thread;
   Tone tone = Tone::Info;
   if (event.outcome != Outcome::Ran)
      tone = event.outcome == Outcome::SkippedByTrace ? Tone::Error : Tone::Warning;
   else if (shader && shader->variant)
      tone = Tone::Success;
   if (shader && shader->variant)
      label += std::format(" [{}]", Shaders::Variants::GetLabel(Shaders::Variants::GetChoice(*shader->variant)));
   return {std::move(label), tone};
}

void Rebuild()
{
   entries.clear();
   first_of_shader.clear();
   std::unordered_set<uint64_t> seen;
   for (size_t i = 0; frame && i < frame->events.size(); i++)
   {
      const Event& event = frame->events[i];
      entries.push_back(Describe(event, i));
      const Trace::Capture::Shader* shader = Trace::Capture::GetPrimaryShader(event);
      const uint64_t key = shader ? (uint64_t(shader->stage) << 32 | shader->hash) : UINT64_MAX;
      first_of_shader.push_back(!shader || seen.insert(key).second);
   }
}

bool IsListed(size_t index)
{
   return Trace::Capture::Matches(frame->events[index], filter) && (!unique_only || first_of_shader[index]);
}

std::string DescribeResource(const Trace::Capture::Resource& resource)
{
   std::string text = std::format("{} {} {}", resource.type, Utils::Format::GetName(resource.format), resource.id);
   if (!resource.debug_name.empty())
      text += std::format(" \"{}\"", resource.debug_name);
   return text;
}

std::string GetSize(const Trace::Capture::View& view)
{
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

TableRow ViewRow(std::string slot, const Trace::Capture::View& view)
{
   return {{std::move(slot)}, {std::string(Utils::Format::GetName(view.format))}, {GetSize(view)}, {std::to_string(view.mip)}, {DescribeResource(view.resource)}};
}

// Columns shared by the view tables
constexpr std::array<TableColumn, 5> VIEW_COLUMNS = {{{"Slot", 44.f}, {"Format", 0.f}, {"Size", 90.f}, {"Mip", 32.f}, {"Resource", 0.f}}};

void DrawViews(std::string_view id, std::string_view slot_prefix, std::span<const Trace::Capture::View> views)
{
   std::vector<TableRow> rows;
   for (const Trace::Capture::View& view : views)
      rows.push_back(ViewRow(std::format("{}{}", slot_prefix, view.slot), view));
   Widgets::DataTable(id, VIEW_COLUMNS, rows);
}

void DrawTargets(const Event& event)
{
   constexpr std::array<TableColumn, 6> COLUMNS = {{{"Slot", 44.f}, {"Format", 0.f}, {"Size", 90.f}, {"Write", 44.f}, {"Blend", 0.f}, {"Resource", 0.f}}};
   std::vector<TableRow> rows;
   for (const Trace::Capture::View& view : event.render_targets)
      rows.push_back({{std::format("o{}", view.slot)}, {std::string(Utils::Format::GetName(view.format))}, {GetSize(view)}, {GetWriteMask(view.write_mask)},
         {view.blend.empty() ? std::string("off") : view.blend}, {DescribeResource(view.resource)}});
   if (event.depth_stencil_view)
   {
      const Trace::Capture::View& view = *event.depth_stencil_view;
      const Trace::Capture::DepthStencil& depth = event.depth_stencil;
      const std::string state = depth.depth_enable ? std::format("{}{}", depth.depth_func, depth.depth_write ? ", writes" : ", read only") : std::string("depth off");
      rows.push_back({{"depth"}, {std::string(Utils::Format::GetName(view.format))}, {GetSize(view)}, {depth.depth_write ? "D" : "-"}, {state}, {DescribeResource(view.resource)}});
   }
   Widgets::DataTable("Targets", COLUMNS, rows);
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
      live.forced_variant = choices[size_t(index)];
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
   Widgets::HashChip(shader.hash, shader.stage, shader.name);
   if (!shader.name.empty())
   {
      ImGui::SameLine();
      Widgets::TextDim(shader.name);
   }
   ImGui::SameLine();
   // What ran: a badge, the picker below chooses
   if (shader.variant)
      Widgets::Tag(Shaders::Variants::GetLabel(Shaders::Variants::GetChoice(*shader.variant)), Tone::Success, "Its variant ran in this call");
   else if (!shader.variants.empty())
      Widgets::Tag("Original", Tone::Warning, "It has a variant, but the game's shader ran in this call (forced, or the variant wasn't ready)");
   else
      Widgets::Tag("Game", Tone::Info, "No variant: the game's shader");
   // The actions apply to the shader now, for the next frames
   Shaders::Database::Shader* live = Shaders::Database::Find(shader.hash);
   if (live)
   {
      Widgets::RowLabel("");
      if (!shader.variants.empty())
      {
         DrawVariantPicker(shader, *live);
         ImGui::SameLine();
      }
      if (Widgets::Button("Dump", Widgets::ButtonKind::Secondary, "Writes its bytecode to Luma/Dump"))
      {
         const std::filesystem::path file = Shaders::Database::Dump(*live);
         Widgets::Toast(file.empty() ? Tone::Error : Tone::Success, file.empty() ? "The shader couldn't be dumped" : std::format("Dumped {}", file.filename().string()));
      }
      ImGui::SameLine();
      bool skip_draws = live->skip_draws;
      if (Widgets::Toggle("Skip Draws", skip_draws))
         live->skip_draws = skip_draws;
      ImGui::SetItemTooltip("Skips this shader's draws from now on");
      ImGui::SameLine();
      Widgets::TextDim("skip its draws");
   }
   ImGui::PopID();
}

// The cbuffer's contents as the shader sees them (a D3D11.1 range from its first constant, 16 bytes each)
std::span<const std::byte> GetBoundContents(const Trace::Capture::ConstantBuffer& buffer)
{
   std::span<const std::byte> contents = buffer.contents;
   if (buffer.constant_count)
   {
      const size_t first = std::min<size_t>(size_t(buffer.first_constant) * 16, contents.size());
      contents = contents.subspan(first, std::min<size_t>(size_t(buffer.constant_count) * 16, contents.size() - first));
   }
   return contents;
}

// Whether the bytes [offset, offset + size) of a tracked reading changed recently
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

// Typed fields when a layout is known, else float4 rows; `reading` highlights what changed while tracked
void DrawContents(uint32_t hash, uint32_t slot, std::span<const std::byte> contents, const Trace::Track::Reading* reading)
{
   const Trace::Cbuffers::Description description = Trace::Cbuffers::Describe(hash, slot, contents);
   const float max_height = ImGui::GetTextLineHeightWithSpacing() * 18.f;
   if (description.source != "raw")
   {
      Widgets::TextDim(std::format("\"{}\", layout from the {}", description.name, description.source == "reflection" ? "shader's reflection" : "replacement's reflection"));
      constexpr std::array<TableColumn, 4> COLUMNS = {{{"Offset", 52.f}, {"Name", 0.f, false}, {"Type", 80.f}, {"Value", 0.f}}};
      std::vector<TableRow> rows;
      for (const Trace::Cbuffers::Field& field : description.fields)
      {
         const bool changed = ChangedRecently(reading, field.offset, field.size);
         rows.push_back({{std::format("{}", field.offset)}, {field.name}, {field.type}, {field.value, changed ? Tone::Warning : Tone::Info, changed ? "Changed in the last second" : ""}});
      }
      Widgets::DataTable("Fields", COLUMNS, rows, max_height);
      return;
   }
   Widgets::TextDim("No layout (stripped, not replaced): float4 rows, hover a value for it as an integer");
   constexpr std::array<TableColumn, 5> COLUMNS = {{{"Register", 64.f}, {"x", 0.f}, {"y", 0.f}, {"z", 0.f}, {"w", 0.f}}};
   std::vector<TableRow> rows;
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
   Widgets::DataTable("Rows", COLUMNS, rows, max_height);
}

// 1.0's constant buffer tracking: this slot of this shader, read again every frame
void DrawTrackButton(const Trace::Track::Target& target, bool tracking)
{
   const std::string label = tracking ? std::format("{} Stop Tracking", Icons::STOP) : std::format("{} Track", Icons::TRACK);
   if (Widgets::Button(label, tracking ? Widgets::ButtonKind::Primary : Widgets::ButtonKind::Secondary,
          "Reads this slot of this shader every frame from now on (this call's instance of it in the frame), highlighting the values that change"))
      Trace::Track::Set(tracking ? std::nullopt : std::optional(target));
}

// The instance of the event's `stage` shader in the frame: how many calls of it came before
uint32_t GetInstance(size_t index, ShaderStage stage, uint32_t hash)
{
   uint32_t instance = 0;
   for (size_t i = 0; i < index; i++)
   {
      for (const Trace::Capture::Shader& shader : frame->events[i].shaders)
         instance += shader.stage == stage && shader.hash == hash && shader.luma_shader.empty();
   }
   return instance;
}

void DrawConstantBuffers(const Event& event, size_t index)
{
   // One selector per stage over its bound slots, the selected slot's contents below it
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
      uint32_t& selected_slot = selected_slots[size_t(shader.stage)];
      auto current = std::ranges::find(buffers, selected_slot, &Trace::Capture::ConstantBuffer::slot);
      if (current == buffers.end())
         current = buffers.begin();
      std::vector<std::string> names;
      for (const Trace::Capture::ConstantBuffer* buffer : buffers)
         names.push_back(std::format("b{}", buffer->slot));
      const std::vector<std::string_view> labels(names.begin(), names.end());
      int32_t choice = int32_t(current - buffers.begin());
      Widgets::RowLabel(std::format("{} slots", GetStageTitle(shader.stage)));
      if (labels.size() <= Widgets::MAX_SEGMENTS)
         Widgets::Segmented("Slot", choice, labels);
      else
      {
         ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.f);
         Widgets::Combo("Slot", choice, labels);
      }
      const Trace::Capture::ConstantBuffer& buffer = *buffers[size_t(choice)];
      selected_slot = buffer.slot;

      const bool trackable = shader.luma_shader.empty() && shader.hash != 0;
      const Trace::Track::Target target = {shader.hash, shader.stage, buffer.slot, trackable ? GetInstance(index, shader.stage, shader.hash) : 0};
      const std::optional<Trace::Track::Target> tracked_target = Trace::Track::Get();
      const bool tracking = trackable && tracked_target == target;
      if (trackable)
      {
         ImGui::SameLine();
         DrawTrackButton(target, tracking);
      }
      const std::string range = buffer.constant_count ? std::format(", constants {} to {}", buffer.first_constant, buffer.first_constant + buffer.constant_count) : "";
      Widgets::RowLabel("");
      Widgets::TextDim(std::format("{} bytes{}, buffer {}", buffer.size, range, buffer.id));

      if (tracking)
      {
         const std::optional<Trace::Track::Reading> reading = Trace::Track::GetReading();
         if (!reading)
         {
            Widgets::Text("Tracking: not read yet (the call didn't happen since, or its deferred command list wasn't executed)", Tone::Warning);
         }
         else
         {
            Widgets::Text(std::format("{}: frame {} of the tracking, {} call{} of the shader last frame{}{}", reading->stale ? "Stale, not read last frame" : "Live", reading->frame, reading->calls,
                             reading->calls == 1 ? "" : "s",
                             reading->buffer != buffer.id ? std::format(", now buffer {}", reading->buffer) : std::string(),
                             reading->deferred ? " (copied on a deferred context, read after its command list executed)" : ""),
               reading->stale ? Tone::Warning : Tone::Success);
            DrawContents(shader.hash, buffer.slot, reading->contents, &*reading);
         }
      }
      else if (buffer.contents.empty())
      {
         Widgets::TextDim(event.deferred ? "Contents not read: recorded on a deferred context (Track reads them)" : "Contents not read (the capture's copy limit)");
      }
      else
      {
         DrawContents(shader.hash, buffer.slot, GetBoundContents(buffer), nullptr);
      }
      ImGui::Spacing();
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
   if ((!event.viewports.empty() || !event.scissor_rects.empty()) && Widgets::Section("Viewports", false))
   {
      constexpr std::array<TableColumn, 7> VIEWPORT_COLUMNS = {{{"#", 28.f}, {"X", 0.f}, {"Y", 0.f}, {"Width", 0.f}, {"Height", 0.f}, {"Min depth", 0.f}, {"Max depth", 0.f}}};
      std::vector<TableRow> rows;
      for (size_t i = 0; i < event.viewports.size(); i++)
      {
         TableRow& row = rows.emplace_back();
         row.push_back({std::to_string(i)});
         for (const float value : event.viewports[i])
            row.push_back({std::format("{}", value)});
      }
      Widgets::DataTable("Viewports", VIEWPORT_COLUMNS, rows);
      if (!event.scissor_rects.empty())
      {
         Widgets::Caption("Scissor rectangles");
         constexpr std::array<TableColumn, 5> SCISSOR_COLUMNS = {{{"#", 28.f}, {"Left", 0.f}, {"Top", 0.f}, {"Right", 0.f}, {"Bottom", 0.f}}};
         std::vector<TableRow> scissors;
         for (size_t i = 0; i < event.scissor_rects.size(); i++)
         {
            TableRow& row = scissors.emplace_back();
            row.push_back({std::to_string(i)});
            for (const int32_t value : event.scissor_rects[i])
               row.push_back({std::to_string(value)});
         }
         Widgets::DataTable("Scissors", SCISSOR_COLUMNS, scissors);
      }
      Widgets::EndSection();
   }
}

void DrawEvent(const Event& event, size_t index)
{
   if (!event.shaders.empty() && Widgets::Section("Shaders"))
   {
      for (const Trace::Capture::Shader& shader : event.shaders)
         DrawShader(shader);
      Widgets::EndSection();
   }
   const bool is_dispatch = event.type == EventType::Dispatch;
   if (Widgets::Section(is_dispatch ? "Dispatch" : event.type == EventType::Present ? "Present"
                                                                                    : "Draw"))
   {
      std::vector<PropertyRow> rows = {
         {"Index", std::to_string(index)},
         {"Function", event.name},
         {"Outcome", std::string(GetOutcomeName(event.outcome)), event.outcome == Outcome::Ran ? Tone::Info : Tone::Warning},
         {"Thread", std::format("{}{}", event.thread, event.deferred ? " (deferred context)" : " (immediate context)")},
      };
      if (is_dispatch)
         rows.push_back({"Groups", std::format("{} x {} x {}", event.dispatch[0], event.dispatch[1], event.dispatch[2])});
      else if (event.type != EventType::Present)
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
      Widgets::EndSection();
   }
   if (event.type == EventType::Present)
      return;
   if ((!event.render_targets.empty() || event.depth_stencil_view) && Widgets::Section("Output Targets"))
   {
      DrawTargets(event);
      Widgets::EndSection();
   }
   if (!event.shader_resources.empty() && Widgets::Section("Shader Resources"))
   {
      DrawViews("Resources", "t", event.shader_resources);
      Widgets::EndSection();
   }
   if (!event.unordered_access.empty() && Widgets::Section("Unordered Access"))
   {
      DrawViews("UnorderedAccess", "u", event.unordered_access);
      Widgets::EndSection();
   }
   if (!event.constant_buffers.empty() && Widgets::Section("Constant Buffers"))
   {
      DrawConstantBuffers(event, index);
      Widgets::EndSection();
   }
   if (!event.samplers.empty() && Widgets::Section("Samplers", false))
   {
      constexpr std::array<TableColumn, 4> COLUMNS = {{{"Slot", 44.f}, {"Filter", 0.f}, {"Address", 0.f}, {"Mip bias", 64.f}}};
      std::vector<TableRow> rows;
      for (const Trace::Capture::Sampler& sampler : event.samplers)
         rows.push_back({{std::format("s{}", sampler.slot)}, {sampler.filter}, {sampler.address_names}, {std::format("{}", sampler.mip_lod_bias)}});
      Widgets::DataTable("Samplers", COLUMNS, rows);
      Widgets::EndSection();
   }
   if (!is_dispatch)
      DrawStates(event);
}

void DrawFilters()
{
   const float input_width = Style::Scale(Style::Sizes::LIST_WIDTH);
   ImGui::SetNextItemWidth(input_width);
   ImGui::InputTextWithHint("##Filter", std::format("{} Hash or name, any stage", Icons::SEARCH).c_str(), &filter.text);
   float line_used = input_width;
   // Wrapped onto the next line when the window is narrow
   const auto chip = [&](std::string_view label, bool& value, std::string_view tooltip) {
      line_used = Widgets::WrapSameLine(line_used, ImGui::CalcTextSize(label.data(), label.data() + label.size()).x + ImGui::GetStyle().FramePadding.x * 2.f);
      return Widgets::FilterChip(label, value, tooltip);
   };
   chip("Vertex-only draws", filter.include_vertex_only, "Also lists the draws without a pixel shader (depth only passes), by their vertex shader. Off, as 1.0 hid them.");
   chip("Luma passes", filter.luma_passes, "Lists Luma's own passes");
   chip("Replaced", filter.replaced_only, "Only the calls whose shader has a variant");
   chip("Unique", unique_only, "Only the first call of each shader");
   if (chip("Threads", show_thread, "Shows each call's thread and deferred context"))
      Rebuild();
}

void DrawTracked()
{
   const std::optional<Trace::Track::Target> target = Trace::Track::Get();
   if (!target)
      return;
   const std::string text = std::format("Tracking b{} of {} 0x{:08X} (call {} of it each frame)", target->slot, GetStageName(target->stage), target->hash, target->instance);
   if (Widgets::Banner(Tone::Info, text, "Stop", "Stops reading the slot every frame"))
      Trace::Track::Set(std::nullopt);
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
      Rebuild();
   }
   DrawTracked();
   if (!frame)
      return;
   DrawFilters();
   Widgets::ListDetail("Events", entries, selected, [](size_t index) { DrawEvent(frame->events[index], index); }, 0.f, &IsListed);
}
} // namespace Panels
} // namespace UI
} // namespace Luma
#endif
