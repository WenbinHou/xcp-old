#
# Detect compiler
#
# Currently supported compilers are:
#
#   MSVC    >= VS2019
#   GCC     >= 7
#   Clang   >= 5
#
message(STATUS "[xcp] CMAKE_C_COMPILER_ID: ${CMAKE_C_COMPILER_ID}")
message(STATUS "[xcp] CMAKE_CXX_COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")

if (NOT "${CMAKE_C_COMPILER_ID}" STREQUAL "${CMAKE_CXX_COMPILER_ID}")
    message(FATAL_ERROR
        "[xcp] Using different compiler for C and C++ is not supported!\n"
        "  C Copmiler: ${CMAKE_C_COMPILER_ID} (${CMAKE_C_COMPILER})\n"
        "  C++ Copmiler: ${CMAKE_CXX_COMPILER_ID} (${CMAKE_CXX_COMPILER})")
endif()

message(STATUS "[xcp] CMAKE_C_COMPILER_VERSION: ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "[xcp] CMAKE_CXX_COMPILER_VERSION: ${CMAKE_CXX_COMPILER_VERSION}")

if (NOT "${CMAKE_C_COMPILER_VERSION}" STREQUAL "${CMAKE_CXX_COMPILER_VERSION}")
    message(FATAL_ERROR
        "[xcp] Using different version of compiler for C and C++ is not supported!\n"
        "  C Copmiler: ${CMAKE_C_COMPILER_ID} (${CMAKE_C_COMPILER_VERSION})\n"
        "  C++ Copmiler: ${CMAKE_CXX_COMPILER_ID} (${CMAKE_CXX_COMPILER_VERSION})")
endif()


unset(COMPILER_GNU CACHE)
unset(COMPILER_CLANGCL CACHE)
unset(COMPILER_CLANG CACHE)
unset(COMPILER_MSVC CACHE)

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "^(GNU)$")
    message(STATUS "[xcp] Using compiler: GNU")
    set(COMPILER_GNU ON CACHE BOOL "Using GNU compiler" FORCE)

elseif ("${CMAKE_CXX_COMPILER_ID}" MATCHES "^(Clang)$")
    # Detect clang-cl
    # See: https://stackoverflow.com/questions/50857779/cmake-detects-clang-cl-as-clang
    if ("${CMAKE_CXX_SIMULATE_ID}" STREQUAL "MSVC")
        message(STATUS "[xcp] Using compiler: Clang (MSVC-compatible clang-cl)")
        set(COMPILER_CLANGCL ON CACHE BOOL "Using clang-cl compiler" FORCE)
    else()
        message(STATUS "[xcp] Using compiler: Clang")
        set(COMPILER_CLANG ON CACHE BOOL "Using clang compiler" FORCE)
    endif()

elseif ("${CMAKE_CXX_COMPILER_ID}" MATCHES "^(MSVC)$")
    message(STATUS "[xcp] Using compiler: MSVC")
    set(COMPILER_MSVC ON CACHE BOOL "Using MSVC compiler" FORCE)

else()
    message(FATAL_ERROR "[xcp] Unknown compiler: ${CMAKE_CXX_COMPILER_ID}")
endif()



#
# Detect compiler version
#
unset(COMPILER_VERSION_MAJOR CACHE)
unset(COMPILER_VERSION_MINOR CACHE)
unset(COMPILER_VERSION_PATCH CACHE)
unset(COMPILER_VERSION CACHE)

set(_REGEX_VERSION "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(\\.[0-9]+)?$")
if("${CMAKE_CXX_COMPILER_VERSION}" MATCHES "${_REGEX_VERSION}")
    string(REGEX MATCH "${_REGEX_VERSION}" _ "${CMAKE_CXX_COMPILER_VERSION}")
    set(COMPILER_VERSION_MAJOR "${CMAKE_MATCH_1}" CACHE STRING "" FORCE)
    set(COMPILER_VERSION_MINOR "${CMAKE_MATCH_2}" CACHE STRING "" FORCE)
    set(COMPILER_VERSION_PATCH "${CMAKE_MATCH_3}" CACHE STRING "" FORCE)
    set(COMPILER_VERSION "${COMPILER_VERSION_MAJOR}.${COMPILER_VERSION_MINOR}.${COMPILER_VERSION_PATCH}" CACHE STRING "" FORCE)
    message(STATUS "[xcp] Using compiler version: major = ${COMPILER_VERSION_MAJOR}")
    message(STATUS "[xcp] Using compiler version: minor = ${COMPILER_VERSION_MINOR}")
    message(STATUS "[xcp] Using compiler version: patch = ${COMPILER_VERSION_PATCH}")
    message(STATUS "[xcp] Using compiler version: ${COMPILER_VERSION}")
