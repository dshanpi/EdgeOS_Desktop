#ifndef DSHANPI_RTMP_PLAY_SERVER_H
#define DSHANPI_RTMP_PLAY_SERVER_H

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "media.h"

/*
 * Small RTMP playback server.  It accepts ordinary RTMP play clients on the
 * board, converts the shared H.264 encoder output to FLV video messages and
 * gives every client an independent bounded queue.  Network backpressure can
 * therefore never stall the VENC callback or another client.
 */
class RtmpPlayServer {
public:
    RtmpPlayServer();
    ~RtmpPlayServer();

    bool Start(uint16_t port = 1935);
    void Stop();
    void OnEncoded(void *data, size_t size, k_venc_pack_type type,
                   uint64_t timestamp);

    bool Running() const { return running_.load(); }
    size_t ClientCount() const;
    uint64_t DroppedFrames() const { return dropped_.load(); }
    std::string StatusDetail() const;

private:
    struct EncodedPacket {
        std::vector<uint8_t> data;
        uint64_t timestamp{0};
        k_venc_pack_type type{K_VENC_P_FRAME};
    };
    struct FlvPacket {
        std::vector<uint8_t> data;
        uint32_t timestamp{0};
        bool sequence_header{false};
        bool keyframe{false};
    };
    class Client;

    static int OnFlvPacket(void *opaque, int type, const void *data,
                           size_t bytes, uint32_t timestamp);
    int HandleFlvPacket(int type, const void *data, size_t bytes,
                        uint32_t timestamp);
    void AcceptLoop();
    void MuxLoop();
    void PrimeClient(Client *client);
    void SetDetail(const std::string &detail);
    uint32_t NormalizeTimestamp(uint64_t timestamp);

    std::atomic<bool> running_{false};
    std::atomic<uint64_t> dropped_{0};
    int listener_{-1};
    uint16_t port_{1935};
    std::thread accept_thread_;
    std::thread mux_thread_;

    mutable std::mutex clients_mutex_;
    std::vector<std::shared_ptr<Client>> clients_;
    mutable std::mutex detail_mutex_;
    std::string detail_;

    std::mutex input_mutex_;
    std::condition_variable input_condition_;
    std::deque<EncodedPacket> input_queue_;
    std::deque<EncodedPacket> codec_headers_;
    void *muxer_{nullptr};
    bool have_timestamp_{false};
    uint64_t timestamp_base_{0};

    std::mutex cache_mutex_;
    FlvPacket sequence_header_;
    FlvPacket keyframe_;
};

#endif
