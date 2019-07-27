#include "common.h"

namespace xcp
{
    template<typename T>
    bool message_send(infra::socket_t const sock, const T& msg)
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
            LOG_ERROR("Serialization exception: {}", ex.what());
            std::abort();
        }

        const uint32_t binary_size = (uint32_t)binary.size();

        char buffer[8];
        *(std::uint32_t*)&buffer[0] = htonl((std::uint32_t)type);
        *(std::uint32_t*)&buffer[4] = htonl(binary_size);

        {
            const int cnt = (int)send(sock, buffer, sizeof(buffer), 0);  // TODO: MSG_MORE
            if (cnt != (int)sizeof(buffer)) {
                LOG_ERROR("send() message prefix expects {}, but returns {}. {}",
                          sizeof(buffer), cnt, infra::socket_error_description());
                return false;
            }
        }

        {
            const int cnt = (int)send(sock, binary.data(), binary_size, 0);
            if (cnt != (int)binary_size) {
                LOG_ERROR("send() message expects {}, but returns {}. {}",
                          binary_size, cnt, infra::socket_error_description());
                return false;
            }
        }

        return true;
    }


    template<typename T>
    bool message_recv(infra::socket_t const sock, /*out*/ T& msg)
    {
        const message_type expected_type = T::type;

        char buffer[8];
        {
            const int cnt = (int)recv(sock, buffer, sizeof(buffer), MSG_WAITALL);
            if (cnt != (int)sizeof(buffer)) {
                LOG_ERROR("recv() message prefix expects {}, but returns {}. {}",
                          sizeof(buffer), cnt, infra::socket_error_description());
                return false;
            }
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
        {
            const int cnt = (int)recv(sock, binary.data(), binary_size, MSG_WAITALL);
            if (cnt != (int)binary_size) {
                LOG_ERROR("recv() message expects {}, but returns {}. {}",
                          binary_size, cnt, infra::socket_error_description());
                return false;
            }
        }

        try {
            std::istringstream iss(binary,std::ios::binary);
            cereal::PortableBinaryInputArchive ar(iss);
            ar(msg);
        }
        catch(const std::exception& ex) {
            LOG_ERROR("Deserialization exception: {}", ex.what());
            std::abort();
        }

        return true;
    }


#define _DECLARE_MESSAGE_SEND_RECV_TEMPLATE(_Message_) \
    template bool message_send<_Message_>(infra::socket_t const sock, const _Message_& msg); \
    template bool message_recv<_Message_>(infra::socket_t const sock, /*out*/ _Message_& msg);

    _DECLARE_MESSAGE_SEND_RECV_TEMPLATE(message_client_hello_request)
    _DECLARE_MESSAGE_SEND_RECV_TEMPLATE(message_server_hello_response)

#undef _DECLARE_MESSAGE_SEND_RECV_TEMPLATE

}  // namespace xcp
