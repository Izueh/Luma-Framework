#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Luma.hpp"

// Tests.exe links Core like a game: Core.cpp (Luma::IsEnabled) comes with Core::Initialize, which needs the game's Init.
// Never called here.
void Luma::Game::Init() {}

// Asserts fail on purpose in some tests: a message box would wait for a click
const bool SILENT_ASSERTS = (Luma::Notifications::ForceSilentAsserts(), true);
