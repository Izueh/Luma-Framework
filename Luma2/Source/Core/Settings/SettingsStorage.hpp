#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Luma {
namespace Settings {
namespace Storage {
// A missing file is fine, values of the wrong type are skipped
bool Load(const std::filesystem::path& path);
bool Save(const std::filesystem::path& path);
// Persisted ImGui layout string stored under [Luma.Overlay.Layout] in Luma.toml
std::string GetOverlayLayout();
void SetOverlayLayout(std::string_view ini);
// Forgets the last loaded document (tests)
void Reset();
} // namespace Storage
} // namespace Settings
} // namespace Luma
