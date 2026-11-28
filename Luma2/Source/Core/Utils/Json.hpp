#pragma once

#include <expected>
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
std::expected<T, std::string> Read(std::string_view text)
{
   T value{};
   const glz::error_ctx result = glz::read_json(value, text);
   if (!result)
      return value;
   return std::unexpected(glz::format_error(result, text));
}
} // namespace Json
} // namespace Utils
} // namespace Luma
