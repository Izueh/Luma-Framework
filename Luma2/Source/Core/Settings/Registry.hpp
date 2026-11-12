#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
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

// A keyboard key, as a Windows virtual key code
struct Key
{
   uint32_t code = 0;

   friend constexpr bool operator==(const Key&, const Key&) = default;
};

// A setting with a string default can't be in a constexpr array, declare it const
using Value = std::variant<bool, int32_t, float, EnumIndex, std::string, Key>;

// For int32_t and float settings, of the same type as the default value: Range{0, 10} or Range{0.f, 1.f}
struct Range
{
   Value min;
   Value max;
};

enum class Visibility : uint8_t
{
   Release, // Always shown
   Test, // Test and development builds
   Development, // Development builds
};

// What a change does besides storing the value
namespace Sources {
// Nothing: read it with Get<T>() or a change listener
struct Value
{
};

// Writes the value (as a float: bools are 0 or 1, enums their index) to a variable, usually a field of the Luma settings
// cbuffer, through `parse` if set. 1.0's settings.hpp tables (FF7R, Metro).
struct Binding
{
   float* target = nullptr;
   float (*parse)(float value) = nullptr;
};

// A shader define. Step 1: a change marks the shaders using it for recompilation.
struct Define
{
   std::string_view name;
};

// Enables a shader patch, or sets one of its environment values. Step 3: a change re-patches.
struct Recipe
{
   std::string_view identity;
   std::string_view env; // Empty for the recipe's on/off switch
};
} // namespace Sources

using Source = std::variant<Sources::Value, Sources::Binding, Sources::Define, Sources::Recipe>;

// Declare as static arrays: the registry doesn't copy the strings. The value lives in the registry.
struct Setting
{
   // Unique across categories. Luma's own start with "Luma.", which games don't use. In the file, a "<category>." prefix
   // (after "Luma.") isn't repeated: "Luma.Overlay.Key" is Key in [Overlay].
   std::string_view key;
   std::string_view label;
   std::string_view tooltip;
   std::string_view category; // UI section and TOML table
   Visibility visibility = Visibility::Release;
   Value default_value = false;
   std::optional<Range> range;
   std::string_view format = "%.3f"; // Of float sliders
   std::span<const std::string_view> enum_labels;
   Source source;
   // Sliders apply while dragging, for a cheap effect worth previewing (paper white). Otherwise on release.
   bool live = false;
   bool (*is_enabled)() = nullptr;
   bool (*is_visible)() = nullptr;
};

using ChangeListener = void (*)(const Setting& setting, const Value& value);

inline constexpr std::string_view DEFAULT_CATEGORY = "General";

// One writer: Set, Reset, Register and the listeners run on the presenting thread, at the present boundary (the overlay draws
// there, MCP jobs run there). Other threads Queue their changes. Reads are lock-free from any thread for every type but
// strings, which take a lock: never read a string setting per draw.

// Skips, and returns false for, settings with a duplicate or empty key, or a range that doesn't suit their type
bool Register(std::span<const Setting> settings);
void AddChangeListener(ChangeListener listener);
// Removes all settings and listeners
void Clear();

const Setting* Find(std::string_view key);
std::optional<Value> GetValue(std::string_view key);

// The default constructed "T" if the key doesn't exist or has a different type
template <typename T>
T Get(std::string_view key)
{
   if (const std::optional<Value> value = GetValue(key))
   {
      if (const T* typed_value = std::get_if<T>(&*value))
         return *typed_value;
   }
   return T{};
}

// Clamps the value. False if the key doesn't exist or the value has a different type.
bool Set(std::string_view key, const Value& value);
bool Reset(std::string_view key);
// From any thread: applied by ApplyQueued, at the next present
void Queue(std::string_view key, Value value);
void ApplyQueued();
// In registration order
void ForEach(const std::function<void(const Setting& setting, const Value& value)>& function);

std::string_view GetCategory(const Setting& setting);
// Clamped to the setting's range and enum labels
Value Clamp(const Setting& setting, const Value& value);
bool IsVisibleInBuild(Visibility visibility);
// For tools and logs: "bool", "int", "float", "enum", "string", "key"
std::string_view GetTypeName(const Value& value);
// Parses `text` as a value of the setting's type: 0/1/true/false, numbers, an enum label or index, a key code
std::optional<Value> Parse(const Setting& setting, std::string_view text);
} // namespace Settings
} // namespace Luma
