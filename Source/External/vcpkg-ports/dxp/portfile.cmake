# Keep the SHA512 in sync with the released zip (bump together with the version).
if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x86")
    set(DXP_ARCH "x86")
    set(DXP_SHA512 "503889c876d48dfb5d29395ca0baf30efe36e0fe558de7d6261153399ff8761bdbf5f5e6cd836fdfe81e550b048c94270cf9333fc2a6de229e6da6f5bf3e88b5")
elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    set(DXP_ARCH "x64")
    set(DXP_SHA512 "17cdcc24094c957f28724553942bf0aa664c99cb135c69d2a046af9b4a84e05ba62128db106f846a65a396edf13871bfddb9cf311c89cbf72712bff5ee5cb716")
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
