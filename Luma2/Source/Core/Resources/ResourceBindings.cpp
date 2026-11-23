#include "Resources/ResourceBindings.hpp"

namespace Luma {
namespace Resources {
namespace Bindings {
namespace {
Options options;
} // namespace

void SetOptions(const Options& set)
{
   options = set;
}

const Options& GetOptions()
{
   return options;
}
} // namespace Bindings
} // namespace Resources
} // namespace Luma
