#include "rtmp_play_server.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <ctime>
#include <unistd.h>

#include "flv-muxer.h"
#include "flv-proto.h"
#include "mpi_venc_api.h"

using namespace std::chrono_literals;

namespace {
constexpr size_t kHandshakeSize = 1536;
constexpr uint32_t kOutputChunkSize = 4096;
constexpr uint32_t kAckWindow = 2500000;
constexpr size_t kInputQueueSize = 48;
constexpr size_t kClientQueueSize = 32;
constexpr size_t kMaxClients = 4;

uint32_t read_be24(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 16) |
           (static_cast<uint32_t>(p[1]) << 8) | p[2];
}

uint32_t read_be32(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

uint32_t read_le32(const uint8_t *p) {
    return p[0] | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

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

void amf_string(std::vector<uint8_t> &out, const std::string &value) {
    out.push_back(0x02);
    put_be16(out, static_cast<uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

void amf_number(std::vector<uint8_t> &out, double value) {
    out.push_back(0x00);
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    for (int shift = 56; shift >= 0; shift -= 8)
        out.push_back(static_cast<uint8_t>(bits >> shift));
}

void amf_boolean(std::vector<uint8_t> &out, bool value) {
    out.push_back(0x01);
    out.push_back(value ? 1 : 0);
}

void amf_null(std::vector<uint8_t> &out) { out.push_back(0x05); }

void amf_property_name(std::vector<uint8_t> &out, const char *name) {
    size_t size = std::strlen(name);
    put_be16(out, static_cast<uint16_t>(size));
    out.insert(out.end(), name, name + size);
}

void amf_property_string(std::vector<uint8_t> &out, const char *name,
                         const std::string &value) {
    amf_property_name(out, name);
    amf_string(out, value);
}

void amf_property_number(std::vector<uint8_t> &out, const char *name,
                         double value) {
    amf_property_name(out, name);
    amf_number(out, value);
}

void amf_property_boolean(std::vector<uint8_t> &out, const char *name,
                          bool value) {
    amf_property_name(out, name);
    amf_boolean(out, value);
}

void amf_object_end(std::vector<uint8_t> &out) {
    out.push_back(0);
    out.push_back(0);
    out.push_back(9);
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
    for (int index = 0; index < 8; ++index)
        bits = (bits << 8) | data[offset++];
    std::memcpy(&value, &bits, sizeof(value));
    return std::isfinite(value);
}
}  // namespace

class RtmpPlayServer::Client {
public:
    Client(RtmpPlayServer &owner, int socket)
        : owner_(owner), socket_(socket) {}

    ~Client() { Stop(); }

    void Start() {
        running_.store(true);
        reader_ = std::thread(&Client::ReaderLoop, this);
        writer_ = std::thread(&Client::WriterLoop, this);
    }

    void Stop() {
        if (!running_.exchange(false) && socket_ < 0)
            return;
        queue_condition_.notify_all();
        int fd = socket_;
        if (fd >= 0)
            shutdown(fd, SHUT_RDWR);
        if (reader_.joinable() &&
            reader_.get_id() != std::this_thread::get_id())
            reader_.join();
        if (writer_.joinable() &&
            writer_.get_id() != std::this_thread::get_id())
            writer_.join();
        std::lock_guard<std::mutex> lock(send_mutex_);
        if (socket_ >= 0) {
            close(socket_);
            socket_ = -1;
        }
    }

    bool Running() const { return running_.load(); }
    bool Playing() const { return playing_.load(); }

    void Enqueue(const FlvPacket &packet) {
        if (!running_.load() || !playing_.load())
            return;
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (packet.sequence_header) {
            queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
                                        [](const FlvPacket &queued) {
                                            return queued.sequence_header;
                                        }),
                         queue_.end());
        }
        while (queue_.size() >= kClientQueueSize) {
            auto disposable = std::find_if(
                queue_.begin(), queue_.end(), [](const FlvPacket &queued) {
                    return !queued.keyframe && !queued.sequence_header;
                });
            if (disposable != queue_.end())
                queue_.erase(disposable);
            else
                queue_.pop_front();
        }
        queue_.push_back(packet);
        queue_condition_.notify_one();
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
        uint32_t timestamp{0};
        uint32_t timestamp_delta{0};
        uint32_t length{0};
        uint8_t type{0};
        uint32_t stream_id{0};
        size_t received{0};
        bool extended_timestamp{false};
        std::vector<uint8_t> payload;
    };

    int ReceiveExact(void *buffer, size_t bytes) {
        auto *output = static_cast<uint8_t *>(buffer);
        size_t received = 0;
        while (received < bytes && running_.load()) {
            ssize_t result = recv(socket_, output + received,
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
        return received == bytes ? 0 : -1;
    }

    bool SendAll(const void *buffer, size_t bytes) {
        const auto *input = static_cast<const uint8_t *>(buffer);
        size_t sent = 0;
        while (sent < bytes && running_.load()) {
            ssize_t result = send(socket_, input + sent, bytes - sent,
                                  MSG_NOSIGNAL);
            if (result > 0) {
                sent += static_cast<size_t>(result);
                continue;
            }
            if (result < 0 && errno == EINTR)
                continue;
            return false;
        }
        return sent == bytes;
    }

    bool Handshake() {
        std::array<uint8_t, 1 + kHandshakeSize> c0c1{};
        if (ReceiveExact(c0c1.data(), c0c1.size()) != 0 || c0c1[0] != 3)
            return false;
        std::array<uint8_t, 1 + kHandshakeSize * 2> response{};
        response[0] = 3;
        uint32_t now = static_cast<uint32_t>(time(nullptr));
        response[1] = static_cast<uint8_t>(now >> 24);
        response[2] = static_cast<uint8_t>(now >> 16);
        response[3] = static_cast<uint8_t>(now >> 8);
        response[4] = static_cast<uint8_t>(now);
        std::copy(c0c1.begin() + 1, c0c1.end(),
                  response.begin() + 1 + kHandshakeSize);
        if (!SendAll(response.data(), response.size()))
            return false;
        std::array<uint8_t, kHandshakeSize> c2{};
        return ReceiveExact(c2.data(), c2.size()) == 0;
    }

    bool SendMessage(uint8_t chunk_stream_id, uint8_t type,
                     uint32_t message_stream_id, uint32_t timestamp,
                     const uint8_t *payload, size_t bytes) {
        if (socket_ < 0 || bytes > 0xFFFFFFU)
            return false;
        std::lock_guard<std::mutex> lock(send_mutex_);
        uint32_t short_timestamp = std::min(timestamp, 0xFFFFFFU);
        size_t offset = 0;
        bool first = true;
        do {
            std::vector<uint8_t> header;
            if (first) {
                header.push_back(chunk_stream_id);
                put_be24(header, short_timestamp);
                put_be24(header, static_cast<uint32_t>(bytes));
                header.push_back(type);
                put_le32(header, message_stream_id);
            } else {
                header.push_back(static_cast<uint8_t>(0xC0 |
                                                       chunk_stream_id));
            }
            if (timestamp >= 0xFFFFFFU)
                put_be32(header, timestamp);
            if (!SendAll(header.data(), header.size()))
                return false;
            size_t part = std::min<size_t>(kOutputChunkSize, bytes - offset);
            if (part > 0 && !SendAll(payload + offset, part))
                return false;
            offset += part;
            first = false;
        } while (offset < bytes);
        return true;
    }

    int ReadMessage(Message &message) {
        for (;;) {
            uint8_t basic = 0;
            int result = ReceiveExact(&basic, 1);
            if (result != 0)
                return result;
            uint8_t format = basic >> 6;
            uint32_t chunk_id = basic & 0x3F;
            if (chunk_id == 0) {
                uint8_t extra = 0;
                if (ReceiveExact(&extra, 1) != 0)
                    return -1;
                chunk_id = 64U + extra;
            } else if (chunk_id == 1) {
                uint8_t extra[2];
                if (ReceiveExact(extra, sizeof(extra)) != 0)
                    return -1;
                chunk_id = 64U + extra[0] +
                           static_cast<uint32_t>(extra[1]) * 256U;
            }
            ChunkState &state = chunks_[chunk_id];
            uint32_t timestamp_field = 0;
            if (format == 0) {
                uint8_t header[11];
                if (ReceiveExact(header, sizeof(header)) != 0)
                    return -1;
                timestamp_field = read_be24(header);
                state.length = read_be24(header + 3);
                state.type = header[6];
                state.stream_id = read_le32(header + 7);
                state.timestamp = timestamp_field;
                state.timestamp_delta = 0;
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
                state.timestamp += timestamp_field;
                state.length = read_be24(header + 3);
                state.type = header[6];
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

    bool SendResult(double transaction, bool stream_result) {
        std::vector<uint8_t> response;
        amf_string(response, "_result");
        amf_number(response, transaction);
        amf_null(response);
        if (stream_result)
            amf_number(response, 1.0);
        return SendMessage(3, 20, 0, 0, response.data(), response.size());
    }

    bool SendConnectResult(double transaction) {
        std::vector<uint8_t> response;
        amf_string(response, "_result");
        amf_number(response, transaction);
        response.push_back(0x03);
        amf_property_string(response, "fmsVer", "FMS/3,5,7,7009");
        amf_property_number(response, "capabilities", 31.0);
        amf_object_end(response);
        response.push_back(0x03);
        amf_property_string(response, "level", "status");
        amf_property_string(response, "code",
                            "NetConnection.Connect.Success");
        amf_property_string(response, "description",
                            "Connection succeeded.");
        amf_property_number(response, "objectEncoding", 0.0);
        amf_object_end(response);
        return SendMessage(3, 20, 0, 0, response.data(), response.size());
    }

    bool SendOnStatus(const char *code, const char *description) {
        std::vector<uint8_t> response;
        amf_string(response, "onStatus");
        amf_number(response, 0.0);
        amf_null(response);
        response.push_back(0x03);
        amf_property_string(response, "level", "status");
        amf_property_string(response, "code", code);
        amf_property_string(response, "description", description);
        amf_property_boolean(response, "details", true);
        amf_object_end(response);
        return SendMessage(5, 20, 1, 0, response.data(), response.size());
    }

    bool SendSessionSetup() {
        std::vector<uint8_t> value;
        put_be32(value, kOutputChunkSize);
        if (!SendMessage(2, 1, 0, 0, value.data(), value.size()))
            return false;
        value.clear();
        put_be32(value, kAckWindow);
        if (!SendMessage(2, 5, 0, 0, value.data(), value.size()))
            return false;
        value.clear();
        put_be32(value, kAckWindow);
        value.push_back(2);
        return SendMessage(2, 6, 0, 0, value.data(), value.size());
    }

    bool BeginPlayback() {
        std::array<uint8_t, 6> begin{};
        begin[0] = 0;
        begin[1] = 0;
        begin[5] = 1;
        if (!SendMessage(2, 4, 0, 0, begin.data(), begin.size()) ||
            !SendOnStatus("NetStream.Play.Reset", "Playing and resetting") ||
            !SendOnStatus("NetStream.Play.Start", "Started playing"))
            return false;
        playing_.store(true);
        owner_.PrimeClient(this);
        kd_mpi_venc_request_idr(0);
        return true;
    }

    bool HandleMessage(const Message &message) {
        if (message.type == 1 && message.payload.size() >= 4) {
            uint32_t value = read_be32(message.payload.data()) & 0x7FFFFFFFU;
            if (value > 0 && value <= 1024U * 1024U)
                input_chunk_size_ = value;
            return true;
        }
        if (message.type == 4 && message.payload.size() >= 6) {
            uint16_t event = (static_cast<uint16_t>(message.payload[0]) << 8) |
                             message.payload[1];
            if (event == 6) {
                std::array<uint8_t, 6> pong{};
                pong[1] = 7;
                std::copy_n(message.payload.data() + 2, 4,
                            pong.data() + 2);
                return SendMessage(2, 4, 0, 0, pong.data(), pong.size());
            }
            return true;
        }
        if (message.type != 20 && message.type != 17)
            return true;
        size_t offset = message.type == 17 ? 1 : 0;
        std::string command;
        double transaction = 0;
        if (!amf_read_string(message.payload, offset, command) ||
            !amf_read_number(message.payload, offset, transaction))
            return true;
        if (command == "connect")
            return SendSessionSetup() && SendConnectResult(transaction);
        if (command == "createStream")
            return SendResult(transaction, true);
        if (command == "getStreamLength" || command == "_checkbw")
            return SendResult(transaction, false);
        if (command == "play")
            return BeginPlayback();
        if (command == "closeStream" || command == "deleteStream")
            return false;
        return true;
    }

    void ReaderLoop() {
        timeval timeout{1, 0};
        setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                   sizeof(timeout));
        setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                   sizeof(timeout));
        if (!Handshake()) {
            running_.store(false);
            queue_condition_.notify_all();
            return;
        }
        while (running_.load()) {
            Message message;
            int result = ReadMessage(message);
            if (result == 1)
                continue;
            if (result != 0 || !HandleMessage(message))
                break;
            if (bytes_received_ - last_acknowledged_ >= kAckWindow) {
                std::vector<uint8_t> ack;
                put_be32(ack, static_cast<uint32_t>(bytes_received_));
                if (!SendMessage(2, 3, 0, 0, ack.data(), ack.size()))
                    break;
                last_acknowledged_ = bytes_received_;
            }
        }
        running_.store(false);
        queue_condition_.notify_all();
        shutdown(socket_, SHUT_RDWR);
    }

    void WriterLoop() {
        bool timestamp_ready = false;
        uint32_t timestamp_base = 0;
        while (running_.load()) {
            FlvPacket packet;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_condition_.wait_for(lock, 500ms, [this]() {
                    return !running_.load() || !queue_.empty();
                });
                if (!running_.load())
                    break;
                if (queue_.empty())
                    continue;
                packet = std::move(queue_.front());
                queue_.pop_front();
            }
            uint32_t timestamp = 0;
            if (!packet.sequence_header) {
                if (!timestamp_ready) {
                    timestamp_ready = true;
                    timestamp_base = packet.timestamp;
                }
                timestamp = packet.timestamp >= timestamp_base
                                ? packet.timestamp - timestamp_base : 0;
            }
            if (!SendMessage(6, 9, 1, timestamp, packet.data.data(),
                             packet.data.size())) {
                running_.store(false);
                shutdown(socket_, SHUT_RDWR);
                break;
            }
        }
    }

    RtmpPlayServer &owner_;
    int socket_{-1};
    std::atomic<bool> running_{false};
    std::atomic<bool> playing_{false};
    std::thread reader_;
    std::thread writer_;
    std::mutex send_mutex_;
    std::mutex queue_mutex_;
    std::condition_variable queue_condition_;
    std::deque<FlvPacket> queue_;
    std::map<uint32_t, ChunkState> chunks_;
    uint32_t input_chunk_size_{128};
    uint64_t bytes_received_{0};
    uint64_t last_acknowledged_{0};
};

RtmpPlayServer::RtmpPlayServer() = default;
RtmpPlayServer::~RtmpPlayServer() { Stop(); }

bool RtmpPlayServer::Start(uint16_t port) {
    if (running_.exchange(true))
        return true;
    port_ = port;
    dropped_.store(0);
    listener_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_ < 0) {
        running_.store(false);
        SetDetail("Cannot create RTMP socket");
        return false;
    }
    int reuse = 1;
    setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port_);
    if (bind(listener_, reinterpret_cast<sockaddr *>(&address),
             sizeof(address)) != 0 || listen(listener_, 4) != 0) {
        char detail[96];
        std::snprintf(detail, sizeof(detail), "RTMP port %u unavailable: %s",
                      port_, std::strerror(errno));
        SetDetail(detail);
        close(listener_);
        listener_ = -1;
        running_.store(false);
        return false;
    }
    /* RT-Smart/lwIP's accept path dereferences the listening socket again
     * after a blocked accept returns. Closing the descriptor from Stop()
     * while that call is sleeping can therefore race into a kernel NULL
     * dereference. Poll a non-blocking listener and close it only after the
     * accept thread has observed running_=false and joined. */
    int flags = fcntl(listener_, F_GETFL, 0);
    if (flags < 0 || fcntl(listener_, F_SETFL, flags | O_NONBLOCK) < 0) {
        SetDetail("Cannot configure RTMP listener");
        close(listener_);
        listener_ = -1;
        running_.store(false);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(input_mutex_);
        input_queue_.clear();
        codec_headers_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        sequence_header_ = FlvPacket{};
        keyframe_ = FlvPacket{};
    }
    have_timestamp_ = false;
    SetDetail("RTMP playback service ready");
    mux_thread_ = std::thread(&RtmpPlayServer::MuxLoop, this);
    accept_thread_ = std::thread(&RtmpPlayServer::AcceptLoop, this);
    std::printf("[network-camera] local RTMP listening on port %u\n", port_);
    return true;
}

void RtmpPlayServer::Stop() {
    if (!running_.exchange(false) && listener_ < 0)
        return;
    input_condition_.notify_all();
    if (accept_thread_.joinable())
        accept_thread_.join();
    if (listener_ >= 0) {
        close(listener_);
        listener_ = -1;
    }
    if (mux_thread_.joinable())
        mux_thread_.join();
    std::vector<std::shared_ptr<Client>> clients;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients.swap(clients_);
    }
    for (const auto &client : clients)
        client->Stop();
    SetDetail("Stopped");
    std::printf("[network-camera] local RTMP stopped\n");
}

