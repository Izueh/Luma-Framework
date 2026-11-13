#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <cstdint>

// The shader files in the Shaders roots, and what each is from its place and name (1.0's rules). Nothing under Includes/ or
// Game/ is compiled on its own.
namespace Luma {
namespace Shaders {
namespace Files {
enum class Kind : uint8_t
{
   // "Name_0x12345678.ps_5_0.hlsl": replaces the game shaders of its hashes
   Replacement,
   // "Luma_Name.hlsl" (or any file under Luma/): Luma's own shaders, the programs its declaration lines name
   Custom,
};

struct Hash
{
   uint32_t value = 0;
   // 1.0 defines "_<text>" when compiling for this hash, with the 8 characters after "0x" as written
   std::string text;

   friend bool operator==(const Hash&, const Hash&) = default;
};

// A program declared in one of Luma's files, by a line "// Luma: <target> [name] [entry=<entry>] [<DEFINE>=<value>...]" in it. Unnamed, it
// has the file's name; several in one file name theirs. Read without compiling.
struct Declaration
{
   std::string name;
   std::string target; // "ps_5_0"
   std::string entry = "main";
   std::vector<std::pair<std::string, std::string>> defines;

   friend bool operator==(const Declaration&, const Declaration&) = default;
};

struct File
{
   std::string path; // From the Shaders root, '/' separated
   Kind kind = Kind::Replacement;
   std::string name; // Without ".hlsl": "Luma_Copy_PS", "Output_HDR_0x########.ps_5_0"
   std::string target; // Replacements: "ps_5_0", from the name
   std::vector<Hash> hashes; // Replacements
   std::vector<Declaration> declarations; // Custom: none for a file without declarations (an include, a sample)
};

// The hashes a "<name>_0x########" file stands for (1.0's redirected_shader_hashes)
struct Redirect
{
   std::string_view name;
   std::span<const uint32_t> hashes;
};

// Init only (Game::Shaders::RedirectHashes). The span and string must outlive Luma.
void AddRedirect(std::string_view name, std::span<const uint32_t> hashes);

// What `path` (from the Shaders root) is. A "0x########" hash stands for the `redirects` of the name before it.
std::optional<File> Parse(std::string_view path, std::span<const Redirect> redirects);
// The declaration lines of a custom shader file's `text`, `name` being the file's name without ".hlsl"
std::vector<Declaration> ParseDeclarations(std::string_view text, std::string_view name);
// Every shader file of the roots, a path in an earlier root hiding the same path in a later one (the game's file wins over Global's,
// listed in `overridden`), with the declared redirects
std::vector<File> Scan(std::span<const std::filesystem::path> roots, std::vector<std::string>* overridden = nullptr);
} // namespace Files
} // namespace Shaders
} // namespace Luma
