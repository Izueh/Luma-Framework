#include "Shaders/D3D12/D3D12Variants.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <optional>
#include <span>
#include <utility>
#include <format>
#include <mutex>
#include <ranges>
#include <set>
#include <string>
#include <vector>

#include <wrl/client.h>

#include "Hooks/D3D12/D3D12Lifetime.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Hooks/HookScope.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Resources/ResourceBindings.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/D3D12/D3D12ResourceDatabase.hpp"
#include "Resources/ResourceDatabase.hpp"
#include "Shaders/D3D12/D3D12PipelineLibrary.hpp"
#include "Shaders/ShaderPatching.hpp"
#include "Shaders/ShaderStore.hpp"
#include "System/JobQueue.hpp"
#include "Utils/Hash.hpp"
#include "Utils/Debug.hpp"
#include "Log/Log.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Shaders {
namespace Variants {
namespace D3D12 {
namespace {
using Key = std::array<std::shared_ptr<const CompiledShader>, 6>;
using Database::D3D12::TargetFormats;

// Part of every name in Luma's pipeline library: a change of what a name covers makes old entries unreachable instead of mismatched
constexpr uint32_t NAME_VERSION = 2;

std::atomic<uint32_t> format_variant_count = 0;
std::atomic<uint32_t> warmed_count = 0;
std::atomic<uint32_t> without_desc_count = 0;

// A pipeline with the formats of the mirrors it draws into, made from the game's desc or a variant's
struct FormatVariant
{
   std::shared_ptr<const Built> built; // Null for the game's shaders
   TargetFormats formats;
   Hooks::Lifetime::D3D12::Object object;

   FormatVariant() { format_variant_count++; }

   ~FormatVariant() { format_variant_count--; }
};

// A game pipeline's variant. Shared by the pipeline's private data, the registry (weakly) and the jobs making it, so a job can
// outlive the pipeline.
struct Core
{
   std::shared_ptr<Store> store;
   // Of the device's variants, when this last chose (presenting thread)
   uint64_t applied_generation = 0;
   std::shared_ptr<const Database::D3D12::DescCopy> desc;
   std::array<const Database::Shader*, 6> shaders = {};
   ComPtr<ID3D12RootSignature> root_signature; // The desc names it
   ComPtr<ID3D12Device> device;
   bool compute = false;
   bool extended = false; // Its signature has Luma's parameters: shader variants can run with it
   bool luma_table = false; // Its signature has Luma's texture table
   uint64_t key = 0; // The game's desc and bytecode, for the format sets recorded across boots
   // What binds take: the variant pipeline (lists hold it until they've executed) and what it was made from
   Hooks::Lifetime::D3D12::Object object;
   std::atomic<std::shared_ptr<const Built>> built;

   std::mutex mutex; // Never held with another
   Key current; // What the published pipeline (or none) was made from
   bool queued = false; // A job makes one
   bool update_again = false; // The choice changed while it did
   bool failed = false;

