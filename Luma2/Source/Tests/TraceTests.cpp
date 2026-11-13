#include <doctest/doctest.h>

#include <array>
#include <ostream>
#include <cstddef>
#include <vector>

#include "Shaders/Variants.hpp"
#include "Trace/Capture.hpp"
#include "Trace/Track.hpp"

using namespace Luma;
using Shaders::Variants::Choice;
using Shaders::Variants::Source;

TEST_CASE("Shaders: the variant picker offers Auto, Original, then each variant that compiled")
{
   const std::array<Source, 1> file = {Source::File};
   const std::vector<Choice> choices = Shaders::Variants::GetChoices(file);
   CHECK(choices == std::vector<Choice>{Choice::Auto, Choice::Original, Choice::File});
   CHECK(Shaders::Variants::FindChoice(choices, Choice::Original) == 1);
   CHECK(Shaders::Variants::FindChoice(choices, Choice::File) == 2);
   // A forced patch whose variant is gone shows as Auto
   CHECK(Shaders::Variants::FindChoice(choices, Choice::Patch) == 0);
   CHECK(Shaders::Variants::GetLabel(Choice::Original) == "Original");
   CHECK(Shaders::Variants::GetChoices({}).size() == 2);
}

#if DEVELOPMENT
namespace {
using Trace::Capture::Event;
using Trace::Capture::EventType;
using Trace::Capture::Filter;
using Trace::Capture::Shader;

Shader MakeShader(ShaderStage stage, uint32_t hash, std::string name = {})
{
   return {.stage = stage, .hash = hash, .name = std::move(name)};
}

// A draw with both stages, a depth only draw, a dispatch, one of Luma's passes, the present
std::vector<Event> MakeFrame()
{
   std::vector<Event> events(5);
   events[0] = {.type = EventType::Draw, .name = "DrawIndexed", .shaders = {MakeShader(ShaderStage::Vertex, 0x11111111, "Mesh VS"), MakeShader(ShaderStage::Pixel, 0x2222ABCD, "Lights")}};
   events[1] = {.type = EventType::Draw, .name = "DrawIndexed", .shaders = {MakeShader(ShaderStage::Vertex, 0x11111111, "Mesh VS")}};
   events[2] = {.type = EventType::Dispatch, .name = "Dispatch", .shaders = {MakeShader(ShaderStage::Compute, 0x33333333)}};
   events[3] = {.type = EventType::LumaPass, .name = "Display Composition", .shaders = {{.stage = ShaderStage::Pixel, .luma_shader = "Luma_DisplayComposition"}}};
   events[4] = {.type = EventType::Present, .name = "Present"};
   events[0].shaders[1].variants = {Source::File};
   events[1].outcome = Trace::Capture::Outcome::Skipped;
   return events;
}

std::vector<size_t> List(const std::vector<Event>& events, const Filter& filter)
{
   std::vector<size_t> listed;
   for (size_t i = 0; i < events.size(); i++)
   {
      if (Trace::Capture::Matches(events[i], filter))
         listed.push_back(i);
   }
   return listed;
}
} // namespace

TEST_CASE("Trace: a call is listed by its pixel shader, else its vertex one, else its compute one")
{
   const std::vector<Event> events = MakeFrame();
   CHECK(Trace::Capture::GetPrimaryShader(events[0])->stage == ShaderStage::Pixel);
   CHECK(Trace::Capture::GetPrimaryShader(events[1])->stage == ShaderStage::Vertex);
   CHECK(Trace::Capture::GetPrimaryShader(events[2])->stage == ShaderStage::Compute);
   CHECK(Trace::Capture::GetPrimaryShader(events[4]) == nullptr);
}

TEST_CASE("Trace: filters match any stage of a call, vertex only draws are hidden by default as in 1.0")
{
   const std::vector<Event> events = MakeFrame();
   CHECK(List(events, {}) == std::vector<size_t>{0, 2, 3, 4});
   CHECK(List(events, {.include_vertex_only = true}) == std::vector<size_t>{0, 1, 2, 3, 4});
   CHECK(List(events, {.luma_passes = false}) == std::vector<size_t>{0, 2, 4});
   // The vertex shader bundled with a pixel one matches its stage, hash and name
   CHECK(List(events, {.stage = ShaderStage::Vertex}) == std::vector<size_t>{0});
   CHECK(List(events, {.include_vertex_only = true, .stage = ShaderStage::Vertex}) == std::vector<size_t>{0, 1});
   CHECK(List(events, {.include_vertex_only = true, .hash = 0x11111111}) == std::vector<size_t>{0, 1});
   CHECK(List(events, {.text = "mesh"}) == std::vector<size_t>{0});
   // Part of a hash, any case, with or without 0x
   CHECK(List(events, {.text = "2222abcd"}) == std::vector<size_t>{0});
   CHECK(List(events, {.text = "0x3333"}) == std::vector<size_t>{2});
   CHECK(List(events, {.text = "composition"}) == std::vector<size_t>{3});
   CHECK(List(events, {.replaced_only = true}) == std::vector<size_t>{0});
   CHECK(List(events, {.include_vertex_only = true, .cancelled_only = true}) == std::vector<size_t>{1});
}

TEST_CASE("Trace: a tracked slot's reading marks the words that changed since the last")
{
   Trace::Track::Set(Trace::Track::Target{.hash = 0x2222ABCD, .stage = ShaderStage::Pixel, .slot = 2});
   CHECK(Trace::Track::IsActive());
   const auto reading = [](std::vector<std::byte> contents) { return Trace::Track::Reading{.contents = std::move(contents)}; };
   Trace::Track::CountCall();
   Trace::Track::SetReading(reading({std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{2}, std::byte{0}, std::byte{0}, std::byte{0}}));
   std::optional<Trace::Track::Reading> first = Trace::Track::GetReading();
   REQUIRE(first);
   CHECK(first->frame == 1);
   CHECK(first->calls == 1);
   CHECK(first->changed == std::vector<uint64_t>{0, 0});
   Trace::Track::SetReading(reading({std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{3}, std::byte{0}, std::byte{0}, std::byte{0}}));
   Trace::Track::SetReading(reading({std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{3}, std::byte{0}, std::byte{0}, std::byte{0}}));
   const std::optional<Trace::Track::Reading> third = Trace::Track::GetReading();
   REQUIRE(third);
   CHECK(third->frame == 3);
   CHECK(third->calls == 0);
   // The second word changed at frame 2 and kept that
   CHECK(third->changed == std::vector<uint64_t>{0, 2});
   // A frame without the call keeps the last reading
   CHECK_FALSE(third->stale);
   Trace::Track::SetReading(std::nullopt);
   CHECK(Trace::Track::GetReading()->frame == 3);
   CHECK(Trace::Track::GetReading()->stale);
   Trace::Track::Set(std::nullopt);
   CHECK_FALSE(Trace::Track::IsActive());
   CHECK_FALSE(Trace::Track::GetReading());
}
#endif
