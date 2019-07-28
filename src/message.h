#if !defined(_XCP_MESSAGE_H_INCLUDED_)
#define _XCP_MESSAGE_H_INCLUDED_

#if !defined(_XCP_COMMON_H_INCLUDED_)
#error "Please don't directly #include this file. Instead, #include common.h"
#endif  // !defined(_XCP_COMMON_H_INCLUDED_)


namespace xcp
{
    struct basic_file_info
    {
        uint64_t file_size;
        uint32_t posix_perm;

        XCP_DEFAULT_SERIALIZATION(file_size, posix_perm);
    };

    enum class message_type : std::uint32_t
    {
        CLIENT_HELLO_REQUEST,
        SERVER_HELLO_RESPONSE,
        SERVER_READY_TO_TRANSFER,
        TRANSFER_DESTINATION_FINISHED,
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

        std::optional<basic_file_info> file_info;  // only if from client to server

        XCP_DEFAULT_SERIALIZATION(is_from_server_to_client, client_file_name, server_path, file_info)
    };

    struct message_server_hello_response : message_base<message_type::SERVER_HELLO_RESPONSE>
    {
        int error_code;
        std::string error_message;

        std::vector<std::tuple<infra::tcp_sockaddr, size_t>> server_channels;

        std::optional<basic_file_info> file_info;  // only if from server to client

        XCP_DEFAULT_SERIALIZATION(error_code, error_message, server_channels, file_info)
    };

    struct message_server_ready_to_transfer : message_base<message_type::SERVER_READY_TO_TRANSFER>
    {
        int error_code { };  // always zero
        std::string error_message;

        XCP_DEFAULT_SERIALIZATION(error_code, error_message)
    };

    struct message_transfer_destination_finished : message_base<message_type::TRANSFER_DESTINATION_FINISHED>
    {
        int error_code { };
        std::string error_message;

        XCP_DEFAULT_SERIALIZATION(error_code, error_message)
    };


    template<typename T>
    bool message_send(infra::socket_t sock, const T& msg);

    template<typename T>
    bool message_recv(infra::socket_t sock, /*out*/ T& msg);

}  // namespace xcp


#endif  // !defined(_XCP_MESSAGE_H_INCLUDED_)
