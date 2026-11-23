#include "Resources/TextureLoader.hpp"

#include <algorithm>
#include <cwctype>
#include <string>

#include "Resources/ResourceDatabase.hpp"
#include "System/Paths.hpp"
#include "Utils/Hash.hpp"
#include "Log/Log.hpp"
#include "Utils/Strings.hpp"

namespace Luma {
namespace Resources {
namespace Textures {
namespace {
// Written in Init, read without a lock after
Options options;

bool IsDds(const std::filesystem::path& path)
{
   std::wstring extension = path.extension().wstring();
   std::ranges::transform(extension, extension.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
   return extension == L".dds";
}
} // namespace

void SetOptions(const Options& set)
{
   options = set;
}

const Options& GetOptions()
{
   return options;
}

void Register()
{
   if (!options.load)
      return;
   uint32_t count = 0;
   // The game's root first: its file wins a name clash (Database::Register keeps the first)
   for (const std::filesystem::path& root : System::Paths::GetTextureRoots())
   {
      std::error_code error;
      for (std::filesystem::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error))
      {
         if (!it->is_regular_file(error) || !IsDds(it->path()))
            continue;
         const std::string name = Utils::Strings::ToUtf8(it->path().stem());
         const uint64_t name_hash = Utils::Hash::Name(name);
         if (const std::optional<uint32_t> existing = Database::Find(name_hash))
         {
            if (Database::Get(*existing).kind == Database::Kind::Texture)
               Log::Info("Resources: {} is overridden by the game's", Utils::Strings::ToUtf8(it->path()));
            continue;
         }
         const std::string relative = Utils::Strings::ToUtf8(std::filesystem::path(std::filesystem::relative(it->path(), root, error).generic_wstring()));
         if (Database::Register({.name_hash = name_hash, .name = name, .kind = Database::Kind::Texture, .file = relative}))
            count++;
      }
   }
   Log::Info("Resources: {} texture{} from the Textures folders", count, count == 1 ? "" : "s");
}

std::filesystem::path Find(uint32_t entry)
{
   const std::string& file = Database::Get(entry).file;
   return System::Paths::Find(System::Paths::GetTextureRoots(), std::filesystem::path(std::u8string(file.begin(), file.end())));
}
} // namespace Textures
} // namespace Resources
} // namespace Luma