   std::mutex format_mutex; // Never held with another
   std::vector<std::unique_ptr<FormatVariant>> format_variants;
   bool format_logged = false;
};

// In the game pipeline's private data
struct Holder
{
   std::shared_ptr<Core> core;
};

// What a list has bound
struct ListRecord
{
   std::shared_ptr<const Built> bound;
   // The game's pipeline, which the game holds while the list may use it
   ID3D12PipelineState* game = nullptr;
   const Database::D3D12::Pipeline* tracked = nullptr;
   // Format variants: the formats of the mirrors bound as targets (UNKNOWN for the game's own), checked at the next draw when dirty
   TargetFormats target_formats;
   bool targets_mirrored = false;
   bool formats_dirty = false;
   bool format_variant_bound = false;
};

// Never held with another lock
std::mutex registry_mutex;
std::vector<std::weak_ptr<Core>> registry;
std::atomic<uint32_t> pending_count = 0;
std::atomic<uint32_t> failed_count = 0;
// Every device's variant generations added up (presenting thread): nothing to choose again while it holds
uint64_t applied_generations = UINT64_MAX;

bool Same(const Key& a, const Key& b)
{
   for (size_t i = 0; i < a.size(); i++)
   {
      if (a[i] != b[i] && (!a[i] || !b[i] || a[i]->bytecode != b[i]->bytecode))
         return false;
   }
   return true;
}

// Entries and shaders already reported by ReportUnbound. Never held with another lock.
std::mutex unbound_mutex;
std::set<std::pair<uint32_t, uint32_t>> unbound_reported;

void ReportUnbound(uint32_t entry, const Database::Shader* shader)
{
   const Resources::Database::Entry& described = Resources::Database::Get(entry);
   if (described.kind == Resources::Database::Kind::Texture || Resources::LumaCBuffers::GetRootIndex(entry))
      return;
   const uint32_t hash = shader ? shader->hash : 0;
   {
      std::scoped_lock lock(unbound_mutex);
      if (!unbound_reported.emplace(entry, hash).second)
         return;
   }
   ASSERT_ONCE_MSG(false, std::format("unimplemented: Resources::Database shaders as D3D12 bindings (\"{}\" requested by 0x{:08X})", described.name, hash));
   Log::Warning("D3D12: 0x{:08X} requests Luma's \"{}\", which D3D12 doesn't bind yet (Resources::Database shaders as D3D12 bindings): it reads nothing there",
      hash, described.name);
}

bool ReadsTextures(const CompiledShader& compiled)
{
   return std::ranges::any_of(compiled.requests, [](const Request& request) { return Resources::Database::Get(request.entry).kind == Resources::Database::Kind::Texture; });
}

// Per stage, the variant a bind takes: the forced one (trace), else the highest-priority one that compiled. Null for the game's. One
// that reads Luma's textures can't run with a signature that has no table for them.
Key Choose(const Core& core, std::array<Source, 6>& sources)
{
   Key key;
   if (!core.extended)
      return key;
   for (size_t i = 0; i < core.shaders.size(); i++)
   {
      const Database::Shader* shader = core.shaders[i];
      if (!shader)
         continue;
      Choice forced = Choice::Auto;
#if DEVELOPMENT
      forced = shader->forced_variant.load(std::memory_order_relaxed);
#endif
      if (forced == Choice::Original)
         continue;
      for (const Variant& variant : GetVariants(*core.store, shader->hash))
      {
         if (!variant.compiled || (forced != Choice::Auto && GetChoice(variant.source) != forced))
            continue;
         if (!core.luma_table && ReadsTextures(*variant.compiled))
            continue;
         key[i] = variant.compiled;
         sources[i] = variant.source;
         break;
      }
   }
   return key;
}

// The desc with the variants' bytecode in place of the game's
std::vector<std::byte> MakeDesc(const Database::D3D12::DescCopy& desc, const Key& key)
{
   std::vector<std::byte> bytes = desc.bytes;
   for (size_t i = 0; i < key.size(); i++)
   {
      if (!key[i] || desc.stages[i] == SIZE_MAX)
         continue;
      const D3D12_SHADER_BYTECODE bytecode = {key[i]->bytecode.data(), key[i]->bytecode.size()};
      std::memcpy(bytes.data() + desc.stages[i], &bytecode, sizeof(bytecode));
   }
   return bytes;
}

// What the native call takes: the struct, or a stream desc over the bytes (made at the call: a copy moves the bytes)
struct NativeDesc
{
   std::vector<std::byte> bytes;
   D3D12_PIPELINE_STATE_STREAM_DESC stream = {};
};

const void* GetNativeDesc(NativeDesc& native, Database::D3D12::PipelineKind kind)
{
   native.stream = {native.bytes.size(), native.bytes.data()};
   return kind == Database::D3D12::PipelineKind::Stream ? static_cast<const void*>(&native.stream) : native.bytes.data();
}

NativeDesc MakeNative(const Database::D3D12::DescCopy& desc, const Key& key)
{
   return {MakeDesc(desc, key)};
}

// Every stage of the pipeline, the replaced ones marked: a refusal is often a mismatch between two stages
std::string DescribeStages(const Core& core, const auto& key)
{
   std::string text;
   for (size_t i = 0; i < core.shaders.size(); i++)
   {
      if (!core.shaders[i])
         continue;
      text += std::format("{}0x{:08X}{}", text.empty() ? "" : ", ", core.shaders[i]->hash, key[i] ? " (replaced)" : "");
   }
   return text;
}

uint32_t GetStagesHash(const Database::D3D12::DescCopy& desc, const std::vector<std::byte>& bytes)
{
   uint32_t stages_hash = 0;
   for (const size_t offset : desc.stages)
   {
      if (offset == SIZE_MAX)
         continue;
      D3D12_SHADER_BYTECODE bytecode;
      std::memcpy(&bytecode, bytes.data() + offset, sizeof(bytecode));
      stages_hash = Utils::Hash::CRC32({static_cast<const uint8_t*>(bytecode.pShaderBytecode), bytecode.BytecodeLength}, stages_hash);
   }
   return stages_hash;
}

// Stable across boots: the desc's contents (Luma's root signature extension included) and every stage's bytecode
std::wstring GetLibraryName(const Database::D3D12::DescCopy& desc, const std::vector<std::byte>& bytes)
{
   return std::format(L"Luma{}_{:016X}{:08X}", NAME_VERSION, desc.content_hash, GetStagesHash(desc, bytes));
}

std::wstring GetFormatName(const Database::D3D12::DescCopy& desc, const std::vector<std::byte>& bytes, const TargetFormats& formats)
{
   return std::format(L"{}F{:08X}D{:02X}", GetLibraryName(desc, bytes),
      Utils::Hash::CRC32({reinterpret_cast<const uint8_t*>(formats.render_targets.data()), sizeof(formats.render_targets)}), uint32_t(formats.depth));
}

ComPtr<ID3D12PipelineState> Create(ID3D12Device* device, Database::D3D12::PipelineKind kind, const void* desc)
{
   Hooks::Scope::Guard scope;
   ComPtr<ID3D12PipelineState> pipeline;
   HRESULT result = E_FAIL;
   switch (kind)
   {
   case Database::D3D12::PipelineKind::Graphics:
      result = device->CreateGraphicsPipelineState(static_cast<const D3D12_GRAPHICS_PIPELINE_STATE_DESC*>(desc), IID_PPV_ARGS(&pipeline));
      break;
   case Database::D3D12::PipelineKind::Compute:
      result = device->CreateComputePipelineState(static_cast<const D3D12_COMPUTE_PIPELINE_STATE_DESC*>(desc), IID_PPV_ARGS(&pipeline));
      break;
   case Database::D3D12::PipelineKind::Stream:
      if (ComPtr<ID3D12Device2> device2; SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device2))))
         result = device2->CreatePipelineState(static_cast<const D3D12_PIPELINE_STATE_STREAM_DESC*>(desc), IID_PPV_ARGS(&pipeline));
      break;
   }
   return SUCCEEDED(result) ? pipeline : nullptr;
}