void RtmpPlayServer::OnEncoded(void *data, size_t size,
                               k_venc_pack_type type, uint64_t timestamp) {
    if (!running_.load() || data == nullptr || size == 0)
        return;
    EncodedPacket packet;
    packet.data.resize(size);
    std::memcpy(packet.data.data(), data, size);
    packet.timestamp = timestamp;
    packet.type = type;
    {
        std::lock_guard<std::mutex> lock(input_mutex_);
        if (type == K_VENC_HEADER) {
            bool exists = std::any_of(
                codec_headers_.begin(), codec_headers_.end(),
                [&packet](const EncodedPacket &cached) {
                    return cached.data == packet.data;
                });
            if (!exists) {
                while (codec_headers_.size() >= 8)
                    codec_headers_.pop_front();
                codec_headers_.push_back(packet);
            }
        }
        while (input_queue_.size() >= kInputQueueSize) {
            auto disposable = std::find_if(
                input_queue_.begin(), input_queue_.end(),
                [](const EncodedPacket &queued) {
                    return queued.type == K_VENC_P_FRAME;
                });
            if (disposable != input_queue_.end())
                input_queue_.erase(disposable);
            else
                input_queue_.pop_front();
            dropped_.fetch_add(1);
        }
        input_queue_.push_back(std::move(packet));
    }
    input_condition_.notify_one();
}

