#if !defined(_XCP_INFRA_NETWORK_H_INCLUDED_)
#define _XCP_INFRA_NETWORK_H_INCLUDED_

#if !defined(_XCP_INFRA_INFRA_H_INCLUDED_)
#error "Please don't directly #include this file. Instead, #include infra.h"
#endif  // !defined(_XCP_INFRA_INFRA_H_INCLUDED_)


namespace infra
{
    inline void global_initialize_network()
    {
#if PLATFORM_WINDOWS || PLATFORM_CYGWIN
        const WORD wVersionRequested = MAKEWORD(2, 2);
        WSADATA wsaData { };
        int ret = WSAStartup(wVersionRequested, &wsaData);
        if (ret != 0) {
            LOG_ERROR("WSAStartup() failed with {}", ret);
            throw std::system_error(std::error_code(ret, std::system_category()), "WSAStartup() failed");
        }
#elif PLATFORM_LINUX
#else
#   error "Unknown platform"
#endif
}

    inline void global_finalize_network()
    {
#if PLATFORM_WINDOWS || PLATFORM_CYGWIN
        if (WSACleanup() != 0) {
            const int wsagle = WSAGetLastError();
            LOG_ERROR("WSACleanup() failed. WSAGetLastError = {}", wsagle);
            throw std::system_error(std::error_code((int)wsagle, std::system_category()), "WSACleanup() failed");
        }
#elif PLATFORM_LINUX
#else
#   error "Unknown platform"
#endif
    }

}  // namespace infra


#endif  // !defined(_XCP_INFRA_NETWORK_H_INCLUDED_)