// Where a graphics desc keeps its target formats (D3D12_GRAPHICS_PIPELINE_STATE_DESC's fields, or a stream's subobjects): their
// offsets in the bytes, SIZE_MAX for what it doesn't have
struct FormatsAt
{
   size_t formats = SIZE_MAX; // DXGI_FORMAT[8]
   size_t count = SIZE_MAX; // UINT
   size_t depth = SIZE_MAX; // DXGI_FORMAT
};

std::optional<FormatsAt> FindFormats(Database::D3D12::PipelineKind kind, const std::vector<std::byte>& bytes)
{
   using Desc = D3D12_GRAPHICS_PIPELINE_STATE_DESC;
   if (kind == Database::D3D12::PipelineKind::Graphics)
      return FormatsAt{offsetof(Desc, RTVFormats), offsetof(Desc, NumRenderTargets), offsetof(Desc, DSVFormat)};
   if (kind != Database::D3D12::PipelineKind::Stream)
      return std::nullopt;
   FormatsAt at;
   size_t offset = 0;
   while (offset + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) <= bytes.size())
   {
      D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
      std::memcpy(&type, bytes.data() + offset, sizeof(type));
      const std::optional<Database::D3D12::SubobjectLayout> layout = Database::D3D12::GetSubobjectLayout(type);
      if (!layout)
         return std::nullopt;
      const size_t value = offset + layout->value_offset;
      if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS)
      {
         at.formats = value + offsetof(D3D12_RT_FORMAT_ARRAY, RTFormats);
         at.count = value + offsetof(D3D12_RT_FORMAT_ARRAY, NumRenderTargets);
      }
      else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT)
         at.depth = value;
      offset += layout->size;
   }
   if (at.formats == SIZE_MAX && at.depth == SIZE_MAX)
      return std::nullopt;
   return at;
}

