#pragma once

// TODO: Handle this more gracefully, instead of expecting developers to change these manually (see "configuration_defines.props" for a more in depth explanation)

// Enable when you are developing shaders or code (not debugging, there's "NDEBUG" for that).
// This brings out the "devkit", allowing you to trace draw calls and a lot more stuff.
#ifndef DEVELOPMENT
#define DEVELOPMENT 1
#endif // DEVELOPMENT

// Enable when you are testing shaders or code (e.g. to dump the shaders, logging warnings, etc etc).
// This is not mutually exclusive with "DEVELOPMENT", but it should be a sub-set of it.
// If neither of these are true, then we are in "shipping" mode, with code meant to be used by the final user.
#ifndef TEST
#define TEST 1
#endif // TEST