#include "Settings/Registry.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <charconv>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Utils/Debug.hpp"
#include "Utils/Log.hpp"

namespace Luma {
namespace Settings {
namespace {
// Scalars (every type but strings fits in 32 bits) are atomics, readable from any thread. Strings are read under a lock.
struct Entry
{
   Setting setting;
   std::atomic<uint32_t> bits = 0;
   std::string text; // Under strings_mutex

   explicit Entry(const Setting& setting_) : setting(setting_) {}
};

struct StringHash
{
   using is_transparent = void;

   size_t operator()(std::string_view text) const { return std::hash<std::string_view>{}(text); }
};

// A deque: entries never move, readers keep using them while settings are registered
std::deque<Entry> entries;
std::unordered_map<std::string, size_t, StringHash, std::equal_to<>> entry_indices_by_key;
std::vector<ChangeListener> change_listeners;
// Lock order: queue_mutex is never held with strings_mutex
std::mutex strings_mutex;
std::mutex queue_mutex;
std::vector<std::pair<std::string, Value>> queued;

Entry* FindEntry(std::string_view key)
{
   const auto it = entry_indices_by_key.find(key);
   return it != entry_indices_by_key.end() ? &entries[it->second] : nullptr;
}

uint32_t ToBits(const Value& value)
{
   return std::visit([]<typename T>(const T& typed) -> uint32_t {
      if constexpr (std::is_same_v<T, bool>)
         return typed ? 1u : 0u;
      else if constexpr (std::is_same_v<T, int32_t>)
         return uint32_t(typed);
      else if constexpr (std::is_same_v<T, float>)
         return std::bit_cast<uint32_t>(typed);
      else if constexpr (std::is_same_v<T, EnumIndex>)
         return uint32_t(typed.value);
      else if constexpr (std::is_same_v<T, Key>)
         return typed.code;
      else
         return 0u;
   },
      value);
}

Value Load(const Entry& entry)
{
   const uint32_t bits = entry.bits.load(std::memory_order_acquire);
   return std::visit([&]<typename T>(const T&) -> Value {
      if constexpr (std::is_same_v<T, bool>)
         return bits != 0;
      else if constexpr (std::is_same_v<T, int32_t>)
         return int32_t(bits);
      else if constexpr (std::is_same_v<T, float>)
         return std::bit_cast<float>(bits);
      else if constexpr (std::is_same_v<T, EnumIndex>)
         return EnumIndex{int32_t(bits)};
      else if constexpr (std::is_same_v<T, Key>)
         return Key{bits};
      else
      {
         std::scoped_lock lock(strings_mutex);
         return entry.text;
      }
   },
      entry.setting.default_value);
}

void Store(Entry& entry, const Value& value)
{
   if (const std::string* text = std::get_if<std::string>(&value))
   {
      std::scoped_lock lock(strings_mutex);
      entry.text = *text;
      return;
   }
   entry.bits.store(ToBits(value), std::memory_order_release);
}

float ToFloat(const Value& value)
{
   if (const bool* typed = std::get_if<bool>(&value))
      return *typed ? 1.f : 0.f;
   if (const int32_t* typed = std::get_if<int32_t>(&value))
      return float(*typed);
   if (const float* typed = std::get_if<float>(&value))
      return *typed;
   if (const EnumIndex* typed = std::get_if<EnumIndex>(&value))
      return float(typed->value);
   return 0.f;
}

// What a change does besides storing the value
void ApplySource(const Setting& setting, const Value& value)
{
   if (const Sources::Binding* binding = std::get_if<Sources::Binding>(&setting.source))
   {
      if (binding->target)
         *binding->target = binding->parse ? binding->parse(ToFloat(value)) : ToFloat(value);
   }
   // A define's or a recipe's change is Shaders' (a change listener): nothing to write
}

template <typename T>
std::optional<T> ParseNumber(std::string_view text)
{
   T number{};
   const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
   if (error != std::errc() || end != text.data() + text.size())
      return std::nullopt;
   return number;
}

bool IsDefineRegistered(std::string_view name)
{
   return std::ranges::any_of(entries, [name](const Entry& entry) {
      const Sources::Define* define = std::get_if<Sources::Define>(&entry.setting.source);
      return define && define->name == name;
   });
}

bool IsRangeValid(const Setting& setting)
{
   if (!setting.range)
      return true;
   const bool is_numeric = std::holds_alternative<int32_t>(setting.default_value) || std::holds_alternative<float>(setting.default_value);
   return is_numeric && setting.range->min.index() == setting.default_value.index() && setting.range->max.index() == setting.default_value.index();
}
} // namespace

bool Register(std::span<const Setting> settings, Kind kind)
{
   bool all_registered = true;
   for (const Setting& setting : settings)
   {
      if (setting.key.empty() || entry_indices_by_key.contains(setting.key))
      {
         Utils::Log::Error("Settings: skipping setting with empty or duplicate key \"{}\"", setting.key);
         all_registered = false;
         continue;
      }
      // Define names are global to the compiler
      if (const Sources::Define* define = std::get_if<Sources::Define>(&setting.source); define && IsDefineRegistered(define->name))
      {
         Utils::Log::Error("Settings: skipping \"{}\", define {} is already registered", setting.key, define->name);
         all_registered = false;
         continue;
      }
      if (!IsRangeValid(setting))
      {
         Utils::Log::Error("Settings: skipping setting \"{}\", its range doesn't suit its type", setting.key);
         all_registered = false;
         continue;
      }
      Entry& entry = entries.emplace_back(setting);
      entry.setting.kind = kind;
      Store(entry, Clamp(setting, setting.default_value));
      entry_indices_by_key.emplace(std::string(setting.key), entries.size() - 1);
      if (std::holds_alternative<Sources::Binding>(setting.source))
         ApplySource(setting, Load(entry));
   }
   return all_registered;
}

bool SetDefault(std::string_view key, const Value& value)
{
   Entry* entry = FindEntry(key);
   if (!entry || entry->setting.default_value.index() != value.index())
      return false;
   const Value clamped_value = Clamp(entry->setting, value);
   const bool at_default = Load(*entry) == Clamp(entry->setting, entry->setting.default_value);
   entry->setting.default_value = clamped_value;
   if (at_default)
   {
      Store(*entry, clamped_value);
      ApplySource(entry->setting, clamped_value);
   }
   return true;
}

void AddChangeListener(ChangeListener listener)
{
   change_listeners.push_back(listener);
}

void Clear()
{
   entries.clear();
   entry_indices_by_key.clear();
   change_listeners.clear();
   std::scoped_lock lock(queue_mutex);
   queued.clear();
}

const Setting* Find(std::string_view key)
{
   const Entry* entry = FindEntry(key);
   return entry ? &entry->setting : nullptr;
}

std::optional<Value> GetValue(std::string_view key)
{
   const Entry* entry = FindEntry(key);
   return entry ? std::optional(Load(*entry)) : std::nullopt;
}

bool Set(std::string_view key, const Value& value)
{
   Entry* entry = FindEntry(key);
   if (!entry || entry->setting.default_value.index() != value.index())
      return false;
   const Value clamped_value = Clamp(entry->setting, value);
   if (clamped_value == Load(*entry))
      return true;
   Store(*entry, clamped_value);
   ApplySource(entry->setting, clamped_value);
   for (const ChangeListener listener : change_listeners)
      listener(entry->setting, clamped_value);
   return true;
}

void Queue(std::string_view key, Value value)
{
   std::scoped_lock lock(queue_mutex);
   queued.emplace_back(std::string(key), std::move(value));
}

void ApplyQueued()
{
   std::vector<std::pair<std::string, Value>> changes;
   {
      std::scoped_lock lock(queue_mutex);
      changes.swap(queued);
   }
   for (const auto& [key, value] : changes)
      Set(key, value);
}

bool Reset(std::string_view key)
{
   const Setting* setting = Find(key);
   return setting && Set(key, setting->default_value);
}

void ForEach(const std::function<void(const Setting& setting, const Value& value)>& function)
{
   for (const Entry& entry : entries)
      function(entry.setting, Load(entry));
}

std::string_view GetCategory(const Setting& setting)
{
   return setting.category.empty() ? DEFAULT_CATEGORY : setting.category;
}

std::string_view GetKindName(Kind kind)
{
   constexpr std::string_view NAMES[] = {"Game", "Luma", "Define", "Recipe", "Development"};
   return NAMES[size_t(kind)];
}

Value Clamp(const Setting& setting, const Value& value)
{
   return std::visit([&setting]<typename T>(T typed) -> Value {
      if constexpr (std::is_same_v<T, EnumIndex>)
      {
         const int32_t max_index = std::max(int32_t(setting.enum_labels.size()) - 1, 0);
         return EnumIndex{std::clamp(typed.value, 0, max_index)};
      }
      else if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, std::string> || std::is_same_v<T, Key>)
      {
         return typed;
      }
      else
      {
         return setting.range ? std::clamp(typed, std::get<T>(setting.range->min), std::get<T>(setting.range->max)) : typed;
      }
   },
      value);
}

bool IsVisibleInBuild(Visibility visibility)
{
   switch (visibility)
   {
   case Visibility::Test:
#if TEST || DEVELOPMENT
      return true;
#else
      return false;
#endif
   case Visibility::Development:
#if DEVELOPMENT
      return true;
#else
      return false;
#endif
   default: return true;
   }
}

std::string_view GetTypeName(const Value& value)
{
   constexpr std::string_view NAMES[] = {"bool", "int", "float", "enum", "string", "key"};
   static_assert(std::size(NAMES) == std::variant_size_v<Value>);
   return NAMES[value.index()];
}

std::optional<Value> Parse(const Setting& setting, std::string_view text)
{
   return std::visit([&]<typename T>(const T&) -> std::optional<Value> {
      if constexpr (std::is_same_v<T, bool>)
      {
         if (text == "1" || text == "true")
            return Value(true);
         if (text == "0" || text == "false")
            return Value(false);
         return std::nullopt;
      }
      else if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, float>)
      {
         if (const std::optional<T> number = ParseNumber<T>(text))
            return Value(*number);
         return std::nullopt;
      }
      else if constexpr (std::is_same_v<T, EnumIndex>)
      {
         for (size_t i = 0; i < setting.enum_labels.size(); i++)
         {
            if (setting.enum_labels[i] == text)
               return Value(EnumIndex{int32_t(i)});
         }
         if (const std::optional<int32_t> index = ParseNumber<int32_t>(text))
            return Value(EnumIndex{*index});
         return std::nullopt;
      }
      else if constexpr (std::is_same_v<T, std::string>)
      {
         return Value(std::string(text));
      }
      else
      {
         if (const std::optional<uint32_t> code = ParseNumber<uint32_t>(text))
            return Value(Key{*code});
         return std::nullopt;
      }
   },
      setting.default_value);
}
} // namespace Settings
} // namespace Luma
