#include <doctest/doctest.h>

#include <cstdint>
#include <string>

#include <Windows.h>

#include "trace/mcp.hpp"
#include "utils/json.hpp"

using namespace Luma;

TEST_CASE("Mcp: requests are the tool name line, then key=value lines")
{
   const Trace::Mcp::Request request = Trace::Mcp::ParseRequest("read_resource\r\nindex=3\nslot=t0=x\nno value\n");
   CHECK(request.tool == "read_resource");
   CHECK(request.args.size() == 2);
   CHECK(request.args.at("index") == "3");
   CHECK(request.args.at("slot") == "t0=x");
   CHECK(Trace::Mcp::ParseRequest("status").args.empty());
   CHECK(Trace::Mcp::ParseRequest("").tool.empty());
}

TEST_CASE("Mcp: status reports the process, and unknown tools and arguments are errors")
{
   Utils::Json::Value status;
   REQUIRE(Utils::Json::Read(Trace::Mcp::Answer(Trace::Mcp::ParseRequest("status\ntimeout_ms=100\nbogus=1")), status));
   CHECK(status["ok"].get<bool>());
   CHECK(status["pid"].get<int64_t>() == int64_t(GetCurrentProcessId()));
   CHECK(status["luma"].get<int64_t>() == 2);
   CHECK(status.contains("presenting"));
   CHECK(status.contains("counts"));
   // timeout_ms is the bridge's, bogus is a mistake
   REQUIRE(status["ignored_args"].is_array());
   CHECK(status["ignored_args"].get_array().size() == 1);

   Utils::Json::Value error;
   REQUIRE(Utils::Json::Read(Trace::Mcp::Answer(Trace::Mcp::ParseRequest("trace_capture")), error));
   CHECK_FALSE(error["ok"].get<bool>());
   CHECK(error["error"].get<std::string>().find("trace_capture") != std::string::npos);
}

TEST_CASE("Mcp: the pipe answers framed requests like Scripts/luma_mcp.py sends them, from several clients")
{
   Trace::Mcp::Start();
   const auto open = [] {
      HANDLE pipe = INVALID_HANDLE_VALUE;
      // The server thread may not be listening yet
      for (int i = 0; i < 100 && pipe == INVALID_HANDLE_VALUE; i++)
      {
         pipe = CreateFileW(Trace::Mcp::GetPipeName().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
         if (pipe == INVALID_HANDLE_VALUE)
            Sleep(20);
      }
      return pipe;
   };
   const auto call = [](HANDLE pipe, const std::string& request) {
      const uint32_t size = uint32_t(request.size());
      DWORD bytes = 0;
      REQUIRE(WriteFile(pipe, &size, sizeof(size), &bytes, nullptr));
      REQUIRE(WriteFile(pipe, request.data(), size, &bytes, nullptr));
      uint32_t reply_size = 0;
      REQUIRE(ReadFile(pipe, &reply_size, sizeof(reply_size), &bytes, nullptr));
      std::string reply(reply_size, '\0');
      DWORD total = 0;
      while (total < reply_size)
      {
         REQUIRE(ReadFile(pipe, reply.data() + total, reply_size - total, &bytes, nullptr));
         total += bytes;
      }
      Utils::Json::Value json;
      REQUIRE(Utils::Json::Read(reply, json));
      return json;
   };

   HANDLE first = open();
   REQUIRE(first != INVALID_HANDLE_VALUE);
   HANDLE second = open();
   REQUIRE(second != INVALID_HANDLE_VALUE);
   CHECK(call(first, "status")["ok"].get<bool>());
   CHECK(call(second, "status\ntimeout_ms=1000")["pid"].get<int64_t>() == int64_t(GetCurrentProcessId()));
   // A connection serves any number of requests
   CHECK_FALSE(call(first, "nothing")["ok"].get<bool>());
   CHECK(call(first, "status")["ok"].get<bool>());
   CloseHandle(first);
   CloseHandle(second);

   Trace::Mcp::Stop();
}
