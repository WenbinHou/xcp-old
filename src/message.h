#if !defined(_XCP_MESSAGE_H_INCLUDED_)
#define _XCP_MESSAGE_H_INCLUDED_

#if !defined(_XCP_COMMON_H_INCLUDED_)
#error "Please don't directly #include this file. Instead, #include common.h"
#endif  // !defined(_XCP_COMMON_H_INCLUDED_)


namespace xcp
{
    enum class message_type : std::uint32_t
    {
        CLIENT_HELLO_REQUEST,
        SERVER_HELLO_RESPONSE,
    };

    template<message_type _MessageType>
    struct message_base
    {
        static constexpr const message_type type = _MessageType;
    };

    struct message_client_hello_request : message_base<message_type::CLIENT_HELLO_REQUEST>
    {
        bool is_from_server_to_client;
        std::string client_file_name;
        std::string server_path;

        XCP_DEFAULT_SERIALIZATION(is_from_server_to_client, client_file_name, server_path)
    };

    struct message_server_hello_response : message_base<message_type::SERVER_HELLO_RESPONSE>
    {
        int error_code;
        std::string error_message;

        std::vector<std::tuple<infra::tcp_sockaddr, size_t>> server_channels;
        uint64_t file_size;
        // TODO: more file information (like timestamps, etc)

        XCP_DEFAULT_SERIALIZATION(error_code, error_message, server_channels, file_size)
    };


    template<typename T>
    bool message_send(infra::socket_t sock, const T& msg);

    template<typename T>
    bool message_recv(infra::socket_t sock, /*out*/ T& msg);

}  // namespace xcp


#endif  // !defined(_XCP_MESSAGE_H_INCLUDED_)
