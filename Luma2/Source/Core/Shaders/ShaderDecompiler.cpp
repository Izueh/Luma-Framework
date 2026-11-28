#include "Shaders/ShaderDecompiler.hpp"

#if DEVELOPMENT
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <Windows.h>

#include "Log/Log.hpp"
#include "Shaders/ShaderCompiler.hpp"
#include "Shaders/ShaderContainer.hpp"
#include "Shaders/ShaderDefines.hpp"
#include "System/JobQueue.hpp"
#include "System/Modules.hpp"
#include "System/Paths.hpp"
#include "Utils/Strings.hpp"

namespace Luma {
namespace Shaders {
namespace Decompiler {
namespace {
std::mutex mutex;
std::unordered_map<std::string, Result> results;

std::string MakeKey(uint32_t hash, std::string_view tag)
{
   return std::format("0x{:08X}_{}", hash, tag);
}

std::filesystem::path GetCachePath(std::string_view key)
{
   return System::Paths::GetCache() / L"Decomp" / std::format("{}.hlsl", key);
}

std::filesystem::path FindDecompilerTool(std::wstring_view exe_name)
{
   std::vector<std::filesystem::path> candidates = {
      System::Paths::GetRoot() / L"Decompilers" / exe_name,
      System::Modules::GetSelfDirectory() / L"Decompilers" / exe_name,
      System::Modules::GetSelfDirectory() / exe_name,
   };
#ifdef LUMA_RESOURCES_DIR
   std::filesystem::path resources_dir = std::filesystem::path(LUMA_RESOURCES_DIR).lexically_normal();
   if (resources_dir.filename().empty())
      resources_dir = resources_dir.parent_path();
   candidates.push_back(resources_dir.parent_path() / L"External" / L"Decompilers" / exe_name);
#endif
   for (const std::filesystem::path& path : candidates)
   {
      std::error_code error;
      if (std::filesystem::is_regular_file(path, error))
         return path;
   }
   return {};
}

std::optional<std::string> ReadTextFile(const std::filesystem::path& path)
{
   std::ifstream stream(path, std::ios::binary);
   if (!stream)
      return std::nullopt;
   return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

bool RunProcessCapture(const std::wstring& command_line, const std::filesystem::path& working_dir, std::string& output, DWORD& exit_code)
{
   SECURITY_ATTRIBUTES sa = {.nLength = sizeof(sa), .bInheritHandle = TRUE};
   HANDLE read_pipe = nullptr;
   HANDLE write_pipe = nullptr;
   if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0))
      return false;
   SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

