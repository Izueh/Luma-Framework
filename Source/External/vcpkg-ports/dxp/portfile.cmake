# Keep the SHA512 in sync with the released zip (bump together with the version).
if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x86")
    set(DXP_ARCH "x86")
    set(DXP_SHA512 "5bf7f9c893e00fcf778b662f221b7623ca15b992d1f6c5a2af0b1d192dc54c942727319f824c1737784d3a614e5d4f85da977dbf6a714a63967c7e8184729c4a")
elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    set(DXP_ARCH "x64")
    set(DXP_SHA512 "bdd8f5ba678311b5504d7d18336ddbcb708a131a940063115a74b3a87a867de66cc4a82ef18cf689da428c61349663aa5612b7630829f729e830f34b5f69252e")
else()
    message(FATAL_ERROR "DXP supports only x64 and x86, got ${VCPKG_TARGET_ARCHITECTURE}")
endif()

vcpkg_download_distfile(DXP_ARCHIVE
    URLS "https://github.com/Izueh/DirectXShaderPatcher/releases/download/v${VERSION}/DirectXShaderPatcher-ninja-${DXP_ARCH}.zip"
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
