#include "rtmp_publisher.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "flv-muxer.h"
#include "flv-proto.h"
#include "mpi_venc_api.h"

using namespace std::chrono_literals;

namespace {
constexpr size_t kHandshakeSize = 1536;
constexpr uint32_t kOutputChunkSize = 4096;
constexpr size_t kVideoQueueSize = 12;
constexpr uint32_t kAckWindow = 2500000;

void put_be16(std::vector<uint8_t> &out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

void put_be24(std::vector<uint8_t> &out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

void put_be32(std::vector<uint8_t> &out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value >> 24));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

void put_le32(std::vector<uint8_t> &out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 24));
}

uint32_t read_be24(const uint8_t *data) {
    return (static_cast<uint32_t>(data[0]) << 16) |
           (static_cast<uint32_t>(data[1]) << 8) | data[2];
}

uint32_t read_be32(const uint8_t *data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) | data[3];
}

uint32_t read_le32(const uint8_t *data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

void amf_string(std::vector<uint8_t> &out, const std::string &value) {
    out.push_back(0x02);
    put_be16(out, static_cast<uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

void amf_key(std::vector<uint8_t> &out, const std::string &key) {
    put_be16(out, static_cast<uint16_t>(key.size()));
    out.insert(out.end(), key.begin(), key.end());
}

void amf_number(std::vector<uint8_t> &out, double value) {
    out.push_back(0x00);
    uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    for (int shift = 56; shift >= 0; shift -= 8)
        out.push_back(static_cast<uint8_t>(bits >> shift));
}

void amf_boolean(std::vector<uint8_t> &out, bool value) {
    out.push_back(0x01);
    out.push_back(value ? 1 : 0);
}

void amf_null(std::vector<uint8_t> &out) {
    out.push_back(0x05);
}

void amf_property_string(std::vector<uint8_t> &out, const char *key,
                         const std::string &value) {
    amf_key(out, key);
    amf_string(out, value);
}

void amf_property_number(std::vector<uint8_t> &out, const char *key,
                         double value) {
    amf_key(out, key);
    amf_number(out, value);
}

void amf_property_boolean(std::vector<uint8_t> &out, const char *key,
                          bool value) {
    amf_key(out, key);
    amf_boolean(out, value);
}

void amf_object_end(std::vector<uint8_t> &out) {
    out.push_back(0x00);
    out.push_back(0x00);
    out.push_back(0x09);
}

bool amf_read_string(const std::vector<uint8_t> &data, size_t &offset,
                     std::string &value) {
    if (offset + 3 > data.size() || data[offset++] != 0x02)
        return false;
    uint16_t length = (static_cast<uint16_t>(data[offset]) << 8) |
                      data[offset + 1];
    offset += 2;
    if (offset + length > data.size())
        return false;
    value.assign(reinterpret_cast<const char *>(data.data() + offset),
                 length);
    offset += length;
    return true;
}

bool amf_read_number(const std::vector<uint8_t> &data, size_t &offset,
                     double &value) {
    if (offset + 9 > data.size() || data[offset++] != 0x00)
        return false;
    uint64_t bits = 0;
    for (int i = 0; i < 8; ++i)
        bits = (bits << 8) | data[offset++];
    std::memcpy(&value, &bits, sizeof(value));
    return true;
}

bool payload_contains(const std::vector<uint8_t> &payload,
                      const char *text) {
    const uint8_t *begin = reinterpret_cast<const uint8_t *>(text);
    const uint8_t *end = begin + std::strlen(text);
    return std::search(payload.begin(), payload.end(), begin, end) !=
           payload.end();
}

struct ParsedRtmpUrl {
    std::string host;
    std::string app;
    std::string stream;
    std::string tc_url;
    uint16_t port{1935};
};

bool parse_rtmp_url(const std::string &url, ParsedRtmpUrl &parsed,
                    std::string &error) {
    constexpr const char *prefix = "rtmp://";
    if (url.compare(0, std::strlen(prefix), prefix) != 0) {
        error = "Address must start with rtmp://";
        return false;
    }
    size_t authority_start = std::strlen(prefix);
    size_t path_start = url.find('/', authority_start);
    if (path_start == std::string::npos || path_start + 1 >= url.size()) {
        error = "RTMP address needs a stream path";
        return false;
    }
    std::string authority = url.substr(authority_start,
                                       path_start - authority_start);
    size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
        parsed.host = authority.substr(0, colon);
        char *end = nullptr;
        long port = std::strtol(authority.c_str() + colon + 1, &end, 10);
        if (end == nullptr || *end != '\0' || port <= 0 || port > 65535) {
            error = "Invalid RTMP port";
            return false;
        }
        parsed.port = static_cast<uint16_t>(port);
    } else {
        parsed.host = authority;
    }
    std::string path = url.substr(path_start + 1);
    while (!path.empty() && path.back() == '/')
        path.pop_back();
    if (parsed.host.empty() || path.empty()) {
        error = "Use rtmp://server/stream or rtmp://server/app/stream";
        return false;
    }
    size_t stream_separator = path.rfind('/');
    if (stream_separator == std::string::npos) {
        /* Servers such as MediaMTX expose root-level paths like /live.
         * Connect to the root application and publish "live" as the stream
         * name so the public playback address remains exactly /live. */
        parsed.app.clear();
        parsed.stream = path;
        parsed.tc_url = "rtmp://" + authority;
    } else {
        if (stream_separator == 0 || stream_separator + 1 >= path.size()) {
            error = "Invalid RTMP stream path";
            return false;
        }
        parsed.app = path.substr(0, stream_separator);
        parsed.stream = path.substr(stream_separator + 1);
        parsed.tc_url = "rtmp://" + authority + "/" + parsed.app;
    }
    return true;
}
}  // namespace

class RtmpPublisher::Client {
public:
    ~Client() { Close(); }

    bool ConnectAndPublish(const std::string &url, std::string &error) {
        Close();
        ParsedRtmpUrl target;
        if (!parse_rtmp_url(url, target, error))
            return false;
        if (!OpenSocket(target, error) || !Handshake(error)) {
            Close();
            return false;
        }

        std::vector<uint8_t> chunk_size;
        put_be32(chunk_size, kOutputChunkSize);
        if (!SendMessage(2, 1, 0, 0, chunk_size.data(),
                         chunk_size.size(), error)) {
            Close();
            return false;
        }

        std::vector<uint8_t> window;
        put_be32(window, kAckWindow);
        if (!SendMessage(2, 5, 0, 0, window.data(), window.size(), error) ||
            !SendConnect(target, error) || !WaitForResult(1.0, nullptr,
                                                          error)) {
            Close();
            return false;
        }

        SendSimpleCommand("releaseStream", 2.0, target.stream, 0, error);
        SendSimpleCommand("FCPublish", 3.0, target.stream, 0, error);

        std::vector<uint8_t> create;
        amf_string(create, "createStream");
        amf_number(create, 4.0);
        amf_null(create);
        if (!SendMessage(3, 20, 0, 0, create.data(), create.size(), error)) {
            Close();
            return false;
        }
        double stream_id = 0;
        if (!WaitForResult(4.0, &stream_id, error) || stream_id < 1) {
            Close();
            return false;
        }
        stream_id_ = static_cast<uint32_t>(stream_id);

        std::vector<uint8_t> publish;
        amf_string(publish, "publish");
        amf_number(publish, 5.0);
        amf_null(publish);
        amf_string(publish, target.stream);
        amf_string(publish, "live");
        if (!SendMessage(5, 20, stream_id_, 0, publish.data(),
                         publish.size(), error) || !WaitForPublish(error)) {
            Close();
            return false;
        }

        connected_.store(true);
        reader_ = std::thread(&Client::ReaderLoop, this);
        std::printf("[rtmp-stream] publishing to %s\n", url.c_str());
        return true;
    }

    bool SendVideo(const void *data, size_t bytes, uint32_t timestamp,
                   std::string &error) {
        if (!connected_.load()) {
            error = "RTMP connection closed";
            return false;
        }
        if (!SendMessage(6, 9, stream_id_, timestamp,
                         static_cast<const uint8_t *>(data), bytes, error)) {
            connected_.store(false);
            ShutdownSocket();
            return false;
        }
        return true;
    }

    bool Connected() const { return connected_.load(); }

    void Interrupt() {
        connected_.store(false);
        ShutdownSocket();
    }

    void Close() {
        connected_.store(false);
        ShutdownSocket();
        if (reader_.joinable() &&
            reader_.get_id() != std::this_thread::get_id())
            reader_.join();
        std::lock_guard<std::mutex> lock(send_mutex_);
        if (socket_ >= 0) {
            close(socket_);
            socket_ = -1;
        }
        chunks_.clear();
        stream_id_ = 0;
        input_chunk_size_ = 128;
        bytes_received_ = 0;
        last_acknowledged_ = 0;
    }

private:
    struct Message {
        uint8_t type{0};
        uint32_t stream_id{0};
        uint32_t timestamp{0};
        std::vector<uint8_t> payload;
    };

    struct ChunkState {
        bool valid{false};
        uint8_t last_format{0};
        uint32_t timestamp{0};
        uint32_t timestamp_delta{0};
        uint32_t length{0};
        uint8_t type{0};
        uint32_t stream_id{0};
        size_t received{0};
        bool extended_timestamp{false};
        std::vector<uint8_t> payload;
    };

    bool OpenSocket(const ParsedRtmpUrl &target, std::string &error) {
        char port_text[8];
        std::snprintf(port_text, sizeof(port_text), "%u", target.port);
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo *addresses = nullptr;
        int lookup = getaddrinfo(target.host.c_str(), port_text, &hints,
                                 &addresses);
        if (lookup != 0 || addresses == nullptr) {
            error = "Cannot resolve RTMP server";
            return false;
        }
        for (addrinfo *item = addresses; item != nullptr;
             item = item->ai_next) {
            int fd = socket(item->ai_family, item->ai_socktype,
                            item->ai_protocol);
            if (fd < 0)
                continue;
            timeval timeout{3, 0};
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                       sizeof(timeout));
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                       sizeof(timeout));
            if (connect(fd, item->ai_addr, item->ai_addrlen) == 0) {
                socket_ = fd;
                break;
            }
            close(fd);
        }
        freeaddrinfo(addresses);
        if (socket_ < 0) {
            error = "Cannot connect to RTMP server";
            return false;
        }
        return true;
    }

    int ReceiveExact(void *buffer, size_t bytes) {
        size_t received = 0;
        auto *destination = static_cast<uint8_t *>(buffer);
        while (received < bytes) {
            ssize_t result = recv(socket_, destination + received,
                                  bytes - received, 0);
            if (result > 0) {
                received += static_cast<size_t>(result);
                bytes_received_ += static_cast<size_t>(result);
                continue;
            }
            if (result < 0 && errno == EINTR)
                continue;
            if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) &&
                received == 0)
                return 1;
            return -1;
        }
        return 0;
    }

    bool SendAll(const void *buffer, size_t bytes, std::string &error) {
        const auto *source = static_cast<const uint8_t *>(buffer);
        size_t sent = 0;
        while (sent < bytes) {
            ssize_t result = send(socket_, source + sent, bytes - sent, 0);
            if (result > 0) {
                sent += static_cast<size_t>(result);
                continue;
            }
            if (result < 0 && errno == EINTR)
                continue;
            error = "RTMP network send failed";
            return false;
        }
        return true;
    }

    bool Handshake(std::string &error) {
        std::array<uint8_t, 1 + kHandshakeSize> c0c1{};
        c0c1[0] = 3;
        uint32_t now = static_cast<uint32_t>(time(nullptr));
        c0c1[1] = static_cast<uint8_t>(now >> 24);
        c0c1[2] = static_cast<uint8_t>(now >> 16);
        c0c1[3] = static_cast<uint8_t>(now >> 8);
        c0c1[4] = static_cast<uint8_t>(now);
        for (size_t i = 9; i < c0c1.size(); ++i)
            c0c1[i] = static_cast<uint8_t>((i * 73U + now) & 0xFFU);
        if (!SendAll(c0c1.data(), c0c1.size(), error))
            return false;
        std::array<uint8_t, 1 + kHandshakeSize * 2> response{};
        if (ReceiveExact(response.data(), response.size()) != 0 ||
            response[0] != 3) {
            error = "RTMP handshake failed";
            return false;
        }
        if (!SendAll(response.data() + 1, kHandshakeSize, error))
            return false;
        return true;
    }

    bool SendMessage(uint8_t chunk_stream_id, uint8_t type,
                     uint32_t message_stream_id, uint32_t timestamp,
                     const uint8_t *payload, size_t bytes,
                     std::string &error) {
        if (socket_ < 0 || chunk_stream_id < 2 || chunk_stream_id > 63 ||
            bytes > 0xFFFFFFU) {
            error = "Invalid RTMP message";
            return false;
        }
        std::lock_guard<std::mutex> lock(send_mutex_);
        uint32_t encoded_timestamp = std::min(timestamp, 0xFFFFFFU);
        size_t offset = 0;
        bool first = true;
        do {
            std::vector<uint8_t> header;
            if (first) {
                header.push_back(chunk_stream_id);
                put_be24(header, encoded_timestamp);
                put_be24(header, static_cast<uint32_t>(bytes));
                header.push_back(type);
                put_le32(header, message_stream_id);
            } else {
                header.push_back(static_cast<uint8_t>(0xC0 |
                                                       chunk_stream_id));
            }
            if (timestamp >= 0xFFFFFFU)
                put_be32(header, timestamp);
            if (!SendAll(header.data(), header.size(), error))
                return false;
            size_t part = std::min<size_t>(kOutputChunkSize, bytes - offset);
            if (part > 0 && !SendAll(payload + offset, part, error))
                return false;
            offset += part;
            first = false;
        } while (offset < bytes);
        return true;
    }

    bool SendConnect(const ParsedRtmpUrl &target, std::string &error) {
        std::vector<uint8_t> command;
        amf_string(command, "connect");
        amf_number(command, 1.0);
        command.push_back(0x03);
        amf_property_string(command, "app", target.app);
        amf_property_string(command, "flashVer",
                            "FMLE/3.0 (compatible; CanMV K230)");
        amf_property_string(command, "tcUrl", target.tc_url);
        amf_property_boolean(command, "fpad", false);
        amf_property_number(command, "capabilities", 15.0);
        amf_property_number(command, "audioCodecs", 0.0);
        amf_property_number(command, "videoCodecs", 252.0);
        amf_property_number(command, "videoFunction", 1.0);
        amf_property_number(command, "objectEncoding", 0.0);
        amf_object_end(command);
        return SendMessage(3, 20, 0, 0, command.data(), command.size(),
                           error);
    }

    bool SendSimpleCommand(const char *name, double transaction,
                           const std::string &argument, uint32_t stream_id,
                           std::string &error) {
        std::vector<uint8_t> command;
        amf_string(command, name);
        amf_number(command, transaction);
        amf_null(command);
        amf_string(command, argument);
        return SendMessage(3, 20, stream_id, 0, command.data(),
                           command.size(), error);
    }

    int ReadMessage(Message &message) {
        for (;;) {
            uint8_t basic = 0;
            int result = ReceiveExact(&basic, 1);
            if (result != 0)
                return result;
            uint8_t format = basic >> 6;
            uint32_t chunk_stream_id = basic & 0x3F;
            if (chunk_stream_id == 0) {
                uint8_t extra = 0;
                if (ReceiveExact(&extra, 1) != 0)
                    return -1;
                chunk_stream_id = 64U + extra;
            } else if (chunk_stream_id == 1) {
                uint8_t extra[2];
                if (ReceiveExact(extra, sizeof(extra)) != 0)
                    return -1;
                chunk_stream_id = 64U + extra[0] +
                                  static_cast<uint32_t>(extra[1]) * 256U;
            }
            ChunkState &state = chunks_[chunk_stream_id];
            uint32_t timestamp_field = 0;
            if (format == 0) {
                uint8_t header[11];
                if (ReceiveExact(header, sizeof(header)) != 0)
                    return -1;
                timestamp_field = read_be24(header);
                state.length = read_be24(header + 3);
                state.type = header[6];
                state.stream_id = read_le32(header + 7);
                state.timestamp_delta = 0;
                state.timestamp = timestamp_field;
                state.received = 0;
                state.payload.clear();
                state.valid = true;
            } else if (format == 1) {
                if (!state.valid)
                    return -1;
                uint8_t header[7];
                if (ReceiveExact(header, sizeof(header)) != 0)
                    return -1;
                timestamp_field = read_be24(header);
                state.timestamp_delta = timestamp_field;
                state.length = read_be24(header + 3);
                state.type = header[6];
                state.timestamp += timestamp_field;
                state.received = 0;
                state.payload.clear();
            } else if (format == 2) {
                if (!state.valid)
                    return -1;
                uint8_t header[3];
                if (ReceiveExact(header, sizeof(header)) != 0)
                    return -1;
                timestamp_field = read_be24(header);
                state.timestamp_delta = timestamp_field;
                state.timestamp += timestamp_field;
                state.received = 0;
                state.payload.clear();
            } else {
                if (!state.valid)
                    return -1;
                if (state.received == 0 && state.timestamp_delta != 0)
                    state.timestamp += state.timestamp_delta;
                timestamp_field = state.extended_timestamp ? 0xFFFFFFU : 0;
            }
            state.last_format = format;
            state.extended_timestamp = timestamp_field == 0xFFFFFFU;
            if (state.extended_timestamp) {
                uint8_t extended[4];
                if (ReceiveExact(extended, sizeof(extended)) != 0)
                    return -1;
                uint32_t value = read_be32(extended);
                if (format == 0)
                    state.timestamp = value;
                else if (format == 1 || format == 2) {
                    state.timestamp -= state.timestamp_delta;
                    state.timestamp_delta = value;
                    state.timestamp += value;
                }
            }
            if (state.length > 8U * 1024U * 1024U)
                return -1;
            if (state.payload.size() != state.length)
                state.payload.resize(state.length);
            size_t remaining = state.length - state.received;
            size_t part = std::min<size_t>(input_chunk_size_, remaining);
            if (part > 0 && ReceiveExact(state.payload.data() + state.received,
                                         part) != 0)
                return -1;
            state.received += part;
            if (state.received == state.length) {
                message.type = state.type;
                message.stream_id = state.stream_id;
                message.timestamp = state.timestamp;
                message.payload = state.payload;
                state.received = 0;
                state.payload.clear();
                return 0;
            }
        }
    }

    bool HandleControl(const Message &message, std::string &error) {
        if (message.type == 1 && message.payload.size() >= 4) {
            uint32_t chunk = read_be32(message.payload.data()) & 0x7FFFFFFFU;
            if (chunk != 0 && chunk <= 1024U * 1024U)
                input_chunk_size_ = chunk;
        } else if (message.type == 4 && message.payload.size() >= 6) {
            uint16_t event = (static_cast<uint16_t>(message.payload[0]) << 8) |
                             message.payload[1];
            if (event == 6) {
                std::array<uint8_t, 6> response{};
                response[1] = 7;
                std::copy_n(message.payload.data() + 2, 4,
                            response.data() + 2);
                if (!SendMessage(2, 4, 0, 0, response.data(),
                                 response.size(), error))
                    return false;
            }
        }
        if (bytes_received_ - last_acknowledged_ >= kAckWindow) {
            std::vector<uint8_t> ack;
            put_be32(ack, static_cast<uint32_t>(bytes_received_));
            if (!SendMessage(2, 3, 0, 0, ack.data(), ack.size(), error))
                return false;
            last_acknowledged_ = bytes_received_;
        }
        return true;
    }

    bool WaitForResult(double expected_transaction, double *value,
                       std::string &error) {
        for (int attempts = 0; attempts < 40; ++attempts) {
            Message message;
            int result = ReadMessage(message);
            if (result == 1)
                continue;
            if (result != 0) {
                error = "RTMP server closed during setup";
                return false;
            }
            if (!HandleControl(message, error))
                return false;
            if (message.type != 20 && message.type != 17)
                continue;
            size_t offset = message.type == 17 ? 1 : 0;
            std::string command;
            double transaction = 0;
            if (!amf_read_string(message.payload, offset, command) ||
                !amf_read_number(message.payload, offset, transaction))
                continue;
            if (command == "_error") {
                error = "RTMP server rejected the request";
                return false;
            }
            if (command != "_result" ||
                std::fabs(transaction - expected_transaction) > 0.01)
                continue;
            if (value != nullptr) {
                if (offset < message.payload.size() &&
                    (message.payload[offset] == 0x05 ||
                     message.payload[offset] == 0x06))
                    ++offset;
                if (!amf_read_number(message.payload, offset, *value)) {
                    error = "Invalid createStream response";
                    return false;
                }
            }
            return true;
        }
        error = "RTMP server response timed out";
        return false;
    }

    bool WaitForPublish(std::string &error) {
        for (int attempts = 0; attempts < 40; ++attempts) {
            Message message;
            int result = ReadMessage(message);
            if (result == 1)
                continue;
            if (result != 0) {
                error = "RTMP server closed before publishing";
                return false;
            }
            if (!HandleControl(message, error))
                return false;
            if (payload_contains(message.payload,
                                 "NetStream.Publish.Start"))
                return true;
            if (payload_contains(message.payload, "NetStream.Publish.Bad") ||
                payload_contains(message.payload, "NetStream.Failed") ||
                payload_contains(message.payload, "_error")) {
                error = "RTMP server rejected the stream key";
                return false;
            }
        }
        error = "RTMP publish confirmation timed out";
        return false;
    }

    void ReaderLoop() {
        std::string error;
        while (connected_.load()) {
            Message message;
            int result = ReadMessage(message);
            if (result == 1)
                continue;
            if (result != 0 || !HandleControl(message, error)) {
                connected_.store(false);
                ShutdownSocket();
                break;
            }
        }
    }

    void ShutdownSocket() {
        int fd = socket_;
        if (fd >= 0)
            shutdown(fd, SHUT_RDWR);
    }

    int socket_{-1};
    uint32_t stream_id_{0};
    uint32_t input_chunk_size_{128};
    uint64_t bytes_received_{0};
    uint64_t last_acknowledged_{0};
    std::atomic<bool> connected_{false};
    std::mutex send_mutex_;
    std::thread reader_;
    std::map<uint32_t, ChunkState> chunks_;
};