   STARTUPINFOW si = {.cb = sizeof(si), .dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW, .wShowWindow = SW_HIDE, .hStdOutput = write_pipe, .hStdError = write_pipe};
   PROCESS_INFORMATION pi = {};
   std::wstring mutable_cmd = command_line;
   const BOOL created = CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, working_dir.c_str(), &si, &pi);
   CloseHandle(write_pipe);
   if (!created)
   {
      CloseHandle(read_pipe);
      return false;
   }
   char buffer[4096];
   DWORD bytes_read = 0;
   while (ReadFile(read_pipe, buffer, sizeof(buffer), &bytes_read, nullptr) && bytes_read > 0)
      output.append(buffer, bytes_read);
   CloseHandle(read_pipe);
   WaitForSingleObject(pi.hProcess, 15000);
   exit_code = 1;
   GetExitCodeProcess(pi.hProcess, &exit_code);
   CloseHandle(pi.hThread);
   CloseHandle(pi.hProcess);
   return true;
}

Result DecompileSync(const std::string& key, const Bytecode& bytecode)
{
   const std::filesystem::path cache_path = GetCachePath(key);
   if (const std::optional<std::string> cached = ReadTextFile(cache_path); cached && !cached->empty())
      return {.state = State::Ready, .cached = true, .tool = "cache", .hlsl = *cached};

   const bool is_dxil = Container::HasChunk(bytecode, "DXIL");
   const std::wstring_view exe_name = is_dxil ? L"decomp.exe" : L"cmd_Decompiler.exe";
   const std::string tool_name = is_dxil ? "decomp.exe" : "cmd_Decompiler.exe";
   const std::filesystem::path tool_path = FindDecompilerTool(exe_name);
   if (tool_path.empty())
   {
      return {
         .state = State::Failed,
         .tool = tool_name,
         .error = is_dxil ? "RenoDX decomp.exe not found. Place decomp.exe in Luma2/External/Decompilers/ or Luma/Decompilers/ to decompile DXIL shaders."
                          : "3DMigoto cmd_Decompiler.exe not found. Place cmd_Decompiler.exe in Luma2/External/Decompilers/ or Luma/Decompilers/.",
      };
   }

   std::error_code error;
   const std::filesystem::path work_dir = System::Paths::GetCache() / L"Decomp" / L"Temp";
   std::filesystem::create_directories(work_dir, error);
   const std::filesystem::path cso_path = work_dir / std::format("{}.bin", key);
   const std::filesystem::path out_hlsl_path = work_dir / std::format("{}.hlsl", key);
   {
      std::ofstream out(cso_path, std::ios::binary | std::ios::trunc);
      if (!out)
         return {.state = State::Failed, .tool = tool_name, .error = "Failed to write temporary shader binary for decompilation."};
      out.write(reinterpret_cast<const char*>(bytecode.data()), std::streamsize(bytecode.size()));
   }

   // Invoke RenoDX decomp.exe as "{cso} {hlsl} --flatten --use-do-while" with working_dir set to dxcompiler.dll's directory so
   // decomp.exe can load dxcompiler.dll; 3DMigoto cmd_Decompiler.exe uses "-D {cso}".
   std::filesystem::path run_dir = work_dir;
   if (is_dxil)
   {
      const Compiler::DxcStatus dxc = Compiler::GetDxcStatus();
      if (dxc.loaded && !dxc.path.empty())
      {
         const std::filesystem::path dxc_dir = std::filesystem::path(std::u8string(dxc.path.begin(), dxc.path.end())).parent_path();
         if (!dxc_dir.empty())
            run_dir = dxc_dir;
      }
   }
   const std::wstring cmd = is_dxil ? std::format(L"\"{}\" \"{}\" \"{}\" --flatten --use-do-while", tool_path.wstring(), cso_path.wstring(), out_hlsl_path.wstring())
                                    : std::format(L"\"{}\" -D \"{}\"", tool_path.wstring(), cso_path.wstring());

   std::string stdout_text;
   DWORD exit_code = 1;
   const bool ran = RunProcessCapture(cmd, run_dir, stdout_text, exit_code);

   std::string hlsl;
   // decomp.exe writes `out_hlsl_path`; 3DMigoto writes "<stem>.hlsl" or "<stem>_replace.txt" next to the input binary
   for (const std::filesystem::path& generated : {out_hlsl_path, work_dir / std::format("{}_replace.txt", key)})
   {
      if (const std::optional<std::string> text = ReadTextFile(generated); text && !text->empty())
      {
         hlsl = *text;
         std::filesystem::remove(generated, error);
         break;
      }
   }
   std::filesystem::remove(cso_path, error);

   if (hlsl.empty() && ran && exit_code == 0 && !stdout_text.empty())
      hlsl = stdout_text;

   if (hlsl.empty())
   {
      return {
         .state = State::Failed,
         .tool = tool_name,
         .error = ran ? std::format("{} exited with code {}{}", tool_name, exit_code, stdout_text.empty() ? "" : std::format(":\n{}", stdout_text))
                      : std::format("Failed to launch {}", Utils::Strings::ToUtf8(tool_path)),
      };
   }

   std::filesystem::create_directories(cache_path.parent_path(), error);
   if (std::ofstream cache_out(cache_path, std::ios::binary | std::ios::trunc); cache_out)
      cache_out.write(hlsl.data(), std::streamsize(hlsl.size()));

   return {.state = State::Ready, .cached = false, .tool = tool_name, .hlsl = std::move(hlsl)};
}
} // namespace

Result GetResult(uint32_t hash, std::string_view tag)
{
   const std::string key = MakeKey(hash, tag);
   {
      std::scoped_lock lock(mutex);
      if (const auto it = results.find(key); it != results.end())
         return it->second;
   }
   if (const std::optional<std::string> cached = ReadTextFile(GetCachePath(key)); cached && !cached->empty())
   {
      Result loaded = {.state = State::Ready, .cached = true, .tool = "cache", .hlsl = *cached};
      std::scoped_lock lock(mutex);
      results[key] = loaded;
      return loaded;
   }
   return {};
}

void Request(uint32_t hash, std::string_view tag, Bytecode bytecode)
{
   if (bytecode.empty())
      return;
   const std::string key = MakeKey(hash, tag);
   {
      std::scoped_lock lock(mutex);
      const auto it = results.find(key);
      if (it != results.end() && (it->second.state == State::Running || it->second.state == State::Ready))
         return;
      results[key] = {.state = State::Running};
   }
   auto outcome_result = std::make_shared<Result>();
   System::Jobs::Queue(
      std::format("Decompiling {}", key),
      [key, bytecode = std::move(bytecode), outcome_result] { *outcome_result = DecompileSync(key, bytecode); },
      [key, outcome_result] {
         {
            std::scoped_lock lock(mutex);
            results[key] = std::move(*outcome_result);
         }
         return System::Jobs::Outcome{};
      },
      System::Jobs::Priority::Low);
}

std::optional<std::string> ReadReplacementSource(std::string_view file, bool inline_includes)
{
   if (file.empty())
      return std::nullopt;
   const std::span<const std::filesystem::path> roots = System::Paths::GetShaderRoots();
   if (inline_includes)
   {
      const std::vector<Compiler::Define> defines = Defines::Collect();
      const Compiler::Preprocessed preprocessed = Compiler::Preprocess(roots, file, defines);
      if (!preprocessed.failed && !preprocessed.text.empty())
         return preprocessed.text;
      return preprocessed.messages.empty() ? std::nullopt : std::optional(preprocessed.messages);
   }
   const std::filesystem::path found = System::Paths::Find(roots, std::filesystem::path(std::u8string(file.begin(), file.end())));
   if (found.empty())
      return std::nullopt;
   return ReadTextFile(found);
}
} // namespace Decompiler
} // namespace Shaders
} // namespace Luma
#endif