size_t RtmpPlayServer::ClientCount() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return static_cast<size_t>(std::count_if(
        clients_.begin(), clients_.end(),
        [](const std::shared_ptr<Client> &client) {
            return client->Running() && client->Playing();
        }));
}

std::string RtmpPlayServer::StatusDetail() const {
    std::lock_guard<std::mutex> lock(detail_mutex_);
    return detail_;
}

void RtmpPlayServer::SetDetail(const std::string &detail) {
    std::lock_guard<std::mutex> lock(detail_mutex_);
    detail_ = detail;
}

void RtmpPlayServer::AcceptLoop() {
    while (running_.load()) {
        sockaddr_in address{};
        socklen_t length = sizeof(address);
        int client_fd = accept(listener_, reinterpret_cast<sockaddr *>(&address),
                               &length);
        if (client_fd < 0) {
            if (!running_.load())
                break;
            if (errno == EINTR)
                continue;
            std::this_thread::sleep_for(50ms);
            continue;
        }
        std::vector<std::shared_ptr<Client>> retired;
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            auto item = clients_.begin();
            while (item != clients_.end()) {
                if (!(*item)->Running()) {
                    retired.push_back(*item);
                    item = clients_.erase(item);
                } else {
                    ++item;
                }
            }
            if (clients_.size() >= kMaxClients) {
                close(client_fd);
                client_fd = -1;
            } else {
                auto client = std::make_shared<Client>(*this, client_fd);
                clients_.push_back(client);
                client->Start();
            }
        }
        for (const auto &client : retired)
            client->Stop();
    }
}

