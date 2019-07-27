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

    struct transfer_base
    {
    public:
        XCP_DISABLE_COPY_CONSTRUCTOR(transfer_base)
        XCP_DISABLE_MOVE_CONSTRUCTOR(transfer_base)
        transfer_base() noexcept = default;
        virtual ~transfer_base() noexcept = default;

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
        virtual ~transfer_source() noexcept = default;

        transfer_source(const std::string& src_path);  // throws transfer_error

    public:
        bool invoke_portal(infra::socket_t sock) override;
        bool invoke_channel(infra::socket_t sock) override;
    };

    struct transfer_destination : transfer_base
    {
    public:
        XCP_DISABLE_COPY_CONSTRUCTOR(transfer_destination)
        XCP_DISABLE_MOVE_CONSTRUCTOR(transfer_destination)
        virtual ~transfer_destination() noexcept = default;

        transfer_destination(const std::string& src_file_name, const std::string& dst_path);  // throws transfer_error
        void init_file_size(uint64_t file_size);  // throws transfer_error

    public:
        bool invoke_portal(infra::socket_t sock) override;
        bool invoke_channel(infra::socket_t sock) override;
    };

}  // namespace xcp

#endif  // !defined(_XCP_TRANSFER_H_INCLUDED_)
