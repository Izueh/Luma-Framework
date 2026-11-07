#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace Luma {
namespace Settings {
// Index into a setting's "enum_labels"
struct EnumIndex
{
   int32_t value = 0;

   friend constexpr bool operator==(const EnumIndex&, const EnumIndex&) = default;
};

using Value = std::variant<bool, int32_t, float, EnumIndex>;

// For int32_t and float settings, of the same type as the default value: Range{0, 10} or Range{0.f, 1.f}
struct Range
{
   Value min;
   Value max;
};

enum class Visibility
{
   Release, // Always shown
   Test, // Test and development builds
   Development, // Development builds
};

// Declare as static arrays: the registry doesn't copy the strings. The value lives in the registry.
struct Setting
{
   // Unique across categories. Luma's own start with "Luma.", which games don't use. A "<category>." prefix isn't
   // repeated in the file.
   std::string_view key;
   std::string_view label;
   std::string_view tooltip;
   std::string_view category; // UI section and TOML table
   Visibility visibility = Visibility::Release;
   Value default_value = false;
   std::optional<Range> range;
   std::string_view format = "%.3f"; // Of float sliders
   std::span<const std::string_view> enum_labels;
   bool (*is_enabled)() = nullptr;
   bool (*is_visible)() = nullptr;
};

using ChangeListener = void (*)(const Setting& setting, const Value& value);

inline constexpr std::string_view DEFAULT_CATEGORY = "General";

// Skips, and returns false for, settings with a duplicate or empty key, or a range that doesn't suit their type
bool Register(std::span<const Setting> settings);
void AddChangeListener(ChangeListener listener);
// Removes all settings and listeners
void Clear();

const Setting* Find(std::string_view key);
const Value* GetValue(std::string_view key);
// The default constructed "T" if the key doesn't exist or has a different type
template <typename T>
T Get(std::string_view key)
{
   if (const Value* value = GetValue(key))
   {
      if (const T* typed_value = std::get_if<T>(value))
         return *typed_value;
   }
   return T{};
}
// Clamps the value. False if the key doesn't exist or the value has a different type.
bool Set(std::string_view key, const Value& value);
bool Reset(std::string_view key);
// In registration order
void ForEach(const std::function<void(const Setting& setting, const Value& value)>& function);

std::string_view GetCategory(const Setting& setting);
// Clamped to the setting's range and enum labels
Value Clamp(const Setting& setting, const Value& value);
bool IsVisibleInBuild(Visibility visibility);
} // namespace Settings
} // namespace Luma