// The formats the pipeline's targets have: its own where the target isn't a mirror. None when they're all its own already.
std::optional<TargetFormats> GetNeededFormats(const Core& core, const TargetFormats& targets)
{
   const std::optional<FormatsAt> at = FindFormats(core.desc->kind, core.desc->bytes);
   if (!at)
      return std::nullopt;
   const std::byte* bytes = core.desc->bytes.data();
   TargetFormats formats;
   bool changed = false;
   if (at->formats != SIZE_MAX)
   {
      UINT count = 0;
      std::memcpy(formats.render_targets.data(), bytes + at->formats, sizeof(formats.render_targets));
      std::memcpy(&count, bytes + at->count, sizeof(count));
      for (UINT i = 0; i < std::min<UINT>(count, 8); i++)
      {
         if (targets.render_targets[i] != DXGI_FORMAT_UNKNOWN && targets.render_targets[i] != formats.render_targets[i])
         {
            formats.render_targets[i] = targets.render_targets[i];
            changed = true;
         }
      }
   }
   if (at->depth != SIZE_MAX)
   {
      std::memcpy(&formats.depth, bytes + at->depth, sizeof(formats.depth));
      // A pipeline without a depth format doesn't use the depth bound
      if (targets.depth != DXGI_FORMAT_UNKNOWN && formats.depth != DXGI_FORMAT_UNKNOWN && targets.depth != formats.depth)
      {
         formats.depth = targets.depth;
         changed = true;
      }
   }
   return changed ? std::optional(formats) : std::nullopt;
}

ComPtr<ID3D12PipelineState> Create(ID3D12Device* device, Database::D3D12::PipelineKind kind, const void* desc);
std::wstring GetLibraryName(const Database::D3D12::DescCopy& desc, const std::vector<std::byte>& bytes);
std::vector<std::byte> MakeDesc(const Database::D3D12::DescCopy& desc, const Key& key);

std::optional<NativeDesc> MakeFormatDesc(const Core& core, const std::shared_ptr<const Built>& built, const TargetFormats& formats, std::wstring& name)
{
   std::vector<std::byte> bytes = built ? MakeDesc(*core.desc, built->compiled) : core.desc->bytes;
   const std::optional<FormatsAt> at = FindFormats(core.desc->kind, bytes);
   if (!at)
      return std::nullopt;
   if (at->formats != SIZE_MAX)
      std::memcpy(bytes.data() + at->formats, formats.render_targets.data(), sizeof(formats.render_targets));
   if (at->depth != SIZE_MAX)
      std::memcpy(bytes.data() + at->depth, &formats.depth, sizeof(formats.depth));
   name = GetFormatName(*core.desc, bytes, formats);
   return NativeDesc{std::move(bytes)};
}

FormatVariant* FindFormatVariant(Core& core, const std::shared_ptr<const Built>& built, const TargetFormats& formats)
{
   for (const std::unique_ptr<FormatVariant>& variant : core.format_variants)
   {
      if (variant->built == built && variant->formats == formats)
         return variant.get();
   }
   return nullptr;
}

FormatVariant* AddFormatVariant(Core& core, const std::shared_ptr<const Built>& built, const TargetFormats& formats, ComPtr<ID3D12PipelineState> pipeline)
{
   auto variant = std::make_unique<FormatVariant>();
   variant->built = built;
   variant->formats = formats;
   Hooks::Lifetime::D3D12::Replace(variant->object, std::move(pipeline));
   core.format_variants.push_back(std::move(variant));
   return core.format_variants.back().get();
}

