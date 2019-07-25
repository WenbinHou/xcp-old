#===============================================================================
# spdlog
#===============================================================================
# We use the header-only version, not the compiled version
#
# We intentionally don't use CMake add_subdirectory,
# as Visual Studio IDE intellisence doesn't support it well
include_directories("${CMAKE_CURRENT_LIST_DIR}/spdlog/include")


#===============================================================================
# CLI11
#===============================================================================
# We intentionally don't use CMake add_subdirectory,
# as Visual Studio IDE intellisence doesn't support it well
include_directories("${CMAKE_CURRENT_LIST_DIR}/CLI11")
