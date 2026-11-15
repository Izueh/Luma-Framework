#pragma once

// The Development "Resources" tab: the game's textures Luma mirrors (their formats and sizes, the memory the mirrors take) and Luma's
// own named resources (Resources::Database)
namespace Luma {
namespace UI {
namespace Panels {
#if DEVELOPMENT
void DrawResources();
#endif
} // namespace Panels
} // namespace UI
} // namespace Luma