void RtmpPlayServer::PrimeClient(Client *client) {
    FlvPacket sequence;
    FlvPacket key;
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        sequence = sequence_header_;
        key = keyframe_;
    }
    if (!sequence.data.empty())
        client->Enqueue(sequence);
    if (!key.data.empty())
        client->Enqueue(key);
}

uint32_t RtmpPlayServer::NormalizeTimestamp(uint64_t timestamp) {
    if (!have_timestamp_) {
        have_timestamp_ = true;
        timestamp_base_ = timestamp;
        return 0;
    }
    uint64_t delta = timestamp >= timestamp_base_ ? timestamp - timestamp_base_
                                                  : 0;
    return static_cast<uint32_t>(std::min<uint64_t>(delta / 1000,
                                                    0xFFFFFFFFULL));
}

int RtmpPlayServer::OnFlvPacket(void *opaque, int type, const void *data,
                                size_t bytes, uint32_t timestamp) {
    return static_cast<RtmpPlayServer *>(opaque)->HandleFlvPacket(
        type, data, bytes, timestamp);
}

int RtmpPlayServer::HandleFlvPacket(int type, const void *data, size_t bytes,
                                    uint32_t timestamp) {
    if (type != FLV_TYPE_VIDEO || data == nullptr || bytes < 2)
        return 0;
    FlvPacket packet;
    const auto *source = static_cast<const uint8_t *>(data);
    packet.data.assign(source, source + bytes);
    packet.timestamp = timestamp;
    packet.sequence_header = source[1] == 0;
    packet.keyframe = (source[0] >> 4) == 1 && source[1] == 1;
    if (packet.sequence_header || packet.keyframe) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        if (packet.sequence_header)
            sequence_header_ = packet;
        if (packet.keyframe)
            keyframe_ = packet;
    }
    std::vector<std::shared_ptr<Client>> clients;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients = clients_;
    }
    for (const auto &client : clients)
        client->Enqueue(packet);
    return 0;
}

