#pragma once

#define _STRINGIZE(x) _STRINGIZE2(x)
#define _STRINGIZE2(x) #x

#if defined(NDEBUG) && DEVELOPMENT
#undef assert
#define assert(expression) ((void)(                                                       \
            (!!(expression)) ||                                                               \
            (MessageBoxA(NULL, "Assertion failed: " #expression "\nFile: " __FILE__ "\nLine: " _STRINGIZE(__LINE__), Globals::MOD_NAME, MB_SETFOREGROUND | MB_OK))) \
        )
#endif

#if DEVELOPMENT || _DEBUG || TEST
#define ASSERT_ONCE(x) { static bool asserted_once = false; \
if (!asserted_once && !(x)) { assert(x); asserted_once = true; } }
#else
#define ASSERT_ONCE(x)
#endif

namespace
{
#if DEVELOPMENT || _DEBUG
   // Returns true if it vaguely succeeded (definition of success in unclear)
   bool LaunchDebugger(const char* name, const DWORD unique_random_handle = 0)
   {
#if 0 // Non stopping optional debugger
      // Get System directory, typically c:\windows\system32
      std::wstring systemDir(MAX_PATH + 1, '\0');
      UINT nChars = GetSystemDirectoryW(&systemDir[0], systemDir.length());
      if (nChars == 0) return false; // failed to get system directory
      systemDir.resize(nChars);

      // Get process ID and create the command line
      DWORD pid = GetCurrentProcessId();
      std::wostringstream s;
      s << systemDir << L"\\vsjitdebugger.exe -p " << pid;
      std::wstring cmdLine = s.str();

      // Start debugger process
      STARTUPINFOW si;
      ZeroMemory(&si, sizeof(si));
      si.cb = sizeof(si);

      PROCESS_INFORMATION pi;
      ZeroMemory(&pi, sizeof(pi));

      if (!CreateProcessW(NULL, &cmdLine[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return false;

      // Close debugger process handles to eliminate resource leak
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
#else // Stop execution until the debugger is attached or skipped

#if 1
		// Note: the process ID is unique within a session, but not across sessions so it could repeat itself (though unlikely), we currently have no better solution to have a unique identifier unique across dll loads and process runs
		DWORD hProcessId = unique_random_handle != 0 ? unique_random_handle : GetCurrentProcessId();
      std::ifstream fileRead("Luma-Debug"); // Implies "Globals::MOD_NAME"
      if (fileRead)
      {
         DWORD hProcessIdRead;
         fileRead >> hProcessIdRead;
         fileRead.close();
         if (hProcessIdRead == hProcessId)
         {
            return true;
         }
      }

      if (!IsDebuggerPresent())
      {
			// TODO: Add a way to skip this dialog for x minutes or until we change compilation mode
			auto ret = MessageBoxA(NULL, "Loaded. You can now attach the debugger or continue execution (press \"Yes\").\nPress \"No\" to skip this message for this session.\nPress \"Cancel\" to close the application.", name, MB_SETFOREGROUND | MB_YESNOCANCEL);
         if (ret == IDABORT || ret == IDCANCEL)
         {
            exit(0);
         }
         // Write a file on disk so we can avoid re-opening the debugger dialog (which can be annoying) if a program loaded and unloaded multiple times in a row (it can happen on boot)
         else if (ret == IDNO)
         {
            std::ofstream fileWrite("Luma-Debug"); // Implies "Globals::MOD_NAME"
            if (fileWrite)
            {
               fileWrite << hProcessId;
               fileWrite.close();
            }
         }
      }
#else
      // Wait for the debugger to attach
      while (!IsDebuggerPresent()) Sleep(100);
#endif

#endif

#if 0
      // Stop execution so the debugger can take over
      DebugBreak();
#endif

      return true;
   }
#endif // DEVELOPMENT || _DEBUG
}