FormatVariant* GetFormatVariant(Core& core, const std::shared_ptr<const Built>& built, const TargetFormats& formats)
{
   std::scoped_lock lock(core.format_mutex);
   if (FormatVariant* found = FindFormatVariant(core, built, formats))
      return found->object.current.load(std::memory_order_relaxed) ? found : nullptr;
   // Drop variants for obsolete shader choices
   const std::shared_ptr<const Built> current = core.built.load(std::memory_order_acquire);
   std::erase_if(core.format_variants, [&](const std::unique_ptr<FormatVariant>& variant) { return variant->built && variant->built != current; });
   std::wstring name;
   std::optional<NativeDesc> native = MakeFormatDesc(core, built, formats, name);
   if (!native)
      return nullptr;
   ComPtr<ID3D12PipelineState> pipeline = PipelineLibrary::D3D12::Load(core.device.Get(), name, core.desc->kind, GetNativeDesc(*native, core.desc->kind));
   if (!pipeline)
   {
      if (!std::exchange(core.format_logged, true))
         Log::Info("D3D12: the pipeline of {} draws into mirrors: its variant with their formats is made at the draw", DescribeStages(core, Key{}));
      pipeline = Create(core.device.Get(), core.desc->kind, GetNativeDesc(*native, core.desc->kind));
      if (pipeline)
         PipelineLibrary::D3D12::Store(core.device.Get(), name, pipeline.Get());
      else
         Log::Error("D3D12: the device refused a pipeline with its mirrors' target formats, the game's runs (wrong formats)");
   }
   if (pipeline)
      PipelineLibrary::D3D12::RecordFormats(core.key, formats);
   FormatVariant* variant = AddFormatVariant(core, built, formats, std::move(pipeline));
   return variant->object.current.load(std::memory_order_relaxed) ? variant : nullptr;
}

void WarmFormatVariants(const std::shared_ptr<Core>& core)
{
   if (core->compute || !Resources::Mirrors::IsNeeded())
      return;
   const std::shared_ptr<const Built> built = core->built.load(std::memory_order_acquire);
   for (const TargetFormats& formats : PipelineLibrary::D3D12::GetFormats(core->device.Get(), core->key))
   {
      {
         std::scoped_lock lock(core->format_mutex);
         if (FindFormatVariant(*core, built, formats))
            continue;
      }
      std::wstring name;
      std::optional<NativeDesc> native = MakeFormatDesc(*core, built, formats, name);
      if (!native)
         continue;
      if (ComPtr<ID3D12PipelineState> loaded = PipelineLibrary::D3D12::Load(core->device.Get(), name, core->desc->kind, GetNativeDesc(*native, core->desc->kind)))
      {
         std::scoped_lock lock(core->format_mutex);
         if (!FindFormatVariant(*core, built, formats))
         {
            AddFormatVariant(*core, built, formats, std::move(loaded));
            warmed_count++;
         }
         continue;
      }
      auto made = std::make_shared<ComPtr<ID3D12PipelineState>>();
      System::Jobs::Queue(
         "Creating pipelines",
         [core, made, native = std::move(*native), name]() mutable {
            *made = Create(core->device.Get(), core->desc->kind, GetNativeDesc(native, core->desc->kind));
            if (*made)
               PipelineLibrary::D3D12::Store(core->device.Get(), name, made->Get());
         },
         [core, made, built, formats] {
            std::scoped_lock lock(core->format_mutex);
            if (*made && !FindFormatVariant(*core, built, formats))
            {
               AddFormatVariant(*core, built, formats, std::move(*made));
               warmed_count++;
            }
            return System::Jobs::Outcome{};
         },
         System::Jobs::Priority::Low);
   }
}

void Update(const std::shared_ptr<Core>& core);

