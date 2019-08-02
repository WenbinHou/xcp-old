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
        std::string relative_path { };
        uint64_t file_size = 0;
        uint32_t posix_perm = 0;

        XCP_DEFAULT_SERIALIZATION(relative_path, file_size, posix_perm)
    };

    struct basic_directory_info
    {
        std::string relative_path { };
        uint32_t posix_perm = 0;

        XCP_DEFAULT_SERIALIZATION(relative_path, posix_perm)
    };

    struct basic_transfer_info  // both for source and destination
    {
        bool source_is_directory;
        std::vector<basic_directory_info> dirs_info { };
        std::vector<basic_file_info> files_info { };

        XCP_DEFAULT_SERIALIZATION(source_is_directory, dirs_info, files_info)
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
        std::uint64_t is_recursive;
        infra::user_name_t user;

        std::optional<basic_transfer_info> transfer_info;  // only if from client to server

        XCP_DEFAULT_SERIALIZATION(is_from_server_to_client, server_path, transfer_block_size, is_recursive, user, transfer_info)
    };

    struct message_server_transfer_response : message_base<message_type::SERVER_TRANSFER_RESPONSE>
    {
        int error_code { };
        std::string error_message;

        std::optional<basic_transfer_info> transfer_info;  // only if from server to client

        XCP_DEFAULT_SERIALIZATION(error_code, error_message, transfer_info)
    };

    struct message_transfer_destination_finished : message_base<message_type::TRANSFER_DESTINATION_FINISHED>
    {
        int error_code { };
        std::string error_message;

        XCP_DEFAULT_SERIALIZATION(error_code, error_message)
    };



    template<typename T>
    inline bool message_send(std::shared_ptr<infra::os_socket_t> sock, const T& msg)
    {
        const message_type type = T::type;

        // TODO: optimize for better performance
        std::string binary;
        try {
            std::ostringstream oss(std::ios::binary);
            cereal::PortableBinaryOutputArchive ar(oss);
            ar(msg);
            binary = oss.str();

            if (binary.size() > (uint32_t)INT32_MAX) {
                LOG_ERROR("Message ({} bytes) is too large to send", binary.size());
                return false;
            }
        }
        catch(const std::exception& ex) {
            PANIC_TERMINATE("Serialization exception: {}", ex.what());
        }

        const uint32_t binary_size = (uint32_t)binary.size();

        char buffer[8];
        *(std::uint32_t*)&buffer[0] = htonl((std::uint32_t)type);
        *(std::uint32_t*)&buffer[4] = htonl(binary_size);

        {
            infra::socket_io_vec vec[2];

            // header
            vec[0].ptr = &buffer;
            vec[0].len = sizeof(buffer);

            // message
            vec[1].ptr = binary.data();
            vec[1].len = binary_size;

            if (!sock->sendv(vec)) {
                LOG_ERROR("message_send: sendv() failed");
                return false;
            }
        }

        return true;
    }


    template<typename T>
    inline bool message_recv(std::shared_ptr<infra::os_socket_t> sock, /*out*/ T& msg)
    {
        const message_type expected_type = T::type;

        char buffer[8];
        if (!sock->recv(buffer, sizeof(buffer))) {
            LOG_ERROR("message_recv: recv() message header failed");
            return false;
        }

        const message_type type = (message_type)ntohl(*(std::uint32_t*)&buffer[0]);
        const uint32_t binary_size = ntohl(*(std::uint32_t*)&buffer[4]);

        if (type != expected_type) {
            LOG_ERROR("Expects message type {}, but got message type {}",
                      (std::uint32_t)expected_type, (std::uint32_t)type);
            return false;
        }

        // TODO: optimize for better performance
        std::string binary;
        binary.resize(binary_size);
        if (!sock->recv(binary.data(), binary_size)) {
            LOG_ERROR("message_recv: recv() message body failed");
            return false;
        }

        try {
            std::istringstream iss(binary,std::ios::binary);
            cereal::PortableBinaryInputArchive ar(iss);
            ar(msg);
        }
        catch(const std::exception& ex) {
            PANIC_TERMINATE("Deserialization exception: {}", ex.what());
        }

        return true;
    }

}  // namespace xcp


#endif  // !defined(_XCP_MESSAGE_H_INCLUDED_)
