#if DEVELOPMENT
#include "Trace/D3D12/D3D12Stats.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <Windows.h>

#include "Hooks/Lifetime.hpp"
#include "Resources/D3D12/D3D12ResourceDatabase.hpp"
#include "Shaders/Container.hpp"
#include "Shaders/D3D12/D3D12PipelineLibrary.hpp"
#include "Shaders/D3D12/D3D12Variants.hpp"
#include "Shaders/Variants.hpp"
#include "System/Jobs.hpp"
#include "System/Paths.hpp"
#include "Utils/Hash.hpp"
#include "Utils/Log.hpp"

namespace Luma {
namespace Trace {
namespace Stats {
namespace D3D12 {
namespace {
using Shaders::Database::D3D12::Pipeline;
using Shaders::Database::D3D12::PipelineDesc;
using Shaders::Database::D3D12::PipelineKind;
using Shaders::Database::D3D12::RootLayout;
using Shaders::Database::D3D12::RootParameter;

enum class Counter : uint8_t
{
   ViewWritesShaderVisible,
   ViewWritesCpu,
   CopyCallsShaderVisible,
   CopiedShaderVisible,
   CopyCallsCpu,
   CopiedCpu,
   Draws,
   Dispatches,
   ExecutedLists,
   ExecuteIndirect,
   Bundles,
   Resets,
   RecordingThreads,
   Count,
};

constexpr std::array<std::string_view, size_t(Counter::Count)> COUNTER_NAMES = {"view_writes_shader_visible", "view_writes_cpu", "copy_calls_shader_visible",
   "copied_shader_visible", "copy_calls_cpu", "copied_cpu", "draws", "dispatches", "executed_lists", "execute_indirect", "bundles", "resets",
   "recording_threads"};

std::array<std::atomic<uint64_t>, size_t(Counter::Count)> frame_counters = {};

void Add(Counter counter, uint64_t value = 1)
{
   frame_counters[size_t(counter)].fetch_add(value, std::memory_order_relaxed);
}

// The binds since the previous draw on a list, in the order of their first occurrence: 2 bits per kind
struct ListOrder
{
   uint8_t code = 0;
   uint8_t seen = 0;
};

// [dispatch][code]
std::array<std::array<std::atomic<uint64_t>, 64>, 2> bind_orders = {};

struct RootSummary
{
   uint32_t created = 0;
   D3D_ROOT_SIGNATURE_VERSION version = D3D_ROOT_SIGNATURE_VERSION_1_0;
   uint32_t size = 0;
   uint32_t parameters = 0;
   uint32_t static_samplers = 0;
   std::string layout;
   bool luma_slot_conflict = false; // b12 or b13 in space 0
   uint32_t max_space = 0;
   bool unbounded = false;
   bool extended = false;
   uint32_t pipelines = 0;
   uint32_t pipelines_with_replaced = 0;
   std::set<uint32_t> replaced_hashes;
};

struct HeapRange
{
   uintptr_t begin = 0;
   uintptr_t end = 0;
   bool shader_visible = false;
};

// In each heap's private data: its range leaves the index with it
struct HeapState
{
   uintptr_t begin = 0;
   ~HeapState();
};

// Lock order: never held with another
std::mutex mutex;
std::map<uint32_t, RootSummary> roots; // By the blob's CRC32
std::unordered_map<const RootLayout*, uint32_t> root_by_layout;
std::map<std::string, uint32_t> pipeline_counts;
std::map<std::string, uint32_t> heap_counts; // "CBV_SRV_UAV visible 1000000"
uint32_t libraries_created = 0;
std::vector<size_t> library_sizes;
uint32_t library_hits = 0;
uint32_t library_misses = 0;
std::string device_info;
std::set<DWORD> recording_threads; // This frame
// Presenting thread
std::array<uint64_t, size_t(Counter::Count)> totals = {};
std::array<uint64_t, size_t(Counter::Count)> maxima = {};
uint64_t frames = 0;
std::chrono::steady_clock::time_point last_write;

// Lock order: never held with another
std::shared_mutex heaps_mutex;
std::vector<HeapRange> heaps; // By begin

HeapState::~HeapState()
{
   std::scoped_lock lock(heaps_mutex);
   std::erase_if(heaps, [this](const HeapRange& range) { return range.begin == begin; });
}

std::optional<bool> IsShaderVisible(D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
   std::shared_lock lock(heaps_mutex);
   auto it = std::upper_bound(heaps.begin(), heaps.end(), handle.ptr, [](uintptr_t value, const HeapRange& range) { return value < range.begin; });
   if (it == heaps.begin())
      return std::nullopt;
   --it;
   if (handle.ptr >= it->end)
      return std::nullopt;
   return it->shader_visible;
}

std::string_view GetVisibilityName(D3D12_SHADER_VISIBILITY visibility)
{
   switch (visibility)
   {
   case D3D12_SHADER_VISIBILITY_ALL: return "all";
   case D3D12_SHADER_VISIBILITY_VERTEX: return "vs";
   case D3D12_SHADER_VISIBILITY_HULL: return "hs";
   case D3D12_SHADER_VISIBILITY_DOMAIN: return "ds";
   case D3D12_SHADER_VISIBILITY_GEOMETRY: return "gs";
   case D3D12_SHADER_VISIBILITY_PIXEL: return "ps";
   case D3D12_SHADER_VISIBILITY_AMPLIFICATION: return "as";
   case D3D12_SHADER_VISIBILITY_MESH: return "ms";
   default: return "?";
   }
}

char GetRegisterLetter(D3D12_DESCRIPTOR_RANGE_TYPE type)
{
   switch (type)
   {
   case D3D12_DESCRIPTOR_RANGE_TYPE_SRV: return 't';
   case D3D12_DESCRIPTOR_RANGE_TYPE_UAV: return 'u';
   case D3D12_DESCRIPTOR_RANGE_TYPE_CBV: return 'b';
   default: return 's';
   }
}

bool CoversLumaSlot(uint32_t base, uint32_t count, uint32_t space)
{
   if (space != 0)
      return false;
   const uint64_t end = count == UINT_MAX ? UINT64_MAX : uint64_t(base) + count;
   return (base <= 12 && 12 < end) || (base <= 13 && 13 < end);
}

// "cbv(b0,ps) table[srv t0-7, smp s0-3](ps) const(b1x4,all)"
RootSummary Summarize(const RootLayout& layout)
{
   RootSummary summary{.version = layout.version, .size = layout.size, .parameters = uint32_t(layout.parameters.size()), .static_samplers = layout.static_samplers};
   summary.extended = layout.luma_parameter != UINT32_MAX;
   for (const RootParameter& parameter : layout.parameters)
   {
      if (!summary.layout.empty())
         summary.layout += ' ';
      const std::string_view visibility = GetVisibilityName(parameter.visibility);
      switch (parameter.type)
      {
      case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
      {
         summary.layout += "table[";
         for (size_t i = 0; i < parameter.ranges.size(); i++)
         {
            const auto& range = parameter.ranges[i];
            const char letter = GetRegisterLetter(range.type);
            const std::string count = range.count == UINT_MAX ? "*" : std::to_string(range.base_register + range.count - 1);
            summary.layout += std::format("{}{}{}-{}", i ? ", " : "", letter, range.base_register, count);
            if (range.space)
               summary.layout += std::format(" space{}", range.space);
            summary.max_space = std::max(summary.max_space, range.space);
            summary.unbounded |= range.count == UINT_MAX;
            if (range.type == D3D12_DESCRIPTOR_RANGE_TYPE_CBV)
               summary.luma_slot_conflict |= CoversLumaSlot(range.base_register, range.count, range.space);
         }
         summary.layout += std::format("]({})", visibility);
         break;
      }
      case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
         summary.layout += std::format("const(b{}x{}{},{})", parameter.shader_register, parameter.constant_count, parameter.space ? std::format(" space{}", parameter.space) : "", visibility);
         summary.luma_slot_conflict |= CoversLumaSlot(parameter.shader_register, 1, parameter.space);
         summary.max_space = std::max(summary.max_space, parameter.space);
         break;
      default:
      {
         const std::string_view kind = parameter.type == D3D12_ROOT_PARAMETER_TYPE_CBV ? "cbv" : parameter.type == D3D12_ROOT_PARAMETER_TYPE_SRV ? "srv" : "uav";
         const char letter = parameter.type == D3D12_ROOT_PARAMETER_TYPE_CBV ? 'b' : parameter.type == D3D12_ROOT_PARAMETER_TYPE_SRV ? 't' : 'u';
         summary.layout += std::format("{}({}{}{},{})", kind, letter, parameter.shader_register, parameter.space ? std::format(" space{}", parameter.space) : "", visibility);
         if (parameter.type == D3D12_ROOT_PARAMETER_TYPE_CBV)
            summary.luma_slot_conflict |= CoversLumaSlot(parameter.shader_register, 1, parameter.space);
         summary.max_space = std::max(summary.max_space, parameter.space);
         break;
      }
      }
   }
   return summary;
}

std::string GetOrderName(uint8_t code)
{
   std::string name;
   while (code)
   {
      constexpr std::array<char, 4> LETTERS = {'?', 'R', 'P', 'T'};
      name.insert(name.begin(), LETTERS[code & 3]);
      code >>= 2;
   }
   return name.empty() ? "-" : name;
}

std::string Quote(std::string_view text)
{
   std::string quoted = "\"";
   for (const char c : text)
   {
      if (c == '"' || c == '\\')
         quoted += '\\';
      quoted += c;
   }
   return quoted + '"';
}

template <typename Map>
std::string ToObject(const Map& map)
{
   std::string text = "{";
   for (const auto& [key, value] : map)
      text += std::format("{}{}: {}", text.size() > 1 ? ", " : "", Quote(key), value);
   return text + "}";
}

// Without `mutex`: the database and variants have locks of their own
std::string DescribeShaders()
{
   std::string text;
   std::map<std::string, uint32_t> targets;
   uint32_t dxil = 0;
   std::vector<std::string> hashes;
   for (const Shaders::Database::Shader* shader : Shaders::Database::GetShaders())
   {
      hashes.push_back(std::format("\"0x{:08X} {}\"", shader->hash, shader->target));
      targets[shader->target.empty() ? "unknown" : shader->target]++;
      const std::optional<Shaders::Container::Program> program = Shaders::Container::GetProgram(shader->bytecode);
      dxil += program && program->dxil;
   }
   text += std::format("  \"shader_targets\": {},\n  \"dxil_shaders\": {},\n", ToObject(targets), dxil);
   std::ranges::sort(hashes);
   std::string hash_list;
   for (const std::string& hash : hashes)
      hash_list += std::format("{}{}", hash_list.empty() ? "" : ", ", hash);
   text += std::format("  \"shader_hashes\": [{}],\n", hash_list);

   std::string found;
   std::string missing;
   std::set<uint32_t> replaced;
   for (const Shaders::Variants::Variant& variant : Shaders::Variants::GetAll())
      replaced.insert(variant.hash);
   for (const uint32_t hash : replaced)
   {
      std::string& list = Shaders::Database::Find(hash) ? found : missing;
      list += std::format("{}\"0x{:08X}\"", list.empty() ? "" : ", ", hash);
   }
   text += std::format("  \"replaced_hashes_created\": [{}],\n  \"replaced_hashes_not_created\": [{}],\n", found, missing);

   const Shaders::Variants::D3D12::Stats variants = Shaders::Variants::D3D12::GetStats();
   const Shaders::PipelineLibrary::D3D12::Stats library = Shaders::PipelineLibrary::D3D12::GetStats();
   const Resources::Database::D3D12::Stats ring = Resources::Database::D3D12::GetStats();
   text += std::format("  \"variant_pipelines\": {{\"tracked\": {}, \"ready\": {}, \"pending\": {}, \"failed\": {}}},\n", variants.pipelines, variants.ready,
      variants.pending, variants.failed);
   text += std::format("  \"luma_pipeline_library\": {{\"hits\": {}, \"misses\": {}, \"stored\": {}}},\n", library.hits, library.misses, library.stored);
   text += std::format("  \"cbuffer_ring\": {{\"bytes\": {}, \"chunks\": {}}},\n", ring.ring_bytes, ring.chunks);

   return text;
}

// Under `mutex`, presenting thread. `shaders` is DescribeShaders'.
std::string BuildReport(const std::string& shaders)
{
   std::string text = std::format("{{\n  \"frames\": {},\n  \"device\": {},\n", frames, Quote(device_info));

   std::map<std::string, std::string> averages;
   std::map<std::string, uint64_t> peaks;
   for (size_t i = 0; i < size_t(Counter::Count); i++)
   {
      averages[std::string(COUNTER_NAMES[i])] = std::format("{:.1f}", frames ? double(totals[i]) / double(frames) : 0.0);
      peaks[std::string(COUNTER_NAMES[i])] = maxima[i];
   }
   text += std::format("  \"per_frame_average\": {},\n  \"per_frame_max\": {},\n", ToObject(averages), ToObject(peaks));

   for (size_t dispatch = 0; dispatch < 2; dispatch++)
   {
      std::map<std::string, uint64_t> orders;
      for (uint8_t code = 0; code < 64; code++)
      {
         if (const uint64_t count = bind_orders[dispatch][code].load(std::memory_order_relaxed))
            orders[GetOrderName(code)] += count;
      }
      text += std::format("  \"bind_order_before_{}\": {},\n", dispatch ? "dispatches" : "draws", ToObject(orders));
   }

   text += std::format("  \"pipelines\": {},\n", ToObject(pipeline_counts));

   text += shaders;

   std::string sizes;
   for (const size_t size : library_sizes)
      sizes += std::format("{}{}", sizes.empty() ? "" : ", ", size);
   text += std::format("  \"game_pipeline_library\": {{\"created\": {}, \"blob_sizes\": [{}], \"load_hits\": {}, \"load_misses\": {}}},\n", libraries_created, sizes,
      library_hits, library_misses);
   text += std::format("  \"descriptor_heaps\": {},\n", ToObject(heap_counts));

   uint32_t largest = 0;
   uint32_t conflicts = 0;
   uint32_t max_space = 0;
   uint32_t over_60 = 0;
   for (const auto& [crc, root] : roots)
   {
      largest = std::max(largest, root.size);
      conflicts += root.luma_slot_conflict;
      max_space = std::max(max_space, root.max_space);
      over_60 += root.size > 60;
   }
   text += std::format("  \"root_signatures\": {{\"count\": {}, \"largest_dwords\": {}, \"over_60_dwords\": {}, \"with_b12_or_b13_space0\": {}, \"max_space\": {}}},\n",
      roots.size(), largest, over_60, conflicts, max_space);

   // Those with replaced shaders first, then by pipelines
   std::vector<std::pair<uint32_t, const RootSummary*>> sorted;
   for (const auto& [crc, root] : roots)
      sorted.emplace_back(crc, &root);
   std::ranges::sort(sorted, [](const auto& a, const auto& b) {
      if (a.second->pipelines_with_replaced != b.second->pipelines_with_replaced)
         return a.second->pipelines_with_replaced > b.second->pipelines_with_replaced;
      return a.second->pipelines > b.second->pipelines;
   });
   text += "  \"root_signature_list\": [\n";
   for (size_t i = 0; i < sorted.size(); i++)
   {
      const RootSummary& root = *sorted[i].second;
      std::string hashes;
      for (const uint32_t hash : root.replaced_hashes)
         hashes += std::format("{}\"0x{:08X}\"", hashes.empty() ? "" : ", ", hash);
      text += std::format("    {{\"crc\": \"0x{:08X}\", \"version\": \"{}\", \"dwords\": {}, \"parameters\": {}, \"static_samplers\": {}, \"created\": {}, "
                          "\"pipelines\": {}, \"pipelines_with_replaced\": {}, \"replaced\": [{}], \"b12_b13_space0\": {}, \"unbounded\": {}, \"extended\": {}, \"layout\": {}}}{}\n",
         sorted[i].first, root.version == D3D_ROOT_SIGNATURE_VERSION_1_0 ? "1.0" : root.version == D3D_ROOT_SIGNATURE_VERSION_1_1 ? "1.1" : "1.2", root.size,
         root.parameters, root.static_samplers, root.created, root.pipelines, root.pipelines_with_replaced, hashes, root.luma_slot_conflict, root.unbounded, root.extended,
         Quote(root.layout), i + 1 < sorted.size() ? "," : "");
   }
   text += "  ]\n}\n";
   return text;
}
} // namespace

Hooks::D3D12::Functions GetD3D12Functions()
{
   Hooks::D3D12::Functions functions;
   functions.set();
   return functions;
}

void RecordDevice(ID3D12Device* device)
{
   D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
   device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
   std::scoped_lock lock(mutex);
   device_info = std::format("resource binding tier {}, shader-visible CBV/SRV/UAV heap limit {}", int(options.ResourceBindingTier),
      options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3 ? "1000000+" : "1000000");
}

void RecordRootSignature(const RootLayout& layout)
{
   const uint32_t crc = Utils::Hash::CRC32(layout.blob);
   std::scoped_lock lock(mutex);
   auto [it, added] = roots.try_emplace(crc);
   if (added)
      it->second = Summarize(layout);
   it->second.created++;
   root_by_layout[&layout] = crc;
}

void RecordPipeline(const Pipeline& pipeline, const PipelineDesc& desc)
{
   std::vector<uint32_t> replaced;
   for (const Shaders::Database::Shader* shader : pipeline.shaders)
   {
      if (shader && Shaders::Variants::HasVariant(shader->hash))
         replaced.push_back(shader->hash);
   }
   const bool compute = !desc.stages[size_t(ShaderStage::Compute)].empty();
   std::scoped_lock lock(mutex);
   pipeline_counts[pipeline.kind == PipelineKind::Graphics ? "graphics" : pipeline.kind == PipelineKind::Compute ? "compute" : compute ? "stream compute" : "stream graphics"]++;
   if (pipeline.from_library)
      pipeline_counts["from game library"]++;
   if (pipeline.embedded_root_signature)
      pipeline_counts["root signature in bytecode"]++;
   else if (!pipeline.root_layout)
      pipeline_counts["root signature not tracked"]++;
   if (desc.unknown_subobject)
      pipeline_counts["stream with unknown subobject"]++;
   if (desc.mesh_shaders)
      pipeline_counts["mesh shaders"]++;
   if (!replaced.empty())
      pipeline_counts["with a replaced shader"]++;
   if (const auto it = root_by_layout.find(pipeline.root_layout); it != root_by_layout.end())
   {
      RootSummary& root = roots[it->second];
      root.pipelines++;
      if (!replaced.empty())
      {
         root.pipelines_with_replaced++;
         root.replaced_hashes.insert(replaced.begin(), replaced.end());
      }
   }
}

void RecordPipelineLibrary(size_t blob_size, HRESULT result)
{
   std::scoped_lock lock(mutex);
   libraries_created++;
   library_sizes.push_back(blob_size);
   Utils::Log::Info("D3D12: the game created a pipeline library from {} bytes (0x{:08X})", blob_size, uint32_t(result));
}

void RecordLibraryLoad(HRESULT result)
{
   std::scoped_lock lock(mutex);
   SUCCEEDED(result) ? library_hits++ : library_misses++;
}

void RecordHeap(const D3D12_DESCRIPTOR_HEAP_DESC& desc, ID3D12DescriptorHeap* heap)
{
   ID3D12Device* device = nullptr;
   if (FAILED(heap->GetDevice(IID_PPV_ARGS(&device))))
      return;
   const UINT increment = device->GetDescriptorHandleIncrementSize(desc.Type);
   device->Release();
   const bool visible = desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
   const uintptr_t begin = heap->GetCPUDescriptorHandleForHeapStart().ptr;
   {
      std::scoped_lock lock(heaps_mutex);
      const HeapRange range{begin, begin + uintptr_t(increment) * desc.NumDescriptors, visible};
      heaps.insert(std::upper_bound(heaps.begin(), heaps.end(), begin, [](uintptr_t value, const HeapRange& other) { return value < other.begin; }), range);
   }
   if (HeapState* state = Hooks::Lifetime::SetState<HeapState>(heap))
      state->begin = begin;
   constexpr std::array<std::string_view, 4> TYPES = {"CBV_SRV_UAV", "SAMPLER", "RTV", "DSV"};
   std::scoped_lock lock(mutex);
   heap_counts[std::format("{} {} {}", desc.Type < TYPES.size() ? TYPES[desc.Type] : "?", visible ? "visible" : "cpu", desc.NumDescriptors)]++;
}

void RecordViewWrite(D3D12_CPU_DESCRIPTOR_HANDLE destination)
{
   Add(IsShaderVisible(destination).value_or(false) ? Counter::ViewWritesShaderVisible : Counter::ViewWritesCpu);
}

void RecordCopy(D3D12_CPU_DESCRIPTOR_HANDLE destination, UINT count)
{
   const bool visible = IsShaderVisible(destination).value_or(false);
   Add(visible ? Counter::CopyCallsShaderVisible : Counter::CopyCallsCpu);
   Add(visible ? Counter::CopiedShaderVisible : Counter::CopiedCpu, count);
}

void RecordReset(ID3D12GraphicsCommandList* list)
{
   Add(Counter::Resets);
   if (ListOrder* order = Hooks::Lifetime::GetOrCreateStateCached<ListOrder>(list))
      *order = {};
   std::scoped_lock lock(mutex);
   recording_threads.insert(GetCurrentThreadId());
}

void RecordBind(ID3D12GraphicsCommandList* list, Bind bind)
{
   ListOrder* order = Hooks::Lifetime::GetOrCreateStateCached<ListOrder>(list);
   if (!order || (order->seen & (1 << uint8_t(bind))))
      return;
   order->seen |= 1 << uint8_t(bind);
   order->code = uint8_t(order->code << 2 | uint8_t(bind));
}

void RecordDraw(ID3D12GraphicsCommandList* list, bool dispatch)
{
   Add(dispatch ? Counter::Dispatches : Counter::Draws);
   ListOrder* order = Hooks::Lifetime::GetOrCreateStateCached<ListOrder>(list);
   if (!order)
      return;
   bind_orders[dispatch][order->code & 63].fetch_add(1, std::memory_order_relaxed);
   *order = {};
}

void RecordExecuteIndirect()
{
   Add(Counter::ExecuteIndirect);
}

void RecordExecuteBundle()
{
   Add(Counter::Bundles);
}

void RecordExecute(UINT count)
{
   Add(Counter::ExecutedLists, count);
}

void CountFrame()
{
   const auto now = std::chrono::steady_clock::now();
   const bool write = now - last_write >= std::chrono::seconds(5);
   const std::string shaders = write ? DescribeShaders() : std::string();
   std::scoped_lock lock(mutex);
   frame_counters[size_t(Counter::RecordingThreads)].store(recording_threads.size(), std::memory_order_relaxed);
   recording_threads.clear();
   for (size_t i = 0; i < size_t(Counter::Count); i++)
   {
      const uint64_t value = frame_counters[i].exchange(0, std::memory_order_relaxed);
      totals[i] += value;
      maxima[i] = std::max(maxima[i], value);
   }
   frames++;
   if (!write)
      return;
   last_write = now;
   System::Jobs::Queue(
      "D3D12 stats",
      [report = BuildReport(shaders)] {
         std::error_code error;
         std::filesystem::create_directories(System::Paths::GetDump(), error);
         std::ofstream file(System::Paths::GetDump() / L"D3D12Stats.json", std::ios::binary | std::ios::trunc);
         file << report;
      },
      [] { return System::Jobs::Outcome{}; }, System::Jobs::Priority::Low);
}
} // namespace D3D12
} // namespace Stats
} // namespace Trace
} // namespace Luma
#endif