// Any thread: `pipeline` (null if it couldn't be made) is what binds take from now on
void Publish(const std::shared_ptr<Core>& core, Key key, const std::array<Source, 6>& sources, ComPtr<ID3D12PipelineState> pipeline)
{
   bool update_again = false;
   {
      std::scoped_lock lock(core->mutex);
      if (core->queued)
         pending_count--;
      core->queued = false;
      update_again = std::exchange(core->update_again, false);
      core->current = key;
      std::shared_ptr<const Built> built;
      if (pipeline)
      {
         auto made = std::make_shared<Built>();
         made->compute = core->compute;
         made->object = pipeline.Get();
         for (size_t i = 0; i < key.size(); i++)
         {
            if (!key[i])
               continue;
            made->variant_stages.Set(ShaderStage(i));
            made->sources[i] = sources[i];
            for (const Request& request : key[i]->requests)
            {
               if (!std::ranges::contains(made->entries, request.entry))
                  made->entries.push_back(request.entry);
               ReportUnbound(request.entry, core->shaders[i]);
            }
         }
         // Luma's textures by register of its table: one layout for the pipeline's stages
         Resources::Database::D3D12::TableLayout layout;
         layout.fill(UINT32_MAX);
         bool textures = false;
         for (const auto& compiled : key)
         {
            for (const Request& request : compiled ? compiled->requests : std::vector<Request>())
            {
               if (Resources::Database::Get(request.entry).kind != Resources::Database::Kind::Texture)
                  continue;
               // Only Luma's table is bound on D3D12: a texture elsewhere (a shader built without LUMA_TEXTURE_SPACE) isn't
               const uint32_t table_index = request.slot - Resources::Bindings::D3D12_FIRST_TEXTURE_REGISTER;
               if (request.slot < Resources::Bindings::D3D12_FIRST_TEXTURE_REGISTER || table_index >= layout.size())
               {
                  ASSERT_ONCE_MSG(false, "A Luma texture requested outside Luma's D3D12 table");
                  Log::Error("D3D12: a variant reads Luma's texture \"{}\" at t{}, outside Luma's table (t{} to t{}): not bound",
                     Resources::Database::Get(request.entry).name, request.slot, Resources::Bindings::D3D12_FIRST_TEXTURE_REGISTER,
                     Resources::Bindings::D3D12_FIRST_TEXTURE_REGISTER + uint32_t(layout.size()) - 1);
                  continue;
               }
               layout[table_index] = request.entry;
               textures = true;
            }
         }
         made->textures = textures;
         if (textures)
            made->table = Resources::Database::D3D12::RegisterTable(layout);
         made->compiled = std::move(key);
         built = std::move(made);
         if (core->failed)
            failed_count--;
         core->failed = false;
      }
      else if (std::ranges::any_of(key, [](const auto& compiled) { return compiled != nullptr; }) && !core->failed)
      {
         core->failed = true;
         failed_count++;
      }
      // The pipeline first: a bind that reads the new record finds it, one that reads the old one finds a mismatch and retries
      Hooks::Lifetime::D3D12::Replace(core->object, std::move(pipeline));
      core->built.store(std::move(built), std::memory_order_release);
   }
   WarmFormatVariants(core);
   if (update_again)
      Update(core);
}

// Any thread: makes the pipeline of the choice now, if it changed
void Update(const std::shared_ptr<Core>& core)
{
   std::array<Source, 6> sources = {};
   Key key = Choose(*core, sources);
   {
      std::scoped_lock lock(core->mutex);
      if (core->queued)
      {
         core->update_again = true;
         return;
      }
      if (Same(key, core->current))
         return;
      core->queued = true;
      pending_count++;
   }
   if (std::ranges::none_of(key, [](const auto& compiled) { return compiled != nullptr; }))
   {
      Publish(core, std::move(key), sources, nullptr);
      return;
   }
   NativeDesc native = MakeNative(*core->desc, key);
   const std::wstring name = GetLibraryName(*core->desc, native.bytes);
   if (ComPtr<ID3D12PipelineState> loaded = PipelineLibrary::D3D12::Load(core->device.Get(), name, core->desc->kind, GetNativeDesc(native, core->desc->kind)))
   {
      Publish(core, std::move(key), sources, std::move(loaded));
      return;
   }
   auto made = std::make_shared<ComPtr<ID3D12PipelineState>>();
   std::string stages = DescribeStages(*core, key);
   System::Jobs::Queue(
      "Creating pipelines",
      [core, made, native = std::move(native), name, stages = std::move(stages)]() mutable {
         *made = Create(core->device.Get(), core->desc->kind, GetNativeDesc(native, core->desc->kind));
         if (*made)
            PipelineLibrary::D3D12::Store(core->device.Get(), name, made->Get());
         else
            Log::Error("D3D12: the device refused a variant pipeline with {}, the game's runs", stages);
      },
      [core, made, key = std::move(key), sources] {
         Publish(core, key, sources, std::move(*made));
         return System::Jobs::Outcome{};
      });
}

ListRecord* GetRecord(ID3D12GraphicsCommandList* list)
{
   return Hooks::Lifetime::GetOrCreateStateCached<ListRecord>(list);
}
} // namespace

