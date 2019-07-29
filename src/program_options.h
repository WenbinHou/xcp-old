#if !defined(_XCP_PROGRAM_OPTIONS_H_INCLUDED_)
#define _XCP_PROGRAM_OPTIONS_H_INCLUDED_

#if !defined(_XCP_COMMON_H_INCLUDED_)
#error "Please don't directly #include this file. Instead, #include common.h"
#endif  // !defined(_XCP_COMMON_H_INCLUDED_)


//
// For CLI11
//
namespace infra
{
    template<bool _WithRepeats>
    std::istream& operator >>(std::istream& iss, infra::basic_tcp_endpoint<_WithRepeats>& ep)
    {
        std::string value;
        iss >> value;
        ep = infra::basic_tcp_endpoint<_WithRepeats>();
        if (!ep.parse(value)) {
            throw CLI::ConversionError(value, "tcp_endpoint");
        }
        return iss;
    }
}  // namespace infra


namespace xcp
{
    struct host_path
    {
    public:
        bool parse(const std::string& value);

        bool is_remote() const noexcept { return host.has_value(); }

        friend std::istream& operator >>(std::istream& iss, host_path& hp)
        {
            std::string value;
            iss >> value;
            hp = host_path();
            if (!hp.parse(value)) {
                throw CLI::ConversionError(value, "host_path");
            }
            return iss;
        }

    public:
        std::optional<std::string> host { };
        std::string path { };
    };

    struct program_options_defaults
    {
        static constexpr const char SERVER_PORTAL_HOST[] = "[::]";
        static constexpr const uint16_t SERVER_PORTAL_PORT = 62581;

        static constexpr const char SERVER_CHANNEL_HOST[] = "[::]";
        static constexpr const uint16_t SERVER_CHANNEL_PORT = 0;
        static constexpr const size_t SERVER_CHANNEL_REPEATS = 12;

        static constexpr const uint32_t MAX_TRANSFER_BLOCK_SIZE = 1024 * 1024 * 1024;  // 1 GB
    };

    struct base_program_options
    {
    public:
        int arg_verbosity = 0;

    public:
        virtual ~base_program_options() = default;
        virtual void add_options(CLI::App& app);
        virtual bool post_process();
    };


    struct xcp_program_options : base_program_options
    {
    public:
        std::optional<uint16_t> arg_port { };
        host_path arg_from_path;
        host_path arg_to_path;
        std::optional<uint64_t> arg_transfer_block_size { };

        bool is_from_server_to_client;
        infra::tcp_endpoint server_portal;

    public:
        void add_options(CLI::App& app) override;
        bool post_process() override;
    };


    struct xcpd_program_options : base_program_options
    {
    public:
        std::optional<infra::tcp_endpoint> arg_portal { };
        std::vector<infra::tcp_endpoint_repeatable> arg_channels { };

        size_t total_channel_repeats_count { 0 };

    public:
        void add_options(CLI::App& app) override;
        bool post_process() override;
    };

}  // namespace xcp


#endif  // !defined(_XCP_PROGRAM_OPTIONS_H_INCLUDED_)
