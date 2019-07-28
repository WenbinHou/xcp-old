#if !defined(_XCP_INFRA_LOGGING_H_INCLUDED_)
#define _XCP_INFRA_LOGGING_H_INCLUDED_

#if !defined(_XCP_INFRA_INFRA_H_INCLUDED_)
#error "Please don't directly #include this file. Instead, #include infra.h"
#endif  // !defined(_XCP_INFRA_INFRA_H_INCLUDED_)


namespace infra
{
    namespace details
    {
        inline std::shared_ptr<spdlog::logger> g_logger { nullptr };
    }  // namespace details

    inline void global_initialize_logging()
    {
        try {
            details::g_logger = spdlog::stderr_color_mt("console", spdlog::color_mode::automatic);
#if PLATFORM_WINDOWS || PLATFORM_LINUX
            details::g_logger->set_pattern("[%Y-%m-%d %T.%e %z] [%t][%s:%#] %^%L: %v%$");
#elif PLATFORM_CYGWIN
            details::g_logger->set_pattern("[%Y-%m-%d %T.%e %z] [-1][%s:%#] %^%L: %v%$");  // threadid is not available in Cygwin
#else
#   error "Unknown platform"
#endif
            details::g_logger->flush_on(spdlog::level::trace);  // TODO: info?
            details::g_logger->set_level(spdlog::level::info);
        }
        catch (const std::exception& ex) {
            fprintf(stdout, "global_initialize_logging() exception: %s", ex.what());
            fflush(stdout);
            throw;
        }
    }

    inline void global_finalize_logging()
    {
        if (details::g_logger) {
            details::g_logger->flush();
            details::g_logger.reset();
        }
    }

    inline void set_logging_verbosity(const int verbosity)
    {
        if (verbosity >= 2) {
            details::g_logger->set_level(spdlog::level::trace);
        }
        else if (verbosity == 1) {
            details::g_logger->set_level(spdlog::level::debug);
        }
        else if (verbosity == 0) {
            details::g_logger->set_level(spdlog::level::info);
        }
        else if (verbosity == -1) {
            details::g_logger->set_level(spdlog::level::warn);
        }
        else {  // verbosity <= -2
            details::g_logger->set_level(spdlog::level::err);
        }
    }


#define LOG_ERROR(...)      SPDLOG_LOGGER_ERROR(::infra::details::g_logger, __VA_ARGS__)
#define LOG_WARN(...)       SPDLOG_LOGGER_WARN(::infra::details::g_logger, __VA_ARGS__)
#define LOG_INFO(...)       SPDLOG_LOGGER_INFO(::infra::details::g_logger, __VA_ARGS__)
#define LOG_DEBUG(...)      SPDLOG_LOGGER_DEBUG(::infra::details::g_logger, __VA_ARGS__)
#define LOG_TRACE(...)      SPDLOG_LOGGER_TRACE(::infra::details::g_logger, __VA_ARGS__)



#if PLATFORM_WINDOWS || PLATFORM_CYGWIN

    namespace details
    {
        inline std::unordered_map<int, const char*> g_getlasterror_message { };  // NOLINT(cert-err58-cpp)
        inline std::shared_mutex g_getlasterror_message_mutex { };
    }  // namespace details

    template<typename TIntOrDword>
    inline const char* str_getlasterror(TIntOrDword const gle)
    {
        static_assert(std::is_same_v<TIntOrDword, int> || std::is_same_v<TIntOrDword, DWORD>);

        {
            std::shared_lock<std::shared_mutex> lock(details::g_getlasterror_message_mutex);
            const auto it = details::g_getlasterror_message.find(gle);
            if (it != details::g_getlasterror_message.end()) {
                return it->second;
            }
        }

        char* msg = nullptr;
        DWORD ret = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK,
            NULL,
            (DWORD)gle,
            MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),  // en-US
            (LPSTR)&msg,
            0,
            NULL);
        if (ret == 0) {
            LOG_ERROR("FormatMessageA(dwMessageId={}) failed. GetLastError = {}", gle, GetLastError());
            return "UNKNOWN_ERROR_MESSAGE";
        }

        LOG_TRACE("Formatting system error code {} got: {}", gle, msg);
        {
            std::unique_lock<std::shared_mutex> lock(details::g_getlasterror_message_mutex);
            details::g_getlasterror_message[gle] = msg;
        }
        return msg;
    }

#elif PLATFORM_LINUX
#else
#   error "Unknown platform"
#endif

}  // namespace infra

#endif  // !defined(_XCP_INFRA_LOGGING_H_INCLUDED_)
