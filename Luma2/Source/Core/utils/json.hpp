#pragma once

#include <string>
#include <string_view>

#include <glaze/json.hpp>

namespace Luma {
namespace Utils {
namespace Json {
// Schema-less, integers kept exact
using Value = glz::generic_i64;

template <typename T>
std::string Write(const T& value)
{
   std::string text;
   if (glz::write_json(value, text))
      return {};
   return text;
}

template <typename T>
bool Read(std::string_view text, T& value, std::string* error = nullptr)
{
   const glz::error_ctx result = glz::read_json(value, text);
   if (!result)
      return true;
   if (error)
      *error = glz::format_error(result, text);
   return false;
}
} // namespace Json
} // namespace Utils
} // namespace Luma
