# Keep the SHA512 in sync with the released zip (bump together with the version).
if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x86")
    set(DXP_ARCH "x86")
    set(DXP_SHA512 "c2df9439ee336303c2c66cc1fd16d4b354f649692cd39ae5c9a6b013275abdc91fcca0ebf6f2d86ffeb486060aae8912fddcf778b7da09d94b4d24d4912dbe96")
elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    set(DXP_ARCH "x64")
    set(DXP_SHA512 "d40d9e830b49eaaa1e7251151d8cf06da8b877368778b26d2ed27e014f383b894b24915f459cb086d4a955ec65f327fa083250743953f1f88900de110abf4b92")
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
