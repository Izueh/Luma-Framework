#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include <glaze/json/generic.hpp>

namespace Luma {
namespace Utils {
namespace Toml {
using Value = glz::generic_i64;
// Keys in insertion order. A document is its root table.
using Table = Value::object_t;

// Returns false (and the error in `error`, if given) on malformed TOML
bool Read(std::string_view text, Table& document, std::string* error = nullptr);
std::string Write(const Table& document);

// A missing file loads as an empty document
bool Load(const std::filesystem::path& path, Table& document);
bool Save(const std::filesystem::path& path, const Table& document);
} // namespace Toml
} // namespace Utils
} // namespace Luma
