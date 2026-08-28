# Keep the SHA512 in sync with the released zip (bump together with the version).
if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x86")
    set(DXP_ARCH "x86")
    set(DXP_SHA512 "e3492d011e4e90d2708de4310efc41b845a7f1b61390ccdab9a473aa1f34cfad0174ca34980dc5db22ec26e04257a470acf1e239977bb1064385e30a6da5e1d8")
elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    set(DXP_ARCH "x64")
    set(DXP_SHA512 "7f00f065e12805a38fd542f4cd4ee1739492aa9f99156820993107b8f0cce52aec653d164483c855033bcd2f3f51429db661735300c5190bdb9f03b21d3d22a8")
else()
    message(FATAL_ERROR "DXP supports only x64 and x86, got ${VCPKG_TARGET_ARCHITECTURE}")
endif()

vcpkg_download_distfile(DXP_ARCHIVE
    URLS "https://github.com/Izueh/DirectXShaderPatcher/releases/download/v${VERSION}/DirectXShaderPatcher-${VERSION}-ninja-${DXP_ARCH}.zip"
    FILENAME "dxp-${VERSION}-ninja-${DXP_ARCH}.zip"
    SHA512 ${DXP_SHA512}
)

vcpkg_extract_source_archive(DXP_SRC ARCHIVE "${DXP_ARCHIVE}" NO_REMOVE_ONE_LEVEL)

file(INSTALL "${DXP_SRC}/${DXP_ARCH}/include/" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(INSTALL "${DXP_SRC}/${DXP_ARCH}/Release/lib/dxpatcher.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
file(INSTALL "${DXP_SRC}/${DXP_ARCH}/Debug/lib/dxpatcher.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")
if(EXISTS "${DXP_SRC}/${DXP_ARCH}/Release/bin")
    file(INSTALL "${DXP_SRC}/${DXP_ARCH}/Release/bin/dxcompiler.dll" DESTINATION "${CURRENT_PACKAGES_DIR}/bin")
    file(INSTALL "${DXP_SRC}/${DXP_ARCH}/Release/bin/ThirdPartyNotices.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/dxp/")
endif()
if(EXISTS "${DXP_SRC}/${DXP_ARCH}/Debug/bin")
    file(INSTALL "${DXP_SRC}/${DXP_ARCH}/Debug/bin/dxcompiler.dll" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/bin")
endif()

file(WRITE "${CURRENT_PACKAGES_DIR}/share/dxp/copyright" "DirectXShaderPatcher: MIT License (see https://github.com/Izueh/DirectXShaderPatcher)\n\nBundled dxcompiler.dll (DirectXShaderCompiler) — see ThirdPartyNotices.txt.\n")

set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
