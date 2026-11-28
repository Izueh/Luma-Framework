// Forwarders to System32\version.dll so Luma can be dropped in as version.dll for games and emulators
// (including Vulkan apps that load vulkan-1.dll/dxgi.dll with LOAD_LIBRARY_SEARCH_SYSTEM32).
#ifdef _WIN64
#define LUMA_VERSION_PATH "\\\\.\\GLOBALROOT\\SystemRoot\\System32\\version.dll."
#else
#define LUMA_VERSION_PATH "\\\\.\\GLOBALROOT\\SystemRoot\\SysWOW64\\version.dll."
#endif
#define LUMA_VERSION_EXPORT(name, ordinal) __pragma(comment(linker, "/EXPORT:" #name "=" LUMA_VERSION_PATH #name ",@" #ordinal))

LUMA_VERSION_EXPORT(GetFileVersionInfoA, 1)
LUMA_VERSION_EXPORT(GetFileVersionInfoByHandle, 2)
LUMA_VERSION_EXPORT(GetFileVersionInfoExA, 3)
LUMA_VERSION_EXPORT(GetFileVersionInfoExW, 4)
LUMA_VERSION_EXPORT(GetFileVersionInfoSizeA, 5)
LUMA_VERSION_EXPORT(GetFileVersionInfoSizeExA, 6)
LUMA_VERSION_EXPORT(GetFileVersionInfoSizeExW, 7)
LUMA_VERSION_EXPORT(GetFileVersionInfoSizeW, 8)
LUMA_VERSION_EXPORT(GetFileVersionInfoW, 9)
LUMA_VERSION_EXPORT(VerFindFileA, 10)
LUMA_VERSION_EXPORT(VerFindFileW, 11)
LUMA_VERSION_EXPORT(VerInstallFileA, 12)
LUMA_VERSION_EXPORT(VerInstallFileW, 13)
LUMA_VERSION_EXPORT(VerLanguageNameA, 14)
LUMA_VERSION_EXPORT(VerLanguageNameW, 15)
LUMA_VERSION_EXPORT(VerQueryValueA, 16)
LUMA_VERSION_EXPORT(VerQueryValueW, 17)
