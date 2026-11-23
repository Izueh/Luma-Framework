#include "System/Paths.hpp"

#include <string>
#include <system_error>
#include <vector>

#include "System/Modules.hpp"
#include "Log/Log.hpp"
#include "Utils/Strings.hpp"

namespace Luma {
namespace System {
namespace Paths {
namespace {
std::filesystem::path root;
std::vector<std::filesystem::path> shader_roots;
std::vector<std::filesystem::path> texture_roots;
std::filesystem::path recipes;

bool IsDirectory(const std::filesystem::path& path)
{
   std::error_code error;
   return std::filesystem::is_directory(path, error);
}
} // namespace

void Initialize()
{
   root = System::Modules::GetPath(nullptr).parent_path() / L"Luma";
   std::error_code error;
   std::filesystem::create_directories(root, error);
}

void ResolveRoots(std::string_view game_folder)
{
   shader_roots = {root / L"Shaders"};
   texture_roots = {root / L"Textures"};
   recipes = root / L"Recipes";
#if DEVELOPMENT && defined(LUMA_RESOURCES_DIR)
   // The repository this build was made from: sources are edited in place
   const std::filesystem::path resources = LUMA_RESOURCES_DIR;
   const std::filesystem::path game = resources / std::u8string(game_folder.begin(), game_folder.end());
   if (!game_folder.empty() && IsDirectory(game) && IsDirectory(resources / L"Global"))
   {
      shader_roots = {game / L"Shaders", resources / L"Global" / L"Shaders"};
      texture_roots = {game / L"Textures", resources / L"Global" / L"Textures"};
      recipes = game / L"Recipes";
      Log::Info("Paths: shaders, textures and recipes from the repository, {}", Utils::Strings::ToUtf8(resources));
   }
#else
   (void)game_folder;
#endif
}

const std::filesystem::path& GetRoot()
{
   return root;
}

std::filesystem::path GetConfig()
{
   return root / L"Luma.toml";
}

std::filesystem::path GetLog()
{
   return root / L"Luma.log";
}

std::filesystem::path GetCache()
{
   return root / L"Cache";
}

std::filesystem::path GetDump()
{
   return root / L"Dump";
}

std::span<const std::filesystem::path> GetShaderRoots()
{
   return shader_roots;
}

std::span<const std::filesystem::path> GetTextureRoots()
{
   return texture_roots;
}

std::filesystem::path GetRecipes()
{
   return recipes;
}

std::filesystem::path Find(std::span<const std::filesystem::path> roots, const std::filesystem::path& relative)
{
   for (const std::filesystem::path& search_root : roots)
   {
      std::error_code error;
      std::filesystem::path path = search_root / relative;
      if (std::filesystem::exists(path, error))
         return path;
   }
   return {};
}
} // namespace Paths
} // namespace System
} // namespace Luma
