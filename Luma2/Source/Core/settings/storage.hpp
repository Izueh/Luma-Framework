#pragma once

#include <filesystem>

namespace Luma {
namespace Settings {
namespace Storage {
// A missing file is fine, values of the wrong type are skipped
bool Load(const std::filesystem::path& path);
bool Save(const std::filesystem::path& path);
// Forgets the last loaded document (tests)
void Reset();
} // namespace Storage
} // namespace Settings
} // namespace Luma
