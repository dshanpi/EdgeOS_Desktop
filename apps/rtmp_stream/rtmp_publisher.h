#ifndef DSHANPI_RTMP_PUBLISHER_H
#define DSHANPI_RTMP_PUBLISHER_H

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "media.h"

enum class RtmpPublisherState {
    Stopped = 0,
    Connecting,
    Streaming,
    Reconnecting,
};

/*
 * Small RTMP publisher for the K230 camera applications.  Encoded H.264 is
 * copied into a bounded queue in the VENC callback and packetized/sent by a
 * dedicated worker, so a slow network can never block the encoder thread.
 */
class RtmpPublisher : public IOnVEncData {
public:
    RtmpPublisher();
    ~RtmpPublisher() override;

    bool Start(const std::string &url);
    void Stop();

    void OnVEncData(k_u32 channel, void *data, size_t size,
                    k_venc_pack_type type, uint64_t timestamp) override;

    RtmpPublisherState State() const { return state_.load(); }
    std::string StatusDetail() const;
    uint64_t DroppedFrames() const { return dropped_.load(); }

private:
    struct EncodedPacket {
        std::vector<uint8_t> data;
        uint64_t timestamp{0};
        k_venc_pack_type type{K_VENC_P_FRAME};
    };

    class Client;

    static int OnFlvPacket(void *opaque, int type, const void *data,
                           size_t bytes, uint32_t timestamp);
    int SendFlvPacket(int type, const void *data, size_t bytes,
                      uint32_t timestamp);
    void Worker();
    void SetDetail(const std::string &detail);
    uint32_t NormalizeTimestamp(uint64_t timestamp);

    std::atomic<bool> running_{false};
    std::atomic<RtmpPublisherState> state_{RtmpPublisherState::Stopped};
    std::atomic<uint64_t> dropped_{0};
    std::string url_;
    mutable std::mutex detail_mutex_;
    std::string detail_;
    std::mutex queue_mutex_;
    std::condition_variable queue_condition_;
    std::deque<EncodedPacket> queue_;
    std::deque<EncodedPacket> codec_headers_;
    std::thread worker_;
    Client *client_{nullptr};
    void *muxer_{nullptr};
    bool have_timestamp_{false};
    uint64_t timestamp_base_{0};
};

#endif
