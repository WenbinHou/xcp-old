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
    struct program_options_defaults
    {
        static constexpr const char SERVER_PORTAL_HOST[] = "[::]";
        static constexpr const uint16_t SERVER_PORTAL_PORT = 62581;

        static constexpr const char SERVER_CHANNEL_HOST[] = "[::]";
        static constexpr const uint16_t SERVER_CHANNEL_PORT = 0;
        static constexpr const size_t SERVER_CHANNEL_REPEATS = 8;
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

    public:
        void add_options(CLI::App& app) override;
        bool post_process() override;
    };


    struct xcpd_program_options : base_program_options
    {
    public:
        std::optional<infra::tcp_endpoint> arg_portal { };
        std::vector<infra::tcp_endpoint_repeatable> arg_channels { };

    public:
        void add_options(CLI::App& app) override;
        bool post_process() override;
    };

}  // namespace xcp


#endif  // !defined(_XCP_PROGRAM_OPTIONS_H_INCLUDED_)
