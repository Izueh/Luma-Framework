#pragma once

#include <algorithm>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>

namespace Luma {
// Small thread-safe per-device state table keyed by normalized device pointer.
// Pointer stability: each T is heap-allocated so returned T* stays valid until Erase or replacement for that key.
// Never held with another lock.
template <typename T>
struct DeviceTable
{
   struct Entry
   {
      const void* key = nullptr;
      std::unique_ptr<T> value;
   };

   mutable std::shared_mutex mutex;
   std::vector<Entry> entries;

   T* Find(const void* key) const
   {
      if (!key)
         return nullptr;
      std::shared_lock lock(mutex);
      const auto it = std::ranges::find(entries, key, &Entry::key);
      return it != entries.end() ? it->value.get() : nullptr;
   }

   template <typename... Args>
   T* Emplace(const void* key, Args&&... args)
   {
      if (!key)
         return nullptr;
      auto created = std::make_unique<T>(std::forward<Args>(args)...);
      T* raw = created.get();
      std::unique_lock lock(mutex);
      const auto it = std::ranges::find(entries, key, &Entry::key);
      if (it != entries.end())
         it->value = std::move(created);
      else
         entries.push_back({key, std::move(created)});
      return raw;
   }

   template <typename... Args>
   T* FindOrCreate(const void* key, Args&&... args)
   {
      if (!key)
         return nullptr;
      {
         std::shared_lock lock(mutex);
         const auto it = std::ranges::find(entries, key, &Entry::key);
         if (it != entries.end())
            return it->value.get();
      }
      std::unique_lock lock(mutex);
      const auto it = std::ranges::find(entries, key, &Entry::key);
      if (it != entries.end())
         return it->value.get();
      auto created = std::make_unique<T>(std::forward<Args>(args)...);
      T* raw = created.get();
      entries.push_back({key, std::move(created)});
      return raw;
   }

   void Erase(const void* key)
   {
      if (!key)
         return;
      std::unique_ptr<T> removed;
      {
         std::unique_lock lock(mutex);
         const auto it = std::ranges::find(entries, key, &Entry::key);
         if (it == entries.end())
            return;
         removed = std::move(it->value);
         entries.erase(it);
      }
      // Destroy outside the lock in case ~T touches other state
   }
};
} // namespace Luma