bool NeedsDescCopy([[maybe_unused]] const Database::D3D12::PipelineDesc& desc)
{
#if DEVELOPMENT || TEST
   return true;
#else
   // Any shader may get a patch: which ones is known only once they're patched, after the creation. Any graphics pipeline may draw
   // into a mirror (a format variant).
   if (Patching::IsNeeded() || (Resources::Mirrors::IsNeeded() && desc.kind != Database::D3D12::PipelineKind::Compute))
      return true;
   return std::ranges::any_of(desc.stages, [](std::span<const uint8_t> bytecode) { return !bytecode.empty() && HasReplacementFile(Utils::Hash::CRC32(bytecode)); });
#endif
}

void AttachVariants(ID3D12PipelineState* pipeline, const Database::D3D12::Pipeline& tracked)
{
   if (!tracked.desc)
      return;
   // A variant's shaders read Luma's registers, which only an extended signature declares. Format variants need no more than the desc.
   const bool extended = tracked.root_layout && tracked.root_layout->luma_parameter != UINT32_MAX;
   if (!extended && !Resources::Mirrors::IsNeeded())
      return;
   auto core = std::make_shared<Core>();
   core->extended = extended;
   core->desc = tracked.desc;
   core->root_signature = tracked.root_signature;
   core->compute = tracked.shaders[size_t(ShaderStage::Compute)] != nullptr;
   core->luma_table = extended && tracked.root_layout->luma_table;
   for (size_t i = 0; i < tracked.shaders.size(); i++)
      core->shaders[i] = tracked.shaders[i];
   core->key = tracked.desc->content_hash ^ (uint64_t(GetStagesHash(*tracked.desc, tracked.desc->bytes)) * 0x9E3779B97F4A7C15ull);
   if (FAILED(pipeline->GetDevice(IID_PPV_ARGS(&core->device))))
      return;
   core->store = GetStore(core->device.Get());
   if (!core->store || !Hooks::Lifetime::SetState<Holder>(pipeline, core))
      return;
   core->applied_generation = GetGeneration(*core->store);
   {
      std::scoped_lock lock(registry_mutex);
      registry.push_back(core);
   }
   Update(core);
   // Update publishes, and warms, only for a pipeline with a variant
   if (!core->built.load(std::memory_order_acquire))
      WarmFormatVariants(core);
}

void SwapInVariant(ID3D12GraphicsCommandList* list, ID3D12PipelineState*& pipeline)
{
   ListRecord* record = GetRecord(list);
   if (!record)
      return;
   record->bound.reset();
   record->game = pipeline;
   record->tracked = pipeline ? Database::D3D12::FindPipeline(pipeline) : nullptr;
   record->formats_dirty = record->targets_mirrored;
   record->format_variant_bound = false;
   const Holder* holder = pipeline ? Hooks::Lifetime::GetState<Holder>(pipeline) : nullptr;
   if (!holder || !Settings::IsEnabled() || !IsLoaded())
      return;
   Core& core = *holder->core;
   std::shared_ptr<const Built> built = core.built.load(std::memory_order_acquire);
   if (!built)
      return;
   IUnknown* held = Hooks::Lifetime::D3D12::Reference(list, core.object);
   // Replaced between the two reads: the record that goes with the pipeline the list now holds
   if (held != built->object)
   {
      built = core.built.load(std::memory_order_acquire);
      if (!built || held != built->object)
         return;
   }
   pipeline = static_cast<ID3D12PipelineState*>(held);
   record->bound = std::move(built);
}

void ForgetBound(ID3D12GraphicsCommandList* list)
{
   if (ListRecord* record = GetRecord(list))
      *record = {};
}

const Built* GetBound(ID3D12GraphicsCommandList* list)
{
   const ListRecord* record = GetRecord(list);
   return record ? record->bound.get() : nullptr;
}

BoundPipeline GetBoundPipeline(ID3D12GraphicsCommandList* list)
{
   const ListRecord* record = GetRecord(list);
   if (!record)
      return {};
   return {record->game, record->tracked, record->bound.get()};
}

