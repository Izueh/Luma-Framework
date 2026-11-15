#include "Shaders/D3D12/D3D12PipelineLibrary.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include "Hooks/Scope.hpp"
#include "System/Jobs.hpp"
#include "System/Paths.hpp"
#include "Utils/Log.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Shaders {
namespace PipelineLibrary {
namespace D3D12 {
namespace {
// One library, for the first device that makes variants (a game has one device that draws; adapter checks make none)
struct Library
{
   // Shared: loads and stores (thread safe in D3D12). Unique: opening, serializing, releasing. Never held with another lock.
   std::shared_mutex mutex;
   ID3D12Device* device = nullptr; // Compared only
   ComPtr<ID3D12PipelineLibrary> library;
   std::vector<uint8_t> blob; // The library reads from it for its whole life
   bool opened = false;
};

Library library;
std::atomic<uint32_t> hits = 0;
std::atomic<uint32_t> misses = 0;
std::atomic<uint32_t> stored = 0;
// Presenting thread, but `dirty` and the time, set by stores
std::atomic<bool> dirty = false;
std::atomic<int64_t> last_store = 0; // steady_clock ticks
bool saving = false;

std::filesystem::path GetPath()
{
   return System::Paths::GetCache() / L"Pipelines" / L"Library.bin";
}

// Under the unique lock
void Open(ID3D12Device* device)
{
   library.opened = true;
   library.device = device;
   ComPtr<ID3D12Device1> device1;
   if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device1))))
   {
      Utils::Log::Warning("D3D12: no ID3D12Device1, variant pipelines aren't kept across boots");
      return;
   }
   std::ifstream file(GetPath(), std::ios::binary);
   library.blob.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
   file.close();
   Hooks::Scope::Guard scope;
   HRESULT result = E_FAIL;
   if (!library.blob.empty())
   {
      result = device1->CreatePipelineLibrary(library.blob.data(), library.blob.size(), IID_PPV_ARGS(&library.library));
      if (FAILED(result))
      {
         // D3D12_ERROR_DRIVER_VERSION_MISMATCH, D3D12_ERROR_ADAPTER_NOT_FOUND, or a damaged file
         Utils::Log::Info("D3D12: the pipeline library was made by another driver or adapter (0x{:08X}), it starts over", uint32_t(result));
         std::error_code error;
         std::filesystem::remove(GetPath(), error);
         library.blob.clear();
      }
   }
   if (FAILED(result) && FAILED(device1->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&library.library))))
   {
      Utils::Log::Warning("D3D12: no pipeline library, variant pipelines aren't kept across boots");
      return;
   }
   Utils::Log::Info("D3D12: pipeline library opened ({} bytes)", library.blob.size());
}

// The library for `device`, opened at its first use. Null for another device or without one.
ID3D12PipelineLibrary* Get(ID3D12Device* device, std::shared_lock<std::shared_mutex>& lock)
{
   lock = std::shared_lock(library.mutex);
   if (!library.opened)
   {
      lock.unlock();
      {
         std::scoped_lock unique(library.mutex);
         if (!library.opened)
            Open(device);
      }
      lock.lock();
   }
   return library.device == device ? library.library.Get() : nullptr;
}

int64_t Now()
{
   return std::chrono::steady_clock::now().time_since_epoch().count();
}
} // namespace

ComPtr<ID3D12PipelineState> Load(ID3D12Device* device, const std::wstring& name, Database::D3D12::PipelineKind kind, const void* desc)
{
   std::shared_lock<std::shared_mutex> lock;
   ID3D12PipelineLibrary* pipelines = Get(device, lock);
   if (!pipelines)
      return nullptr;
   Hooks::Scope::Guard scope;
   ComPtr<ID3D12PipelineState> pipeline;
   HRESULT result = E_FAIL;
   switch (kind)
   {
   case Database::D3D12::PipelineKind::Graphics:
      result = pipelines->LoadGraphicsPipeline(name.c_str(), static_cast<const D3D12_GRAPHICS_PIPELINE_STATE_DESC*>(desc), IID_PPV_ARGS(&pipeline));
      break;
   case Database::D3D12::PipelineKind::Compute:
      result = pipelines->LoadComputePipeline(name.c_str(), static_cast<const D3D12_COMPUTE_PIPELINE_STATE_DESC*>(desc), IID_PPV_ARGS(&pipeline));
      break;
   case Database::D3D12::PipelineKind::Stream:
      if (ComPtr<ID3D12PipelineLibrary1> pipelines1; SUCCEEDED(pipelines->QueryInterface(IID_PPV_ARGS(&pipelines1))))
         result = pipelines1->LoadPipeline(name.c_str(), static_cast<const D3D12_PIPELINE_STATE_STREAM_DESC*>(desc), IID_PPV_ARGS(&pipeline));
      break;
   }
   (SUCCEEDED(result) ? hits : misses)++;
   return SUCCEEDED(result) ? pipeline : nullptr;
}

void Store(ID3D12Device* device, const std::wstring& name, ID3D12PipelineState* pipeline)
{
   std::shared_lock<std::shared_mutex> lock;
   ID3D12PipelineLibrary* pipelines = Get(device, lock);
   if (!pipelines)
      return;
   Hooks::Scope::Guard scope;
   // E_INVALIDARG for a name already stored
   if (SUCCEEDED(pipelines->StorePipeline(name.c_str(), pipeline)))
   {
      stored++;
      last_store = Now();
      dirty = true;
   }
}

void Save()
{
   if (saving || !dirty || Now() - last_store.load() < std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::seconds(3)).count())
      return;
   saving = true;
   dirty = false;
   System::Jobs::Queue(
      "Saving pipelines",
      [] {
         std::vector<uint8_t> bytes;
         {
            // Serialize can't run beside a store
            std::scoped_lock lock(library.mutex);
            if (!library.library)
               return;
            Hooks::Scope::Guard scope;
            bytes.resize(library.library->GetSerializedSize());
            if (FAILED(library.library->Serialize(bytes.data(), bytes.size())))
            {
               Utils::Log::Error("D3D12: the pipeline library couldn't be serialized");
               return;
            }
         }
         std::error_code error;
         std::filesystem::create_directories(GetPath().parent_path(), error);
         std::filesystem::path temporary = GetPath();
         temporary += L".tmp";
         {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
         }
         std::filesystem::rename(temporary, GetPath(), error);
         if (error)
            Utils::Log::Error("D3D12: the pipeline library couldn't be written ({})", error.message());
      },
      [] {
         saving = false;
         return System::Jobs::Outcome{};
      },
      System::Jobs::Priority::Low);
}

void ReleaseDeviceObjects(ID3D12Device* device)
{
   std::scoped_lock lock(library.mutex);
   if (library.device != device)
      return;
   library.library.Reset();
   library.device = nullptr;
}

Stats GetStats()
{
   return {hits.load(), misses.load(), stored.load()};
}
} // namespace D3D12
} // namespace PipelineLibrary
} // namespace Shaders
} // namespace Luma