else()
    message(FATAL_ERROR "[xcp] Unknown compiler version: ${CMAKE_CXX_COMPILER_VERSION}")
endif()



#
# Detect platform
#
# Currently supported platforms are:
#   Linux
#   Native Windows (including MinGW)
#
message(STATUS "[xcp] CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")

unset(PLATFORM_WINDOWS CACHE)
unset(PLATFORM_CYGWIN CACHE)
unset(PLATFORM_LINUX CACHE)

if ("${CMAKE_SYSTEM_NAME}" MATCHES "^(Windows)$")
    set(PLATFORM_WINDOWS ON CACHE STRING "" FORCE)
    message(STATUS "[xcp] Platform: PLATFORM_WINDOWS")

elseif ("${CMAKE_SYSTEM_NAME}" MATCHES "^(CYGWIN)$")
    set(PLATFORM_CYGWIN ON CACHE STRING "" FORCE)
    message(STATUS "[xcp] Platform: PLATFORM_CYGWIN")

elseif ("${CMAKE_SYSTEM_NAME}" MATCHES "^(MSYS)$")
    message(FATAL_ERROR "[xcp] Platform: MSYS is NOT supported")

elseif ("${CMAKE_SYSTEM_NAME}" MATCHES "^(Linux)$")
    set(PLATFORM_LINUX ON CACHE STRING "" FORCE)
    message(STATUS "[xcp] Platform: PLATFORM_LINUX")

else()
    message(FATAL_ERROR "[xcp] Unknown platform: ${CMAKE_SYSTEM_NAME}")
endif()



#
# Set compiler/linker flags for specific compilers
# This is done BEFORE adding standalone thirdparty libraris, thus infecting them too
#
if (COMPILER_GNU)
    # Nothing to do

elseif (COMPILER_CLANG)
    # Option: XCP_USE_LLVM_LIBCXX
    # Do we build against LLVM libc++?
    option(XCP_USE_LLVM_LIBCXX
        "Build against LLVM libc++"
        OFF)

    if (XCP_USE_LLVM_LIBCXX)
        message(STATUS "[xcp] Use LLVM libc++")
        add_compile_options("-stdlib=libc++")
        add_link_options("-stdlib=libc++")
        link_libraries(c++ c++abi)
    endif()

elseif (COMPILER_MSVC OR COMPILER_CLANGCL)
    macro(_fix_cl_options Var)
        # Use /MT, /MTd instead of /MD, /MDd
        string (REGEX REPLACE "( |\t|^)(/MD)( |\t|$)" "\\1/MT\\3" ${Var} "${${Var}}")
        string (REGEX REPLACE "( |\t|^)(/MDd)( |\t|$)" "\\1/MTd\\3" ${Var} "${${Var}}")

        # Remove default warning level
        string (REGEX REPLACE "( |\t|^)/W[0-4]( |\t|$)" "\\1\\2" ${Var} "${${Var}}")

        message(STATUS "[xcp] ${Var}: ${${Var}}")
    endmacro()

    _fix_cl_options(CMAKE_C_FLAGS)
    _fix_cl_options(CMAKE_C_FLAGS_DEBUG)
    _fix_cl_options(CMAKE_C_FLAGS_RELEASE)
    _fix_cl_options(CMAKE_C_FLAGS_RELWITHDEBINFO)
    _fix_cl_options(CMAKE_C_FLAGS_MINSIZEREL)
    _fix_cl_options(CMAKE_CXX_FLAGS)
    _fix_cl_options(CMAKE_CXX_FLAGS_DEBUG)
    _fix_cl_options(CMAKE_CXX_FLAGS_RELEASE)
    _fix_cl_options(CMAKE_CXX_FLAGS_RELWITHDEBINFO)
    _fix_cl_options(CMAKE_CXX_FLAGS_MINSIZEREL)

else()
    message(FATAL_ERROR "[xcp] Unknown compiler")
endif()



#
# Make linking as static as possible in MinGW
#
if (MINGW)
    if (COMPILER_GNU)
        add_link_options("-static")
        #add_link_options("-static-libgcc")     # not necessary
        #add_link_options("-static-libstdc++")  # not necessary
    elseif (COMPILER_CLANG)
        add_link_options("-static")
    else()
        message(FATAL_ERROR "[xcp] Unknown compiler under MinGW")
    endif()
endif()