void UseOriginal(ID3D12GraphicsCommandList* list)
{
   ListRecord* record = GetRecord(list);
   if (!record || !record->bound || !record->game)
      return;
   {
      Hooks::Scope::Guard scope;
      list->SetPipelineState(record->game);
   }
   record->bound.reset();
   record->formats_dirty = record->targets_mirrored;
   record->format_variant_bound = false;
}

void RestorePipeline(ID3D12GraphicsCommandList* list)
{
   ListRecord* record = GetRecord(list);
   if (!record || !record->game)
      return;
   {
      Hooks::Scope::Guard scope;
      list->SetPipelineState(record->bound ? static_cast<ID3D12PipelineState*>(record->bound->object) : record->game);
   }
   record->format_variant_bound = false;
   record->formats_dirty = record->targets_mirrored;
}

void SetTargetFormats(ID3D12GraphicsCommandList* list, const TargetFormats& formats)
{
   ListRecord* record = GetRecord(list);
   if (!record)
      return;
   const bool mirrored = formats.Any();
   if (!mirrored && !record->targets_mirrored && !record->format_variant_bound)
      return;
   record->target_formats = formats;
   record->targets_mirrored = mirrored;
   record->formats_dirty = true;
}

void BindFormatVariant(ID3D12GraphicsCommandList* list)
{
   ListRecord* record = GetRecord(list);
   if (!record || !record->formats_dirty)
      return;
   record->formats_dirty = false;
   const Holder* holder = record->game ? Hooks::Lifetime::GetState<Holder>(record->game) : nullptr;
   Core* core = holder && !holder->core->compute ? holder->core.get() : nullptr;
   const std::optional<TargetFormats> formats = core && record->targets_mirrored ? GetNeededFormats(*core, record->target_formats) : std::nullopt;
   ID3D12PipelineState* pipeline = nullptr;
   if (formats)
   {
      if (FormatVariant* variant = GetFormatVariant(*core, record->bound, *formats))
         pipeline = static_cast<ID3D12PipelineState*>(Hooks::Lifetime::D3D12::Reference(list, variant->object));
   }
   else if (record->targets_mirrored && !holder && record->game)
   {
      without_desc_count++;
      LUMA_UNIMPLEMENTED("Format variants of a pipeline whose desc wasn't copied (made before Luma, or from a state object)");
   }
   if (pipeline)
   {
      Hooks::Scope::Guard scope;
      list->SetPipelineState(pipeline);
      record->format_variant_bound = true;
      return;
   }
   // Back to what the game's bind gave
   if (std::exchange(record->format_variant_bound, false))
   {
      Hooks::Scope::Guard scope;
      list->SetPipelineState(record->bound ? static_cast<ID3D12PipelineState*>(record->bound->object) : record->game);
   }
}

void UpdateVariants()
{
   const uint64_t any_generation = GetAnyGeneration();
   if (any_generation == applied_generations)
      return;
   applied_generations = any_generation;
   std::vector<std::shared_ptr<Core>> cores;
   {
      std::scoped_lock lock(registry_mutex);
      std::erase_if(registry, [&](const std::weak_ptr<Core>& weak) {
         std::shared_ptr<Core> core = weak.lock();
         if (!core)
            return true;
         cores.push_back(std::move(core));
         return false;
      });
   }
   for (const std::shared_ptr<Core>& core : cores)
   {
      const uint64_t generation = GetGeneration(*core->store);
      if (generation == core->applied_generation)
         continue;
      core->applied_generation = generation;
      Update(core);
   }
}

Stats GetStats()
{
   Stats stats;
   std::vector<std::shared_ptr<Core>> cores;
   {
      std::scoped_lock lock(registry_mutex);
      for (const std::weak_ptr<Core>& weak : registry)
      {
         if (std::shared_ptr<Core> core = weak.lock())
            cores.push_back(std::move(core));
      }
   }
   stats.pipelines = uint32_t(cores.size());
   for (const std::shared_ptr<Core>& core : cores)
      stats.ready += core->built.load(std::memory_order_acquire) != nullptr;
   stats.pending = pending_count.load();
   stats.failed = failed_count.load();
   stats.format_variants = format_variant_count.load();
   stats.warmed_format_variants = warmed_count.load();
   stats.draws_without_desc = without_desc_count.load();
   return stats;
}
} // namespace D3D12
} // namespace Variants
} // namespace Shaders
} // namespace Luma
