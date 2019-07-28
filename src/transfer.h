#if !defined(_XCP_TRANSFER_H_INCLUDED_)
#define _XCP_TRANSFER_H_INCLUDED_

#if !defined(_XCP_COMMON_H_INCLUDED_)
#error "Please don't directly #include this file. Instead, #include common.h"
#endif  // !defined(_XCP_COMMON_H_INCLUDED_)

namespace xcp
{
    struct transfer_error : std::exception
    {
    public:
        const int error_code;
        const std::string error_message;

    public:
        transfer_error(const int error_code, std::string error_message) noexcept
            : error_code(error_code),
              error_message(std::move(error_message))
        { }
    };

    struct transfer_base : infra::disposable
    {
    public:
        XCP_DISABLE_COPY_CONSTRUCTOR(transfer_base)
        XCP_DISABLE_MOVE_CONSTRUCTOR(transfer_base)
        transfer_base() noexcept = default;

        virtual bool invoke_portal(infra::socket_t sock) = 0;
        virtual bool invoke_channel(infra::socket_t sock) = 0;

    public:
        uint64_t file_size { };  // both for source and destination
    };

    struct transfer_source : transfer_base
    {
    public:
        XCP_DISABLE_COPY_CONSTRUCTOR(transfer_source)
        XCP_DISABLE_MOVE_CONSTRUCTOR(transfer_source)
        explicit transfer_source(const std::string& src_path);  // throws transfer_error

        bool invoke_portal(infra::socket_t sock) override;
        bool invoke_channel(infra::socket_t sock) override;

        void dispose_impl() noexcept override final;
        ~transfer_source() noexcept override final { this->async_dispose(true); }

    private:
        void prepare_transfer_regular_file(const stdfs::path& file_path);

    private:
        static constexpr const uint32_t BLOCK_SIZE = 1024 * 1024 * 1;  // block: 1 MB

        std::atomic_uint64_t _curr_offset = 0;
#if PLATFORM_WINDOWS
        HANDLE _file_handle = INVALID_HANDLE_VALUE;
#elif PLATFORM_LINUX
        int _file_handle = -1;
#else
#   error "Unknown platform"
#endif
    };

    struct transfer_destination : transfer_base
    {
    public:
        XCP_DISABLE_COPY_CONSTRUCTOR(transfer_destination)
        XCP_DISABLE_MOVE_CONSTRUCTOR(transfer_destination)

        transfer_destination(const std::string& src_file_name, const std::string& dst_path);  // throws transfer_error
        void init_file_size(uint64_t file_size, size_t total_channel_repeats_count);  // throws transfer_error

        bool invoke_portal(infra::socket_t sock) override;
        bool invoke_channel(infra::socket_t sock) override;

        void dispose_impl() noexcept override final;
        ~transfer_destination() noexcept override final { this->async_dispose(true); }

    private:
        size_t _total_channel_repeats_count;
        std::atomic_size_t _finished_channel_repeats_count { 0 };
        infra::semaphore _sem_all_channels_finished { 0 };

        stdfs::path _dst_file_path;
        void* _dst_file_mapped = nullptr;
#if PLATFORM_WINDOWS
        HANDLE _file_handle = INVALID_HANDLE_VALUE;
#elif PLATFORM_LINUX
        int _file_handle = -1;
#else
#   error "Unknown platform"
#endif
    };

}  // namespace xcp

#endif  // !defined(_XCP_TRANSFER_H_INCLUDED_)