RtmpPublisher::RtmpPublisher() : client_(new Client) {}

RtmpPublisher::~RtmpPublisher() {
    Stop();
    delete client_;
    client_ = nullptr;
}

bool RtmpPublisher::Start(const std::string &url) {
    if (running_.exchange(true))
        return false;
    url_ = url;
    dropped_.store(0);
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.clear();
        codec_headers_.clear();
    }
    state_.store(RtmpPublisherState::Connecting);
    SetDetail("Connecting to RTMP server...");
    worker_ = std::thread(&RtmpPublisher::Worker, this);
    return true;
}

void RtmpPublisher::Stop() {
    if (!running_.exchange(false))
        return;
    queue_condition_.notify_all();
    if (client_ != nullptr)
        client_->Interrupt();
    if (worker_.joinable())
        worker_.join();
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.clear();
    }
    state_.store(RtmpPublisherState::Stopped);
    SetDetail("Stopped");
}

void RtmpPublisher::OnVEncData(k_u32, void *data, size_t size,
                               k_venc_pack_type type, uint64_t timestamp) {
    if (!running_.load() || data == nullptr || size == 0)
        return;
    EncodedPacket packet;
    packet.data.resize(size);
    std::memcpy(packet.data.data(), data, size);
    packet.timestamp = timestamp;
    packet.type = type;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (type == K_VENC_HEADER) {
            /*
             * K230 emits SPS and PPS as separate HEADER packs.  Keeping
             * only the most recent pack leaves a reconnect with PPS but no
             * SPS (or vice versa), so preserve the complete recent header
             * set.  The small cap also handles encoders that repeat headers
             * periodically without allowing unbounded growth.
             */
            const bool already_cached = std::any_of(
                codec_headers_.begin(), codec_headers_.end(),
                [&packet](const EncodedPacket &cached) {
                    return cached.data == packet.data;
                });
            if (!already_cached) {
                while (codec_headers_.size() >= 8)
                    codec_headers_.pop_front();
                codec_headers_.push_back(packet);
            }
        }
        while (queue_.size() >= kVideoQueueSize) {
            queue_.pop_front();
            dropped_.fetch_add(1);
        }
        queue_.push_back(std::move(packet));
    }
    queue_condition_.notify_one();
}

