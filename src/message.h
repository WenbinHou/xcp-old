#if !defined(_XCP_MESSAGE_H_INCLUDED_)
#define _XCP_MESSAGE_H_INCLUDED_

#if !defined(_XCP_COMMON_H_INCLUDED_)
#error "Please don't directly #include this file. Instead, #include common.h"
#endif  // !defined(_XCP_COMMON_H_INCLUDED_)


namespace xcp
{
    struct portal_protocol
    {
        static constexpr const uint32_t GREETING_MAGIC_1 = 0x31c1b3f6;
        static constexpr const uint32_t GREETING_MAGIC_2 = 0xe5fd020e;

        struct role
        {
            static constexpr const uint32_t ROLE_PORTAL = 0x8739e779;
            static constexpr const uint32_t ROLE_CHANNEL = 0x7fbc389b;
        };

        struct version
        {
            static constexpr const uint16_t INVALID = 0x0000;
            static constexpr const uint16_t V1 = 0x0001;
        };
    };

    struct basic_file_info
    {
        std::string relative_path;
        uint64_t file_size;
        uint32_t posix_perm;

        XCP_DEFAULT_SERIALIZATION(relative_path, file_size, posix_perm)
    };

    enum class message_type : std::uint32_t
    {
        SERVER_INFORMATION = 0,
        CLIENT_TRANSFER_REQUEST = 1,
        SERVER_TRANSFER_RESPONSE = 2,
        TRANSFER_DESTINATION_FINISHED = 3,
    };

    template<message_type _MessageType>
    struct message_base
    {
        static constexpr const message_type type = _MessageType;
    };

    struct message_server_information : message_base<message_type::SERVER_INFORMATION>
    {
        std::vector<std::tuple<infra::tcp_sockaddr, size_t>> server_channels;

        XCP_DEFAULT_SERIALIZATION(server_channels)
    };

    struct message_client_transfer_request : message_base<message_type::CLIENT_TRANSFER_REQUEST>
    {
        bool is_from_server_to_client;
        std::string server_path;
        std::uint64_t transfer_block_size;

        std::optional<basic_file_info> file_info;  // only if from client to server

        XCP_DEFAULT_SERIALIZATION(is_from_server_to_client, server_path, transfer_block_size, file_info)
    };

    struct message_server_transfer_response : message_base<message_type::SERVER_TRANSFER_RESPONSE>
    {
        int error_code { };
        std::string error_message;

        std::optional<basic_file_info> file_info;  // only if from server to client

        XCP_DEFAULT_SERIALIZATION(error_code, error_message, file_info)
    };

    struct message_transfer_destination_finished : message_base<message_type::TRANSFER_DESTINATION_FINISHED>
    {
        int error_code { };
        std::string error_message;

        XCP_DEFAULT_SERIALIZATION(error_code, error_message)
    };


    template<typename T>
    bool message_send(std::shared_ptr<infra::os_socket_t> sock, const T& msg);

    template<typename T>
    bool message_recv(std::shared_ptr<infra::os_socket_t> sock, /*out*/ T& msg);

}  // namespace xcp


#endif  // !defined(_XCP_MESSAGE_H_INCLUDED_)
