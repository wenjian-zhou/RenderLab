# Repository-owned target triplet. Keep this file in source control so the
# x64/static-library/dynamic-CRT contract cannot drift with a vcpkg update.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_PLATFORM_TOOLSET v143)
set(VCPKG_PLATFORM_TOOLSET_VERSION 14.38.33130)
