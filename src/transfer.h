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

        virtual bool invoke_portal(std::shared_ptr<infra::os_socket_t> sock) = 0;
        virtual bool invoke_channel(std::shared_ptr<infra::os_socket_t> sock) = 0;

        const basic_transfer_info& get_transfer_info() const noexcept
        {
            ASSERT(_info.has_value());
            return _info.value();
        }

        virtual ~transfer_base() noexcept
        {
            _info.reset();
        }

    public:
        // NOTE: report_progress_callback might be called by multiple threads concurrently
        std::function<void(uint64_t /*transferred*/, uint64_t /*total*/)> report_progress_callback { nullptr };

    protected:
        std::atomic_uint64_t _transferred_size { 0 };
        uint64_t _total_size { 0 };

        std::optional<basic_transfer_info> _info { };  // both for source and destination
    };

    struct transfer_source : transfer_base
    {
    public:
        XCP_DISABLE_COPY_CONSTRUCTOR(transfer_source)
        XCP_DISABLE_MOVE_CONSTRUCTOR(transfer_source)
        explicit transfer_source(const std::string& src_path, uint64_t transfer_block_size);  // throws transfer_error

        bool invoke_portal(std::shared_ptr<infra::os_socket_t> sock) override;
        bool invoke_channel(std::shared_ptr<infra::os_socket_t> sock) override;

        void dispose_impl() noexcept override final;
        ~transfer_source() noexcept override final { this->async_dispose(true); }

    private:
        struct send_file_context
        {
        public:
            XCP_DISABLE_COPY_CONSTRUCTOR(send_file_context)
            XCP_DEFAULT_MOVE_ASSIGN(send_file_context)
            send_file_context() = default;
            send_file_context(send_file_context&& other) noexcept
            {
                this->curr_offset = other.curr_offset.load();
                other.curr_offset = 0;

                if (this->file_handle != INVALID_FILE_HANDLE) this->close_handle();
                this->file_handle = other.file_handle;
                other.file_handle = INVALID_FILE_HANDLE;
            }

            void close_handle() noexcept
            {
#if PLATFORM_WINDOWS
                if (this->file_handle != INVALID_FILE_HANDLE) {
                    (void)CloseHandle(this->file_handle);
                    this->file_handle = INVALID_FILE_HANDLE;
                }
#elif PLATFORM_LINUX
                if (this->file_handle != INVALID_FILE_HANDLE) {
                    (void)close(this->file_handle);
                    this->file_handle = INVALID_FILE_HANDLE;
                }
#else
#   error "Unknown platform"
#endif
            }

        public:
            std::atomic_uint64_t curr_offset = 0;
#if PLATFORM_WINDOWS
            static constexpr const HANDLE INVALID_FILE_HANDLE = INVALID_HANDLE_VALUE;
            HANDLE file_handle = INVALID_HANDLE_VALUE;
#elif PLATFORM_LINUX
            static constexpr const int INVALID_FILE_HANDLE = -1;
            int file_handle = -1;
#else
#   error "Unknown platform"
#endif
        };

    private:
        void internal_open_regular_file(const stdfs::path& file_path, /*out*/send_file_context& ctx, /*out*/basic_file_info& fi);  // throws transfer_error
        void prepare_transfer_regular_file(const stdfs::path& file_path);  // throws transfer_error
        void prepare_transfer_directory(const stdfs::path& dir_path);  // throws transfer_error

    private:
        static constexpr const uint32_t TRANSFER_BLOCK_SIZE_INIT = 1024 * 1024 * 1;  // block: 1 MB
        static_assert(TRANSFER_BLOCK_SIZE_INIT <= program_options_defaults::MAX_TRANSFER_BLOCK_SIZE);

        const bool _is_transfer_block_size_fixed;
        const uint32_t _init_transfer_block_size;

        std::vector<send_file_context> _send_ctx;
    };

    struct transfer_destination : transfer_base
    {
    public:
        XCP_DISABLE_COPY_CONSTRUCTOR(transfer_destination)
        XCP_DISABLE_MOVE_CONSTRUCTOR(transfer_destination)

        explicit transfer_destination(std::string dst_path);  // throws transfer_error
        void init_transfer_info(const basic_transfer_info& transfer_info, size_t total_channel_repeats_count);  // throws transfer_error

        bool invoke_portal(std::shared_ptr<infra::os_socket_t> sock) override;
        bool invoke_channel(std::shared_ptr<infra::os_socket_t> sock) override;

        void dispose_impl() noexcept override final;
        ~transfer_destination() noexcept override final { this->async_dispose(true); }

    private:
        struct recv_file_context
        {
        public:
            XCP_DISABLE_COPY_CONSTRUCTOR(recv_file_context)
            XCP_DEFAULT_MOVE_ASSIGN(recv_file_context)
            recv_file_context() = default;
            recv_file_context(recv_file_context&& other) noexcept
            {
                // Move dst_file_mapped, mapped_length
                if (this->dst_file_mapped != nullptr) {
                    this->unmap_file();
                }
                this->dst_file_mapped = other.dst_file_mapped;
                other.dst_file_mapped = nullptr;
                this->mapped_length = other.mapped_length;
                other.mapped_length = 0;

                // Move file_handle
                if (this->file_handle != INVALID_FILE_HANDLE) {
                    this->close_handle();
                }
                this->file_handle = other.file_handle;
                other.file_handle = INVALID_FILE_HANDLE;
            }

            void close_handle() noexcept
            {
#if PLATFORM_WINDOWS
                if (this->file_handle != INVALID_FILE_HANDLE) {
                    (void)CloseHandle(this->file_handle);
                    this->file_handle = INVALID_FILE_HANDLE;
                }
#elif PLATFORM_LINUX
                if (this->file_handle != INVALID_FILE_HANDLE) {
                    (void)close(this->file_handle);
                    this->file_handle = INVALID_FILE_HANDLE;
                }
#else
#   error "Unknown platform"
#endif
            }

            void unmap_file() noexcept
            {
#if PLATFORM_WINDOWS
                if (this->dst_file_mapped != nullptr) {
                    (void)FlushViewOfFile(this->dst_file_mapped, mapped_length);
                    (void)UnmapViewOfFile(this->dst_file_mapped);
                    this->dst_file_mapped = nullptr;
                    this->mapped_length = 0;
                }
#elif PLATFORM_LINUX
                if (this->file_handle != INVALID_FILE_HANDLE) {
                    msync(this->dst_file_mapped, this->mapped_length, MS_ASYNC);
                    munmap(this->dst_file_mapped, this->mapped_length);
                    this->dst_file_mapped = nullptr;
                    this->mapped_length = 0;
                }
#else
#   error "Unknown platform"
#endif
            }

        public:
            size_t mapped_length = 0;
            void* dst_file_mapped = nullptr;
#if PLATFORM_WINDOWS
            static constexpr const HANDLE INVALID_FILE_HANDLE = INVALID_HANDLE_VALUE;
            HANDLE file_handle = INVALID_HANDLE_VALUE;
#elif PLATFORM_LINUX
            static constexpr const int INVALID_FILE_HANDLE = -1;
            int file_handle = -1;
#else
#   error "Unknown platform"
#endif
        };

        
    private:
        void internal_open_regular_file(const stdfs::path& file_path, const basic_file_info& fi, /*out*/recv_file_context& ctx);  // throws transfer_error
        void prepare_transfer_regular_file();  // throws transfer_error
        void prepare_transfer_directory();  // throws transfer_error

    private:
        infra::gate_guard _gate_all_channels_finished { };

        const stdfs::path _requested_dst_path;

        std::vector<recv_file_context> _recv_ctx;
    };

}  // namespace xcp

#endif  // !defined(_XCP_TRANSFER_H_INCLUDED_)