void RtmpPlayServer::MuxLoop() {
    muxer_ = flv_muxer_create(OnFlvPacket, this);
    if (muxer_ == nullptr) {
        SetDetail("Cannot initialize RTMP video muxer");
        return;
    }
    std::vector<uint8_t> headers;
    bool waiting_for_keyframe = true;
    while (running_.load()) {
        EncodedPacket packet;
        {
            std::unique_lock<std::mutex> lock(input_mutex_);
            input_condition_.wait_for(lock, 500ms, [this]() {
                return !running_.load() || !input_queue_.empty();
            });
            if (!running_.load())
                break;
            if (input_queue_.empty())
                continue;
            packet = std::move(input_queue_.front());
            input_queue_.pop_front();
        }
        if (packet.type == K_VENC_HEADER) {
            headers.insert(headers.end(), packet.data.begin(),
                           packet.data.end());
            continue;
        }
        if (waiting_for_keyframe) {
            if (packet.type != K_VENC_I_FRAME)
                continue;
            waiting_for_keyframe = false;
        }
        if (!headers.empty()) {
            flv_muxer_avc(static_cast<flv_muxer_t *>(muxer_), headers.data(),
                          headers.size(), 0, 0);
            headers.clear();
        }
        uint32_t timestamp = NormalizeTimestamp(packet.timestamp);
        flv_muxer_avc(static_cast<flv_muxer_t *>(muxer_), packet.data.data(),
                      packet.data.size(), timestamp, timestamp);
    }
    flv_muxer_destroy(static_cast<flv_muxer_t *>(muxer_));
    muxer_ = nullptr;
}
