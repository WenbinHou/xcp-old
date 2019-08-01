cmake_minimum_required(VERSION 3.13.0 FATAL_ERROR)

#===============================================================================
# Just disable in-source build
#===============================================================================
if ("${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_BINARY_DIR}")
    message(FATAL_ERROR
        "In-source build is not permitted. Follow these steps for out-of-source build:\n"
        "First, remove the files already generated:\n"
        "    rm -rf CMakeFiles CMakeCache.txt\n"
        "Then, create a separate folder for building:\n"
        "    mkdir build\n"
        "    cd build\n"
        "    cmake ..")
endif()



#===============================================================================
# Project version
#   Also add as definitions
#===============================================================================
project(
    xcp
    VERSION 0.0.2
    LANGUAGES C CXX)
message(STATUS "[xcp] Build version: ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")



#===============================================================================
# If CI, print verbose commands, ...
#===============================================================================
if (__CI__)
    set(CMAKE_VERBOSE_MAKEFILE ON)
endif()



#===============================================================================
# Options
#===============================================================================
# Option: XCP_COMPILER_WERROR
# Treat warnings as errors?
option(XCP_COMPILER_WERROR
    "Treat compiler warnings as errors"
    OFF)

# Option: XCP_ENABLE_UNITTESTS
# Do we build unit tests?
option(XCP_ENABLE_UNITTESTS
    "Enable xcp unit-tests"
    ON)

# Option: XCP_BUILD_STATIC_EXECUTABLE
# Do we build static executable?
option(XCP_BUILD_STATIC_EXECUTABLE
    "Build static executable (as least as static as possible)"
    ON)



#===============================================================================
# Detect target platform, compiler, etc
# Make certain modifications (add compiler flags)
#===============================================================================
include(cmake/Predef.cmake)



#===============================================================================
# Add thirdparty: standalone libraries
# This must be done after include Predef.cmake
#===============================================================================
message(STATUS "[xcp] Add Add thirdparty: standalone libraries")
include(thirdparty/Standalone.cmake)



#===============================================================================
# Detect current git branch and commit hash
#===============================================================================
include(cmake/GitInfo.cmake)



#===============================================================================
# Common compile options for all targets
# This is done AFTER adding standalone thirdparty libraris, thus NOT infecting them
#===============================================================================
# Define: xcp version
add_compile_definitions(
    XCP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    XCP_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    XCP_VERSION_PATCH=${PROJECT_VERSION_PATCH})

# Define: git information
add_compile_definitions(
    XCP_GIT_BRANCH=${GIT_BRANCH}
    XCP_GIT_COMMIT_HASH=${GIT_COMMIT_HASH})

# Define: PLATFORM_XXX
if (PLATFORM_WINDOWS)
    add_compile_definitions(PLATFORM_WINDOWS=1)
elseif (PLATFORM_CYGWIN)
    add_compile_definitions(PLATFORM_CYGWIN=1)
elseif (PLATFORM_LINUX)
    add_compile_definitions(PLATFORM_LINUX=1)
else()
    message(FATAL_ERROR "[xcp] Unknown platform: ${CMAKE_SYSTEM_NAME}")
endif()

# Define: COMPILER_XXX
if (COMPILER_GNU)
    add_compile_definitions(COMPILER_GNU=1)
elseif (COMPILER_CLANG)
    add_compile_definitions(COMPILER_CLANG=1)
elseif (COMPILER_CLANGCL)
    add_compile_definitions(COMPILER_CLANGCL=1)
elseif (COMPILER_MSVC)
    add_compile_definitions(COMPILER_MSVC=1)
else()
    message(FATAL_ERROR "[xcp] Unknown compiler")
endif()

# Require C11, C++17
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# Position independent code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Enable all warnings for current compiler
if (COMPILER_GNU)
    add_compile_options("-Wall")
    add_compile_options("-Wextra")

elseif (COMPILER_CLANG)
    add_compile_options("-Wall")
    add_compile_options("-Wextra")

elseif (COMPILER_MSVC OR COMPILER_CLANGCL)
    # /W4 is enough, /Wall is too pendatic
    add_compile_options("/W4")

else()
    message(FATAL_ERROR "[xcp] Unknown compiler")
endif()

# Treat warnings as errors?
# We shall always treat warnings as errors if CI
if (XCP_COMPILER_WERROR OR __CI__)
    if (COMPILER_GNU OR COMPILER_CLANG)
        add_compile_options("-Werror")
        add_link_options("LINKER:--fatal-warnings")

    elseif (COMPILER_MSVC OR COMPILER_CLANGCL)
        # For MSVC, treat compiler warnings as errors, treat linker warnings as errors
        # See:
        #   https://docs.microsoft.com/en-us/cpp/build/reference/compiler-option-warning-level
        #   https://docs.microsoft.com/en-us/cpp/build/reference/wx-treat-linker-warnings-as-errors
        add_compile_options("/WX")
        add_link_options("/WX")

    else()
        message(FATAL_ERROR "[xcp] Unknown compiler")
    endif()
endif()



#===============================================================================
# Link with some common libraries
#===============================================================================
# Link with thread library
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
link_libraries(Threads::Threads)

# Link with std::filesystem upon different compiler
if (COMPILER_GNU)
    if ("${COMPILER_VERSION_MAJOR}" LESS 9)
        link_libraries(stdc++fs)
    else()  # >= GCC 9.0
        # No need to link with other library
    endif()
elseif (COMPILER_CLANG)
    if (XCP_USE_LLVM_LIBCXX)  # use libc++
        # See: https://libcxx.llvm.org/docs/UsingLibcxx.html#using-filesystem
        if ("${COMPILER_VERSION_MAJOR}" LESS 7)
            link_libraries(c++experimental)
            #link_libraries(c++fs)
        elseif ("${COMPILER_VERSION_MAJOR}" LESS 9)
            link_libraries(c++fs)
        else()  # >= LLVM 9.0
            # No need to link with other library
        endif()
    else()  # use libstdc++
        link_libraries(stdc++fs)
    endif()
elseif (COMPILER_CLANGCL)
elseif (COMPILER_MSVC)
else()
    message(FATAL_ERROR "[xcp] Unknown compiler")
endif()

# Link with network libraries on Windows/Cygwin
if (PLATFORM_WINDOWS OR PLATFORM_CYGWIN)
    link_libraries(ws2_32 mswsock)
elseif (PLATFORM_LINUX)
else()
    message(FATAL_ERROR "Unknown platform")
endif()



#===============================================================================
# Unit testing enabled?
#===============================================================================
if (XCP_ENABLE_UNITTESTS)
    enable_testing()
    message(STATUS "[xcp] Unit-tests are enabled")

    find_package(Python COMPONENTS Interpreter)
    if (Python_Interpreter_FOUND)
        message(STATUS "[xcp] Python found for python test cases: ${Python_EXECUTABLE}")
    else()
        message(WARNING "[xcp] Python not found. Python test cases will be disabled")
    endif()
else()
    message(STATUS "[xcp] Unit-tests are disabled")
endif()



#===============================================================================
# Add thirdparty: non-standalone libraries
#===============================================================================
message(STATUS "[xcp] Add Add thirdparty: non-standalone libraries")
include(thirdparty/NonStandalone.cmake)



#===============================================================================
# Add source code
#===============================================================================
include(src/CMakeLists.txt)
