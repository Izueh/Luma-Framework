#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Luma.hpp"

// Tests.exe links Core like a game: Core.cpp (Luma::IsEnabled) comes with Core::Initialize, which needs the game's Init.
// Never called here.
void Luma::Game::Init() {}
