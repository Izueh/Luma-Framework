#include "UI/Panels/Shaders.hpp"

#if DEVELOPMENT
#include <algorithm>
#include <format>
#include <map>
#include <string>
#include <vector>

#include <imgui.h>

#include "Resources/ResourceDatabase.hpp"
#include "Shaders/Database.hpp"
#include "System/Jobs.hpp"
#include "Shaders/Shaders.hpp"
#include "Shaders/Variants.hpp"
#include "UI/Widgets.hpp"

namespace Luma {
namespace UI {
namespace Panels {
namespace {
using Widgets::Tone;

// One of Luma's shaders, as the database has it
struct LumaShader
{
   const Resources::Database::Entry* entry;
   Resources::Database::ShaderState state;
};

// One entry of the list: a replacement file with its hashes, or one of Luma's shaders
struct Entry
{
   std::string file;
   std::vector<Shaders::Variants::Variant> replacements;
   const LumaShader* luma_shader = nullptr;
};

size_t selected = SIZE_MAX;

// The stage a replacement's target names ("ps_5_0"), for a hash the game didn't create yet
ShaderStage GetStage(std::string_view file)
{
   const size_t dot = file.rfind(".hlsl");
   const std::string_view prefix = dot != std::string_view::npos && dot >= 6 ? file.substr(dot - 6, 2) : std::string_view();
   constexpr std::string_view PREFIXES[] = {"vs", "hs", "ds", "gs", "ps", "cs"};
   for (size_t i = 0; i < std::size(PREFIXES); i++)
   {
      if (prefix == PREFIXES[i])
         return ShaderStage(i);
   }
   return ShaderStage::Pixel;
}

void DrawReplacement(const Shaders::Variants::Variant& replacement, std::string_view file)
{
   const Shaders::Database::Shader* shader = Shaders::Database::Find(replacement.hash);
   Widgets::HashChip(replacement.hash, shader ? shader->stage : GetStage(file), shader ? shader->name : std::string_view());
   ImGui::SameLine();
   if (!shader)
      Widgets::TextDim("not created by the game yet");
   else if (!replacement.compiled)
      Widgets::Text("not replaced, it didn't compile", Tone::Error);
   else
      Widgets::Text(std::format("replaced, {} object{} in the game{}", shader->objects.load(), shader->objects == 1 ? "" : "s",
                       replacement.failed ? " (the last version that compiled)" : ""),
         replacement.failed ? Tone::Warning : Tone::Success);
   if (!replacement.messages.empty())
      Widgets::Code(replacement.messages);
}

void DrawLumaShader(const LumaShader& shader)
{
   const Resources::Database::Entry& entry = *shader.entry;
   const Resources::Database::ShaderState& state = shader.state;
   Widgets::Property("Name", entry.name);
   Widgets::Property("Lookup", std::format("\"{}\"_h = 0x{:016X}", entry.name, entry.name_hash));
   Widgets::Property("File", entry.file);
   if (state.compiled)
      Widgets::Text(std::format("{} bytes, {} requested resource{}{}", state.compiled->bytecode.size(), state.compiled->requests.size(),
                       state.compiled->requests.size() == 1 ? "" : "s", state.failed ? " (the last version that compiled)" : ""),
         state.failed ? Tone::Warning : Tone::Success);
   else
      Widgets::Text("not compiled", Tone::Error);
   if (!state.messages.empty())
      Widgets::Code(state.messages);
}

void DrawToolbar()
{
   if (Widgets::Button("Reload", Widgets::ButtonKind::Primary, "Compiles what changed (from the cache when it can) and swaps the replacements in"))
      Shaders::Reload();
   ImGui::SameLine();
   const bool loaded = Shaders::Variants::IsLoaded();
   if (Widgets::Button(loaded ? "Unload" : "Load", Widgets::ButtonKind::Secondary,
          loaded ? "The game's own shaders, the replacements kept for Load" : "The replacements back in, and a reload"))
      loaded ? Shaders::Unload() : Shaders::Load();
   ImGui::SameLine();
   const Shaders::BuildInfo& build = Shaders::GetLastBuild();
   if (build.id)
      Widgets::Text(std::format("{} compiled, {} from the cache, {} failed", build.compiled, build.cached, build.failed), build.failed ? Tone::Error : Tone::Info);
   const System::Jobs::Status status = System::Jobs::GetStatus();
   if (!status.name.empty())
      Widgets::ProgressBar(status.total ? float(status.done) / float(status.total) : -1.f, std::format("{} {}/{}", status.name, status.done, status.total));
}
} // namespace

void DrawShaders()
{
   DrawToolbar();

   // Replacements grouped by file, then the custom shaders
   std::map<std::string, Entry> files;
   for (Shaders::Variants::Variant& replacement : Shaders::Variants::GetAll())
   {
      Entry& entry = files[replacement.file];
      entry.file = replacement.file;
      entry.replacements.push_back(std::move(replacement));
   }
   std::vector<LumaShader> luma_shaders;
   for (uint32_t index = 0; index < Resources::Database::GetCount(); index++)
   {
      if (const Resources::Database::Entry& entry = Resources::Database::Get(index); entry.kind == Resources::Database::Kind::Shader)
         luma_shaders.push_back({&entry, Resources::Database::GetShader(index)});
   }
   std::vector<Entry> entries;
   for (auto& [file, entry] : files)
      entries.push_back(std::move(entry));
   for (const LumaShader& shader : luma_shaders)
      entries.push_back({shader.entry->file, {}, &shader});
   std::vector<Widgets::ListEntry> list;
   for (const Entry& entry : entries)
   {
      const bool failed = entry.luma_shader ? entry.luma_shader->state.failed : std::ranges::any_of(entry.replacements, &Shaders::Variants::Variant::failed);
      const std::string label = entry.luma_shader ? std::format("{} ({})", entry.luma_shader->entry->name, entry.file) : entry.file;
      list.push_back({label, failed ? Tone::Error : Tone::Info});
   }

   if (Widgets::Section("Files"))
   {
      Widgets::ListDetail("Files", list, selected, [&](size_t index) {
         const Entry& entry = entries[index];
         if (entry.luma_shader)
            return DrawLumaShader(*entry.luma_shader);
         Widgets::Property("File", entry.file);
         for (const Shaders::Variants::Variant& replacement : entry.replacements)
            DrawReplacement(replacement, entry.file); }, ImGui::GetTextLineHeightWithSpacing() * 16.f);
      Widgets::EndSection();
   }

   // What runs replaced now, by stage
   std::vector<const Shaders::Database::Shader*> replaced;
   for (const Shaders::Database::Shader* shader : Shaders::Database::GetShaders())
   {
      if (shader->objects && Shaders::Variants::HasVariant(shader->hash))
         replaced.push_back(shader);
   }
   std::ranges::sort(replaced, [](const auto* a, const auto* b) { return std::pair(a->stage, a->hash) < std::pair(b->stage, b->hash); });
   if (Widgets::Section("Replaced in the Game", false))
   {
      Widgets::Caption(std::format("{} shader{} running a replacement", replaced.size(), replaced.size() == 1 ? "" : "s"));
      float line_used = 0.f;
      for (const Shaders::Database::Shader* shader : replaced)
      {
         line_used = Widgets::WrapSameLine(line_used, ImGui::CalcTextSize("0x00000000").x + ImGui::GetStyle().FramePadding.x * 2.f);
         Widgets::HashChip(shader->hash, shader->stage, shader->name);
      }
      if (!Shaders::Variants::IsLoaded())
         Widgets::TextDim("Unloaded: the game's own shaders run");
      Widgets::EndSection();
   }

   const Shaders::BuildInfo& build = Shaders::GetLastBuild();
   if (!build.errors.empty() && Widgets::Section("Compile Errors"))
   {
      Widgets::Code(build.errors);
      Widgets::EndSection();
   }
}
} // namespace Panels
} // namespace UI
} // namespace Luma
#endif
