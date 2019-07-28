#if !defined(_XCP_INFRA_PREDEF_H_INCLUDED_)
#define _XCP_INFRA_PREDEF_H_INCLUDED_

#if !defined(_XCP_INFRA_INFRA_H_INCLUDED_)
#error "Please don't directly #include this file. Instead, #include infra.h"
#endif  // !defined(_XCP_INFRA_INFRA_H_INCLUDED_)


//
// Make sure one of the following is true:
//
//  PLATFORM_WINDOWS
//  PLATFORM_LINUX
//
#if PLATFORM_WINDOWS
//  Build on native Windows (including MinGW)
#elif PLATFORM_CYGWIN
//  Build on Linux
#elif PLATFORM_LINUX
//  Build on Linux
#else
#   error "Unknown platform"
#endif


//
// Define some macros on Windows
//
#if PLATFORM_WINDOWS || PLATFORM_CYGWIN

#   if !defined(_WIN32_WINNT)
#       define _WIN32_WINNT 0x0601  // Windows 7
#   endif  // !defined(_WIN32_WINNT)

#   if !defined(NOMINMAX)
#       define NOMINMAX 1
#   endif  // !defined(NOMINMAX)

//  Disable pedantic warnings on MSVC
#   if !defined(_CRT_SECURE_NO_WARNINGS)
#       define _CRT_SECURE_NO_WARNINGS 1
#   endif  // !defined(_CRT_SECURE_NO_WARNINGS)
#   if !defined(_CRT_NONSTDC_NO_WARNINGS)
#       define _CRT_NONSTDC_NO_WARNINGS 1
#   endif  // !defined(_CRT_NONSTDC_NO_WARNINGS)

#elif PLATFORM_LINUX

#   if !defined(_GNU_SOURCE)
#       define _GNU_SOURCE 1
#   endif

#else
#   error "Unknown platform"

#endif


//
// Header files
//
#if PLATFORM_WINDOWS || PLATFORM_CYGWIN

#   include <winsock2.h>  // This must be put before <windows.h>
#   include <ws2tcpip.h>
#   include <mswsock.h>

#   include <windows.h>
#   include <shlwapi.h>
#   include <shlobj.h>
#   include <aclapi.h>
#   include <userenv.h>

#   if PLATFORM_WINDOWS
#       include <sys/types.h>
#       include <sys/stat.h>
#       include <direct.h>
#       include <fcntl.h>
#       include <io.h>
#   elif PLATFORM_CYGWIN
#       include <unistd.h>
#       include <semaphore.h>
#   else
#       error "Unknown platform"
#   endif

#elif PLATFORM_LINUX

#   include <sys/eventfd.h>
#   include <sys/file.h>
#   include <sys/mman.h>
#   include <sys/sendfile.h>
#   include <sys/stat.h>
#   include <sys/time.h>
#   include <sys/types.h>
#   include <sys/wait.h>
#   include <arpa/inet.h>
#   include <fcntl.h>
#   include <unistd.h>
#   include <semaphore.h>
#   include <strings.h>
#   include <syscall.h>
#   include <netdb.h>

#else
#   error "Unknown platform"

#endif


//
// Standard C headers
//
#include <cerrno>
#include <cinttypes>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>


//
// Common C++ headers
//
#include <algorithm>
#include <any>
#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <fstream>
#include <functional>
#include <future>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <random>
#include <regex>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>


#if __has_include(<filesystem>)
#   include <filesystem>
    namespace stdfs = std::filesystem;
#elif __has_include(<experimental/filesystem>)
#   include <experimental/filesystem>
    namespace stdfs = std::experimental::filesystem;
#else
#   error "Can't find C++ filesystem implementation"
#endif


//
// Third-party headers
//

// Push: disable specific warnings for third-party libraries
#if COMPILER_GNU
#   pragma GCC diagnostic push

//  warning: implicit capture of 'this' via '[=]' is deprecated in C++20 [-Wdeprecated]
#   pragma GCC diagnostic ignored "-Wdeprecated"

#elif COMPILER_CLANG || COMPILER_CLANGCL
#   pragma clang diagnostic push

//  See https://clang.llvm.org/docs/DiagnosticsReference.html#wdeprecated
#   pragma clang diagnostic ignored "-Wdeprecated"

#elif COMPILER_MSVC
#else
#   error "Unknown compiler"
#endif


// CLI11
#include <CLI11.hpp>

// spdlog
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

// cereal
#if !defined(CEREAL_THREAD_SAFE)
#   define CEREAL_THREAD_SAFE 1
#else
#   if !CEREAL_THREAD_SAFE
#       error "CEREAL_THREAD_SAFE should be true-like"
#   endif
#endif
#include <cereal/cereal.hpp>
#include <cereal/access.hpp>
#include <cereal/archives/portable_binary.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/atomic.hpp>
#include <cereal/types/common.hpp>
#include <cereal/types/deque.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/queue.hpp>
#include <cereal/types/stack.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/set.hpp>
#include <cereal/types/tuple.hpp>
#include <cereal/types/unordered_map.hpp>
#include <cereal/types/unordered_set.hpp>
#include <cereal/types/vector.hpp>


// Pop: restore specific warnings for third-party libraries
#if COMPILER_GNU
#   pragma GCC diagnostic pop

#elif COMPILER_CLANG || COMPILER_CLANGCL
#   pragma clang diagnostic pop

#elif COMPILER_MSVC
#else
#   error "Unknown compiler"
#endif


#endif  // !defined(_XCP_INFRA_PREDEF_H_INCLUDED_)
