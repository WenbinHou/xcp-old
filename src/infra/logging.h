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
            details::g_logger = spdlog::stdout_color_mt("console", spdlog::color_mode::automatic);
            details::g_logger->set_pattern("[%Y-%m-%d %T.%e %z] [%t][%s:%#] %^%L: %v%$    // %!");
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
}  // namespace infra

#define LOG_ERROR(...)      SPDLOG_LOGGER_ERROR(::infra::details::g_logger, __VA_ARGS__)
#define LOG_WARN(...)       SPDLOG_LOGGER_WARN(::infra::details::g_logger, __VA_ARGS__)
#define LOG_INFO(...)       SPDLOG_LOGGER_INFO(::infra::details::g_logger, __VA_ARGS__)
#define LOG_DEBUG(...)      SPDLOG_LOGGER_DEBUG(::infra::details::g_logger, __VA_ARGS__)
#define LOG_TRACE(...)      SPDLOG_LOGGER_TRACE(::infra::details::g_logger, __VA_ARGS__)


#endif  // !defined(_XCP_INFRA_LOGGING_H_INCLUDED_)