std::string RtmpPublisher::StatusDetail() const {
    std::lock_guard<std::mutex> lock(detail_mutex_);
    return detail_;
}

void RtmpPublisher::SetDetail(const std::string &detail) {
    std::lock_guard<std::mutex> lock(detail_mutex_);
    detail_ = detail;
}

uint32_t RtmpPublisher::NormalizeTimestamp(uint64_t timestamp) {
    if (!have_timestamp_) {
        have_timestamp_ = true;
        timestamp_base_ = timestamp;
        return 0;
    }
    uint64_t delta = timestamp >= timestamp_base_
                         ? timestamp - timestamp_base_
                         : 0;
    return static_cast<uint32_t>(std::min<uint64_t>(delta / 1000,
                                                    0xFFFFFFFFULL));
}

int RtmpPublisher::OnFlvPacket(void *opaque, int type, const void *data,
                               size_t bytes, uint32_t timestamp) {
    return static_cast<RtmpPublisher *>(opaque)->SendFlvPacket(
        type, data, bytes, timestamp);
}

int RtmpPublisher::SendFlvPacket(int type, const void *data, size_t bytes,
                                 uint32_t timestamp) {
    if (type != FLV_TYPE_VIDEO)
        return 0;
    std::string error;
    if (!client_->SendVideo(data, bytes, timestamp, error)) {
        SetDetail(error);
        return -1;
    }
    return 0;
}

