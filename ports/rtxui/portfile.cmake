vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ArthurSonzogni/RTXUI
    REF v0.1.1
    SHA512 d54adb7ae910fe7b0f537da763254d078b8dad20c74ed9641f0220fddf8e7c7d66731bae1565689720e05948e707ea3ee07d096a03fbb76df431aca6a52ebf4e
    HEAD_REF main
    PATCHES
        crlf-full-frame.patch
        ctrl-space.patch
        mouse-motion.patch
        cell-ink.patch
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DRTXUI_BUILD_TESTS=OFF
        -DRTXUI_BUILD_EXAMPLES=OFF
        -DRTXUI_BUILD_MODULE=OFF
        -DRTXUI_BUILD_FUZZERS=OFF
        -DRTXUI_BUILD_BENCHMARKS=OFF
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/rtxui PACKAGE_NAME rtxui)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
