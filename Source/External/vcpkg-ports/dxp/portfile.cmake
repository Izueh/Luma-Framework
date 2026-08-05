# Downloads the prebuilt DirectXShaderPatcher SDK (ninja-x64) from GitHub
# Releases and installs it into the vcpkg layout. No source build: the SDK's
# CI produces the zip (Release /MD lib + Debug /MDd lib + headers + DLLs).
#
# NOTE: the download URL and SHA512 must match a released tag. Update SHA512
# (and the URL) whenever the port version is bumped.

vcpkg_download_distfile(DXP_ARCHIVE
    URLS "https://github.com/Izueh/DirectXShaderPatcher/releases/download/v${VERSION}/DirectXShaderPatcher-${VERSION}-ninja-x64.zip"
    FILENAME "dxp-${VERSION}-ninja-x64.zip"
    SHA512 602e5055660d24792f405c977c1914a255d5858b05e3fd0d98be642557a142968f761c1414f863a2fec5115036ae8a92e896a74c9dc8e60cea51025da27fe3a9
)

vcpkg_extract_source_archive(DXP_SRC ARCHIVE "${DXP_ARCHIVE}" NO_REMOVE_ONE_LEVEL)

# Headers
file(INSTALL "${DXP_SRC}/x64/include/" DESTINATION "${CURRENT_PACKAGES_DIR}/include")

# Release artifacts
file(INSTALL "${DXP_SRC}/x64/Release/lib/dxpatcher.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
file(INSTALL "${DXP_SRC}/x64/Release/bin/dxcompiler.dll" DESTINATION "${CURRENT_PACKAGES_DIR}/bin")

# Debug artifacts
file(INSTALL "${DXP_SRC}/x64/Debug/lib/dxpatcher.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")
file(INSTALL "${DXP_SRC}/x64/Debug/bin/dxcompiler.dll" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/bin")

# Notices (dxcompiler.dll is DXC — required with the DLL)
file(INSTALL "${DXP_SRC}/x64/Release/bin/ThirdPartyNotices.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/dxp/")

# Copyright: DirectXShaderPatcher is MIT; dxcompiler.dll is DXC (see notices).
file(WRITE "${CURRENT_PACKAGES_DIR}/share/dxp/copyright" "DirectXShaderPatcher: MIT License (see https://github.com/Izueh/DirectXShaderPatcher)\n\nBundled dxcompiler.dll (DirectXShaderCompiler) — see ThirdPartyNotices.txt.\n")

set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