void RtmpPublisher::Worker() {
    muxer_ = flv_muxer_create(OnFlvPacket, this);
    if (muxer_ == nullptr) {
        SetDetail("Cannot initialize FLV muxer");
        state_.store(RtmpPublisherState::Stopped);
        running_.store(false);
        return;
    }

    bool first_attempt = true;
    bool waiting_for_keyframe = true;
    bool initial_header_probe_requested = false;
    std::chrono::steady_clock::time_point initial_header_deadline;
    std::vector<uint8_t> pending_codec_header;
    while (running_.load()) {
        if (!client_->Connected()) {
            state_.store(first_attempt ? RtmpPublisherState::Connecting
                                       : RtmpPublisherState::Reconnecting);
            SetDetail(first_attempt ? "Connecting to RTMP server..."
                                    : "Connection lost; retrying...");
            /*
             * Publisher startup intentionally precedes camera startup, so
             * connecting immediately can race the encoder's two distinct
             * SPS/PPS HEADER packs.  In that race the first client receives
             * only one parameter-set variant and frames referencing the
             * other PPS cannot be decoded.  Cache the first header, request
             * another IDR, and give the encoder a short window to publish
             * the second distinct variant before opening RTMP.  Encoders
             * that expose only one variant still continue after the timeout.
             */
            if (first_attempt) {
                size_t header_count = 0;
                {
                    std::lock_guard<std::mutex> lock(queue_mutex_);
                    header_count = codec_headers_.size();
                }
                if (header_count == 0) {
                    SetDetail("Starting camera encoder...");
                    std::this_thread::sleep_for(50ms);
                    continue;
                }
                if (!initial_header_probe_requested) {
                    kd_mpi_venc_request_idr(0);
                    initial_header_probe_requested = true;
                    initial_header_deadline =
                        std::chrono::steady_clock::now() + 750ms;
                }
                if (header_count < 2 &&
                    std::chrono::steady_clock::now() <
                        initial_header_deadline) {
                    SetDetail("Preparing video stream...");
                    std::this_thread::sleep_for(50ms);
                    continue;
                }
            }
            std::string error;
            if (!client_->ConnectAndPublish(url_, error)) {
                SetDetail(error + "; retrying...");
                first_attempt = false;
                for (int wait = 0; running_.load() && wait < 20; ++wait)
                    std::this_thread::sleep_for(100ms);
                continue;
            }
            first_attempt = false;
            std::deque<EncodedPacket> codec_headers;
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                queue_.clear();
                codec_headers = codec_headers_;
            }
            have_timestamp_ = false;
            flv_muxer_reset(static_cast<flv_muxer_t *>(muxer_));
            pending_codec_header.clear();
            for (const EncodedPacket &codec_header : codec_headers) {
                pending_codec_header.insert(pending_codec_header.end(),
                                            codec_header.data.begin(),
                                            codec_header.data.end());
            }
            kd_mpi_venc_request_idr(0);
            waiting_for_keyframe = true;
            state_.store(RtmpPublisherState::Streaming);
            SetDetail("Live stream connected");
        }

        EncodedPacket packet;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_condition_.wait_for(lock, 250ms, [this]() {
                return !running_.load() || !queue_.empty();
            });
            if (!running_.load())
                break;
            if (queue_.empty()) {
                if (!client_->Connected())
                    continue;
                continue;
            }
            packet = std::move(queue_.front());
            queue_.pop_front();
        }
        if (packet.type == K_VENC_HEADER) {
            pending_codec_header.insert(pending_codec_header.end(),
                                        packet.data.begin(),
                                        packet.data.end());
            continue;
        }
        if (waiting_for_keyframe) {
            if (packet.type != K_VENC_I_FRAME)
                continue;
            waiting_for_keyframe = false;
        }
        /*
         * K230 supplies two SPS/PPS variants in consecutive HEADER packs.
         * Feed the complete burst to libflv at once immediately before the
         * key frame.  Sending the first partial configuration separately
         * makes players that keep only the first AVC sequence header fail
         * whenever a slice references the second PPS id.
         */
        if (!pending_codec_header.empty()) {
            flv_muxer_avc(static_cast<flv_muxer_t *>(muxer_),
                          pending_codec_header.data(),
                          pending_codec_header.size(), 0, 0);
            pending_codec_header.clear();
        }
        uint32_t timestamp = NormalizeTimestamp(packet.timestamp);
        int result = flv_muxer_avc(static_cast<flv_muxer_t *>(muxer_),
                                   packet.data.data(), packet.data.size(),
                                   timestamp, timestamp);
        if (result != 0) {
            client_->Close();
            state_.store(RtmpPublisherState::Reconnecting);
        }
    }
    client_->Close();
    flv_muxer_destroy(static_cast<flv_muxer_t *>(muxer_));
    muxer_ = nullptr;
}
