#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unistd.h>

#include "dual_stream.h"
#include "hal_netmgmt.h"
#include "lv_k230_display.h"
#include "lv_k230_input_touch.h"
#include "lvgl.h"
#include "media.h"
#include "mpi_sensor_api.h"
#include "mpi_vb_api.h"
#include "rtmp_play_server.h"
#include "rtmp_publisher.h"
#include "rtsp_server.h"
#include "ui_font_network_20.h"

using namespace std::chrono_literals;

namespace {
constexpr const char *kConfigPath = "/data/dshanpi_network_camera.conf";
constexpr const char *kOldRtspModePath = "/data/dshanpi_rtsp_mode.conf";
constexpr const char *kOldRtmpModePath = "/data/dshanpi_rtmp_mode.conf";
constexpr const char *kOldRtmpUrlPath = "/data/dshanpi_rtmp_url.conf";
constexpr const char *kDefaultPushUrl =
    "rtmp://192.168.1.100/live";

constexpr uint8_t kServiceRtsp = 1U << 0;
constexpr uint8_t kServiceRtmp = 1U << 1;
constexpr uint8_t kServicePush = 1U << 2;

enum class StreamMode { Rear = 0, Front, Dual, Count };
enum class VideoResolution { P720 = 0, P1080, Count };
enum class Page { Overview = 0, Services, Settings, Count };
enum class Language { ZhCn = 0, ZhTw, English, Japanese };

struct AppConfig {
    int version{4};
    StreamMode source{StreamMode::Rear};
    VideoResolution resolution{VideoResolution::P720};
    bool rtsp_enabled{true};
    bool rtmp_enabled{true};
    bool push_enabled{false};
    std::string push_url{kDefaultPushUrl};
};

std::atomic<bool> g_exit{false};
std::atomic<StreamMode> g_requested_mode{StreamMode::Rear};
std::atomic<VideoResolution> g_requested_resolution{VideoResolution::P720};
std::atomic<uint8_t> g_requested_services{kServiceRtsp | kServiceRtmp};
std::atomic<bool> g_services_changed{false};
std::atomic<bool> g_push_url_changed{false};
std::mutex g_config_mutex;
AppConfig g_config;
Language g_language{Language::English};
Page g_page{Page::Overview};

lv_obj_t *g_pages[static_cast<int>(Page::Count)]{};
lv_obj_t *g_tabs[static_cast<int>(Page::Count)]{};
lv_obj_t *g_mode_buttons[static_cast<int>(StreamMode::Count)]{};
lv_obj_t *g_resolution_buttons[static_cast<int>(VideoResolution::Count)]{};
lv_obj_t *g_service_toggles[3]{};
lv_obj_t *g_status_label{nullptr};
lv_obj_t *g_service_summary{nullptr};
lv_obj_t *g_client_label{nullptr};
lv_obj_t *g_push_status{nullptr};
lv_obj_t *g_url_label{nullptr};
lv_obj_t *g_url_dialog{nullptr};
lv_obj_t *g_url_input{nullptr};
lv_obj_t *g_url_keyboard{nullptr};
lv_obj_t *g_url_hint{nullptr};
lv_obj_t *g_url_preview{nullptr};
bool g_url_quick_mode{true};
std::string g_url_path{"/live"};

const char *g_url_kb_number[] = {
    "1", "2", "3", "DEL", "\n",
    "4", "5", "6", "CLEAR", "\n",
    "7", "8", "9", ".", "\n",
    "URL", "0", ":", "SAVE", ""};
const char *g_url_kb_lower[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "DEL", "\n",
    "z", "x", "c", "v", "b", "n", "m", "/", ".", ":", "\n",
    "IP", "ABC", "-", "_", "CLEAR", "SAVE", ""};
const char *g_url_kb_upper[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "DEL", "\n",
    "Z", "X", "C", "V", "B", "N", "M", "/", ".", ":", "\n",
    "IP", "abc", "-", "_", "CLEAR", "SAVE", ""};

#define URL_KB_CTRL(width)                                                    \
    static_cast<lv_buttonmatrix_ctrl_t>(                                      \
        (width) | LV_BUTTONMATRIX_CTRL_NO_REPEAT |                            \
        LV_BUTTONMATRIX_CTRL_CLICK_TRIG)
#define URL_KB_ACTION(width)                                                  \
    static_cast<lv_buttonmatrix_ctrl_t>(URL_KB_CTRL(width) |                  \
                                         LV_BUTTONMATRIX_CTRL_CHECKED)
const lv_buttonmatrix_ctrl_t g_url_kb_number_ctrl[16] = {
    URL_KB_CTRL(2), URL_KB_CTRL(2), URL_KB_CTRL(2), URL_KB_ACTION(4),
    URL_KB_CTRL(2), URL_KB_CTRL(2), URL_KB_CTRL(2), URL_KB_CTRL(4),
    URL_KB_CTRL(2), URL_KB_CTRL(2), URL_KB_CTRL(2), URL_KB_CTRL(4),
    URL_KB_CTRL(3), URL_KB_CTRL(2), URL_KB_CTRL(2), URL_KB_ACTION(3)};
const lv_buttonmatrix_ctrl_t g_url_kb_text_ctrl[36] = {
    URL_KB_CTRL(1), URL_KB_CTRL(1), URL_KB_CTRL(1), URL_KB_CTRL(1),
    URL_KB_CTRL(1), URL_KB_CTRL(1), URL_KB_CTRL(1), URL_KB_CTRL(1),
    URL_KB_CTRL(1), URL_KB_CTRL(1), URL_KB_CTRL(1), URL_KB_CTRL(1),
    URL_KB_CTRL(1), URL_KB_CTRL(1), URL_KB_CTRL(1), URL_KB_CTRL(1),
    URL_KB_CTRL(1), URL_KB_CTRL(1), URL_KB_CTRL(1), URL_KB_ACTION(2),
    URL_KB_CTRL(1), URL_KB_CTRL(1), URL_KB_CTRL(1), URL_KB_CTRL(1),
    URL_KB_CTRL(1), URL_KB_CTRL(1), URL_KB_CTRL(1), URL_KB_CTRL(1),
    URL_KB_CTRL(1), URL_KB_CTRL(1), URL_KB_CTRL(2), URL_KB_CTRL(2),
    URL_KB_CTRL(1), URL_KB_CTRL(1), URL_KB_CTRL(3), URL_KB_ACTION(3)};
#undef URL_KB_ACTION
#undef URL_KB_CTRL

const char *tr(const char *english, const char *zh_cn, const char *zh_tw,
               const char *japanese) {
    switch (g_language) {
    case Language::ZhCn: return zh_cn;
    case Language::ZhTw: return zh_tw;
    case Language::Japanese: return japanese;
    default: return english;
    }
}

const char *mode_name(StreamMode mode) {
    switch (mode) {
    case StreamMode::Rear:
        return tr("Rear CSI0", "后摄 CSI0", "後攝 CSI0", "リア CSI0");
    case StreamMode::Front:
        return tr("Front CSI2", "前摄 CSI2", "前攝 CSI2", "フロント CSI2");
    case StreamMode::Dual:
        return tr("Dual side-by-side", "双摄左右拼接", "雙攝左右拼接",
                  "デュアル左右");
    default: return "Rear CSI0";
    }
}

unsigned resolution_width(VideoResolution resolution) {
    return resolution == VideoResolution::P1080 ? 1920U : 1280U;
}

unsigned resolution_height(VideoResolution resolution) {
    return resolution == VideoResolution::P1080 ? 1080U : 720U;
}

unsigned resolution_bitrate(VideoResolution resolution) {
    return resolution == VideoResolution::P1080 ? 6000U : 4000U;
}

const char *resolution_name(VideoResolution resolution) {
    return resolution == VideoResolution::P1080 ? "1080p" : "720p";
}

void signal_handler(int) { g_exit.store(true); }

std::string load_text(const char *path, const char *fallback) {
    char backup[160];
    std::snprintf(backup, sizeof(backup), "%s.bak", path);
    FILE *file = std::fopen(path, "r");
    if (file == nullptr)
        file = std::fopen(backup, "r");
    if (file == nullptr)
        return fallback;
    char value[384]{};
    if (std::fgets(value, sizeof(value), file) == nullptr)
        value[0] = '\0';
    std::fclose(file);
    value[std::strcspn(value, "\r\n")] = '\0';
    return value[0] == '\0' ? fallback : value;
}

bool save_config_atomic(const AppConfig &config) {
    char temporary[160];
    char backup[160];
    std::snprintf(temporary, sizeof(temporary), "%s.tmp", kConfigPath);
    std::snprintf(backup, sizeof(backup), "%s.bak", kConfigPath);
    unlink(temporary);
    FILE *file = std::fopen(temporary, "w");
    if (file == nullptr)
        return false;
    bool failed =
        std::fprintf(file,
                     "version=%d\nsource=%d\nresolution=%d\nrtsp_enabled=%d\n"
                     "rtmp_server_enabled=%d\nrtmp_push_enabled=%d\n"
                     "rtmp_push_url=%s\n",
                     config.version, static_cast<int>(config.source),
                     static_cast<int>(config.resolution),
                     config.rtsp_enabled ? 1 : 0,
                     config.rtmp_enabled ? 1 : 0,
                     config.push_enabled ? 1 : 0,
                     config.push_url.c_str()) < 0 ||
        std::fflush(file) != 0 || fsync(fileno(file)) != 0;
    if (std::fclose(file) != 0)
        failed = true;
    if (failed) {
        unlink(temporary);
        return false;
    }
    unlink(backup);
    bool had_previous = std::rename(kConfigPath, backup) == 0;
    if (!had_previous && errno != ENOENT) {
        unlink(temporary);
        return false;
    }
    if (std::rename(temporary, kConfigPath) != 0) {
        if (had_previous)
            std::rename(backup, kConfigPath);
        unlink(temporary);
        return false;
    }
    unlink(backup);
    return true;
}

bool read_legacy_mode(const char *path, StreamMode &mode) {
    FILE *file = std::fopen(path, "r");
    if (file == nullptr)
        return false;
    int value = 0;
    bool valid = std::fscanf(file, "%d", &value) == 1 && value >= 0 &&
                 value < static_cast<int>(StreamMode::Count);
    std::fclose(file);
    if (valid)
        mode = static_cast<StreamMode>(value);
    return valid;
}

bool simplify_legacy_push_url(std::string &url) {
    const std::string legacy_path = "/live/canmv";
    const size_t position = url.find(legacy_path);
    if (position == std::string::npos)
        return false;
    /* Early keyboard layouts made it easy to append punctuation after the
     * old stream key (for example /live/canmv:).  Replace the entire legacy
     * tail so both the stream key and accidental punctuation are removed. */
    url.replace(position, std::string::npos, "/live");
    return true;
}

AppConfig load_config() {
    AppConfig config;
    FILE *file = std::fopen(kConfigPath, "r");
    if (file == nullptr) {
        char backup[160];
        std::snprintf(backup, sizeof(backup), "%s.bak", kConfigPath);
        file = std::fopen(backup, "r");
    }
    if (file == nullptr) {
        if (!read_legacy_mode(kOldRtspModePath, config.source))
            read_legacy_mode(kOldRtmpModePath, config.source);
        config.push_url = load_text(kOldRtmpUrlPath, kDefaultPushUrl);
        simplify_legacy_push_url(config.push_url);
        save_config_atomic(config);
        std::printf("[network-camera] migrated legacy stream settings\n");
        return config;
    }
    char line[512];
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        int value = 0;
        if (std::sscanf(line, "version=%d", &value) == 1) {
            config.version = value;
        } else if (std::sscanf(line, "source=%d", &value) == 1 &&
                   value >= 0 && value < static_cast<int>(StreamMode::Count)) {
            config.source = static_cast<StreamMode>(value);
        } else if (std::sscanf(line, "resolution=%d", &value) == 1 &&
                   value >= 0 &&
                   value < static_cast<int>(VideoResolution::Count)) {
            config.resolution = static_cast<VideoResolution>(value);
        } else if (std::sscanf(line, "rtsp_enabled=%d", &value) == 1) {
            config.rtsp_enabled = value != 0;
        } else if (std::sscanf(line, "rtmp_server_enabled=%d", &value) == 1) {
            config.rtmp_enabled = value != 0;
        } else if (std::sscanf(line, "rtmp_push_enabled=%d", &value) == 1) {
            config.push_enabled = value != 0;
        } else if (std::strncmp(line, "rtmp_push_url=", 14) == 0) {
            char *value_start = line + 14;
            value_start[std::strcspn(value_start, "\r\n")] = '\0';
            if (std::strncmp(value_start, "rtmp://", 7) == 0)
                config.push_url = value_start;
        }
    }
    std::fclose(file);
    bool needs_save = simplify_legacy_push_url(config.push_url);
    if (config.version < 4) {
        config.version = 4;
        needs_save = true;
        std::printf("[network-camera] upgraded stream service config\n");
    }
    if (needs_save) {
        save_config_atomic(config);
        std::printf("[network-camera] saved upgraded network camera config\n");
    }
    return config;
}

Language load_language() {
    FILE *file = std::fopen("/data/dshanpi_system.conf", "r");
    if (file == nullptr)
        return Language::English;
    char line[160];
    int value = static_cast<int>(Language::English);
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        if (std::sscanf(line, "language=%d", &value) == 1)
            break;
    }
    std::fclose(file);
    if (value < 0 || value > static_cast<int>(Language::Japanese))
        value = static_cast<int>(Language::English);
    return static_cast<Language>(value);
}

bool get_wifi_ip(std::string &ip) {
    int connected = 0;
    struct ifconfig_t config{};
    if (netmgmt_wlan_sta_isconnected(&connected) != 0 || !connected ||
        netmgmt_utils_get_ifconfig(RT_NET_DEV_WLAN_STA, &config) != 0 ||
        config.ip.addr == 0)
        return false;
    const auto *octets = reinterpret_cast<const uint8_t *>(&config.ip.addr);
    char text[16];
    std::snprintf(text, sizeof(text), "%u.%u.%u.%u", octets[0], octets[1],
                  octets[2], octets[3]);
    ip = text;
    return true;
}

bool probe_sensor(int csi, k_vicap_sensor_type &sensor_type) {
    k_vicap_probe_config probe{};
    k_vicap_sensor_info info{};
    probe.csi_num = csi;
    probe.width = 1920;
    probe.height = 1080;
    probe.fps = 30;
    if (kd_mpi_sensor_adapt_get(&probe, &info) != 0)
        return false;
    sensor_type = info.sensor_type;
    return true;
}

class RtspService {
public:
    bool Start() {
        if (running_.load())
            return true;
        constexpr int kAttempts = 8;
        for (int attempt = 1; attempt <= kAttempts; ++attempt) {
            if (server_.Init(8554, nullptr) == 0)
                break;
            server_.DeInit();
            if (attempt == kAttempts)
                return false;
            std::printf("[network-camera] RTSP port busy, retry %d/%d\n",
                        attempt, kAttempts);
            usleep(250 * 1000);
        }
        SessionAttr attr{};
        attr.with_audio = false;
        attr.with_audio_backchannel = false;
        attr.with_video = true;
        attr.video_type = VideoType::kVideoTypeH264;
        if (server_.CreateSession(session_, attr) < 0) {
            server_.DeInit();
            return false;
        }
        server_.Start();
        running_.store(true);
        std::printf("[network-camera] RTSP service started on port 8554\n");
        return true;
    }

    void Stop() {
        if (!running_.exchange(false))
            return;
        server_.DeInit();
        std::printf("[network-camera] RTSP service stopped\n");
    }

    void Send(const void *data, size_t size, uint64_t timestamp) {
        if (running_.load())
            server_.SendVideoData(session_, static_cast<const uint8_t *>(data),
                                  size, timestamp);
    }

    bool Running() const { return running_.load(); }
    ~RtspService() { Stop(); }

private:
    KdRtspServer server_;
    const std::string session_{"live"};
    std::atomic<bool> running_{false};
};

class FrameHub : public IOnVEncData {
public:
    FrameHub(RtspService &rtsp, RtmpPlayServer &rtmp,
             RtmpPublisher &publisher)
        : rtsp_(rtsp), rtmp_(rtmp), publisher_(publisher) {}

    void Configure(bool rtsp, bool rtmp, bool push) {
        std::lock_guard<std::mutex> lock(mutex_);
        rtsp_enabled_ = rtsp;
        rtmp_enabled_ = rtmp;
        push_enabled_ = push;
    }

    void OnVEncData(k_u32 channel, void *data, size_t size,
                    k_venc_pack_type type, uint64_t timestamp) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (rtsp_enabled_)
            rtsp_.Send(data, size, timestamp);
        if (rtmp_enabled_)
            rtmp_.OnEncoded(data, size, type, timestamp);
        if (push_enabled_)
            publisher_.OnVEncData(channel, data, size, type, timestamp);
    }

private:
    RtspService &rtsp_;
    RtmpPlayServer &rtmp_;
    RtmpPublisher &publisher_;
    std::mutex mutex_;
    bool rtsp_enabled_{false};
    bool rtmp_enabled_{false};
    bool push_enabled_{false};
};

class CameraSource {
public:
    bool Start(StreamMode mode, VideoResolution resolution,
               IOnVEncData *sink) {
        mode_ = mode;
        const unsigned width = resolution_width(resolution);
        const unsigned height = resolution_height(resolution);
        const unsigned bitrate = resolution_bitrate(resolution);
        if (mode == StreamMode::Dual) {
            if (dual_.Init(sink, width, height, bitrate) < 0) {
                dual_.DeInit();
                return false;
            }
            dual_ready_ = true;
            if (dual_.Start() < 0) {
                Stop();
                return false;
            }
            started_ = true;
            return true;
        }
        KdMediaInputConfig input;
        input.video_valid = true;
        input.video_type = KdMediaVideoType::kVideoTypeH264;
        input.venc_width = static_cast<int>(width);
        input.venc_height = static_cast<int>(height);
        input.bitrate_kbps = static_cast<int>(bitrate);
        int csi = mode == StreamMode::Front ? 2 : 0;
        input.mirror = mode == StreamMode::Front ? VICAP_MIRROR_VER
                                                 : VICAP_MIRROR_NONE;
        if (!probe_sensor(csi, input.sensor_type) || media_.Init(input) < 0)
            return false;
        media_ready_ = true;
        if (media_.CreateVcapVEnc(sink) < 0) {
            Stop();
            return false;
        }
        venc_ready_ = true;
        if (media_.StartVcapVEnc() < 0) {
            Stop();
            return false;
        }
        started_ = true;
        return true;
    }

    void Stop() {
        if (mode_ == StreamMode::Dual) {
            if (dual_ready_) {
                dual_.DeInit();
                dual_ready_ = false;
            }
        } else {
            if (started_ && media_ready_)
                media_.StopVcapVEnc();
            if (venc_ready_) {
                media_.DestroyVcapVEnc();
                venc_ready_ = false;
            }
            if (media_ready_) {
                media_.Deinit();
                media_ready_ = false;
            }
        }
        started_ = false;
    }

    ~CameraSource() { Stop(); }

private:
    StreamMode mode_{StreamMode::Rear};
    KdMedia media_;
    DualRtspPipeline dual_;
    bool dual_ready_{false};
    bool media_ready_{false};
    bool venc_ready_{false};
    bool started_{false};
};

lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                     const lv_font_t *font, uint32_t color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

void style_card(lv_obj_t *card, uint32_t color = 0xFFFFFF) {
    lv_obj_set_style_bg_color(card, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
}

void show_page(Page page) {
    g_page = page;
    for (int index = 0; index < static_cast<int>(Page::Count); ++index) {
        bool active = index == static_cast<int>(page);
        if (g_pages[index] != nullptr) {
            if (active)
                lv_obj_clear_flag(g_pages[index], LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(g_pages[index], LV_OBJ_FLAG_HIDDEN);
        }
        if (g_tabs[index] != nullptr) {
            lv_obj_set_style_bg_color(g_tabs[index],
                                      lv_color_hex(active ? 0xDCE7FF
                                                          : 0xF8FAFC), 0);
            lv_obj_set_style_text_color(g_tabs[index],
                                        lv_color_hex(active ? 0x244A8D
                                                            : 0x475569), 0);
        }
    }
}

void tab_cb(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
        !lv_k230_touch_accept_click())
        return;
    intptr_t value = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
    if (value >= 0 && value < static_cast<int>(Page::Count))
        show_page(static_cast<Page>(value));
}

void back_cb(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED &&
        lv_k230_touch_accept_click())
        g_exit.store(true);
}

void update_mode_buttons(StreamMode selected) {
    for (int index = 0; index < static_cast<int>(StreamMode::Count); ++index) {
        lv_obj_t *button = g_mode_buttons[index];
        if (button == nullptr)
            continue;
        bool active = index == static_cast<int>(selected);
        lv_obj_set_style_bg_color(button,
                                  lv_color_hex(active ? 0x3F64AF
                                                      : 0xEEF2F7), 0);
        lv_obj_set_style_text_color(button,
                                    lv_color_hex(active ? 0xFFFFFF
                                                        : 0x334155), 0);
    }
}

void mode_cb(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
        !lv_k230_touch_accept_click() || g_url_dialog != nullptr)
        return;
    intptr_t value = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
    if (value < 0 || value >= static_cast<int>(StreamMode::Count))
        return;
    StreamMode mode = static_cast<StreamMode>(value);
    g_requested_mode.store(mode);
    update_mode_buttons(mode);
    if (g_status_label != nullptr)
        lv_label_set_text(g_status_label,
                          tr("Switching camera source...", "正在切换摄像头...",
                             "正在切換攝影機...", "カメラを切替中..."));
}

void update_resolution_buttons(VideoResolution selected) {
    for (int index = 0;
         index < static_cast<int>(VideoResolution::Count); ++index) {
        lv_obj_t *button = g_resolution_buttons[index];
        if (button == nullptr)
            continue;
        const bool active = index == static_cast<int>(selected);
        lv_obj_set_style_bg_color(
            button, lv_color_hex(active ? 0x3F64AF : 0xEEF2F7), 0);
        lv_obj_t *label = lv_obj_get_child(button, 0);
        if (label != nullptr)
            lv_obj_set_style_text_color(
                label, lv_color_hex(active ? 0xFFFFFF : 0x334155), 0);
    }
}

void resolution_cb(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
        !lv_k230_touch_accept_click() || g_url_dialog != nullptr)
        return;
    const intptr_t value =
        reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
    if (value < 0 ||
        value >= static_cast<int>(VideoResolution::Count))
        return;
    const auto resolution = static_cast<VideoResolution>(value);
    g_requested_resolution.store(resolution);
    update_resolution_buttons(resolution);
    if (g_status_label != nullptr)
        lv_label_set_text(
            g_status_label,
            tr("Switching video resolution...", "正在切换视频分辨率...",
               "正在切換視訊解析度...", "映像解像度を切替中..."));
}

void service_toggle_cb(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED ||
        g_url_dialog != nullptr)
        return;
    intptr_t bit = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
    lv_obj_t *toggle = static_cast<lv_obj_t *>(lv_event_get_target(event));
    uint8_t services = g_requested_services.load();
    const bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    if (enabled)
        services |= static_cast<uint8_t>(bit);
    else
        services &= ~static_cast<uint8_t>(bit);
    for (int index = 0; index < 3; ++index) {
        if (g_service_toggles[index] == nullptr)
            continue;
        const uint8_t service_bit = static_cast<uint8_t>(1U << index);
        if ((services & service_bit) != 0)
            lv_obj_add_state(g_service_toggles[index], LV_STATE_CHECKED);
        else
            lv_obj_remove_state(g_service_toggles[index], LV_STATE_CHECKED);
    }
    g_requested_services.store(services);
    g_services_changed.store(true);
}

bool valid_rtmp_url(const char *url) {
    if (url == nullptr || std::strncmp(url, "rtmp://", 7) != 0)
        return false;
    const char *authority = url + 7;
    const char *path = std::strchr(url + 7, '/');
    return path != nullptr && path > authority && path[1] != '\0' &&
           std::strchr(url, ' ') == nullptr;
}

bool split_rtmp_url(const std::string &url, std::string &authority,
                    std::string &path) {
    if (url.rfind("rtmp://", 0) != 0)
        return false;
    const size_t path_start = url.find('/', 7);
    if (path_start == std::string::npos || path_start == 7)
        return false;
    authority = url.substr(7, path_start - 7);
    path = url.substr(path_start);
    return !authority.empty() && !path.empty();
}

std::string current_url_editor_value() {
    const char *text = g_url_input == nullptr ? "" :
                       lv_textarea_get_text(g_url_input);
    if (g_url_quick_mode)
        return std::string("rtmp://") + text + g_url_path;
    return text;
}

void refresh_url_preview() {
    if (g_url_preview == nullptr)
        return;
    const std::string preview = "RTMP  " + current_url_editor_value();
    lv_label_set_text(g_url_preview, preview.c_str());
}

void set_url_editor_error(bool error) {
    if (g_url_input != nullptr) {
        lv_obj_set_style_border_width(g_url_input, 2, 0);
        lv_obj_set_style_border_color(
            g_url_input, lv_color_hex(error ? 0xBA1A1A : 0x9AA8BC), 0);
    }
    if (g_url_hint != nullptr) {
        lv_label_set_text(
            g_url_hint,
            error ? tr("Enter a valid RTMP address",
                       "请输入有效的 RTMP 地址",
                       "請輸入有效的 RTMP 位址",
                       "有効な RTMP アドレスを入力")
                  : (g_url_quick_mode
                         ? tr("Quick edit: server IP / port",
                              "快速修改服务器 IP / 端口",
                              "快速修改伺服器 IP / 連接埠",
                              "クイック編集：サーバー IP / ポート")
                         : tr("Full address mode: domain, path and stream key",
                              "完整地址：可修改域名、路径和串流密钥",
                              "完整位址：可修改網域、路徑和串流金鑰",
                              "完全編集：ドメイン、パス、ストリームキー")));
        lv_obj_set_style_text_color(
            g_url_hint, lv_color_hex(error ? 0xBA1A1A : 0x64748B), 0);
    }
}

void url_input_changed_cb(lv_event_t *) {
    set_url_editor_error(false);
    refresh_url_preview();
}

void set_url_editor_mode(bool quick) {
    if (g_url_input == nullptr || g_url_keyboard == nullptr)
        return;

    if (quick == g_url_quick_mode)
        return;

    if (quick) {
        std::string authority;
        std::string path;
        if (!split_rtmp_url(lv_textarea_get_text(g_url_input), authority,
                            path)) {
            set_url_editor_error(true);
            return;
        }
        g_url_path = path;
        g_url_quick_mode = true;
        lv_keyboard_set_mode(g_url_keyboard, LV_KEYBOARD_MODE_NUMBER);
        lv_textarea_set_text(g_url_input, authority.c_str());
    } else {
        const std::string full_url = current_url_editor_value();
        g_url_quick_mode = false;
        lv_keyboard_set_mode(g_url_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_textarea_set_text(g_url_input, full_url.c_str());
    }
    lv_textarea_set_cursor_pos(g_url_input, LV_TEXTAREA_CURSOR_LAST);
    set_url_editor_error(false);
    refresh_url_preview();
}

void close_url_dialog() {
    if (g_url_dialog != nullptr)
        lv_obj_delete(g_url_dialog);
    g_url_dialog = nullptr;
    g_url_input = nullptr;
    g_url_keyboard = nullptr;
    g_url_hint = nullptr;
    g_url_preview = nullptr;
}

void cancel_url_cb(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED &&
        lv_k230_touch_accept_click())
        close_url_dialog();
}

void save_url_editor() {
    if (g_url_input == nullptr)
        return;
    const std::string url = current_url_editor_value();
    if (!valid_rtmp_url(url.c_str())) {
        set_url_editor_error(true);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_config_mutex);
        g_config.push_url = url;
        save_config_atomic(g_config);
    }
    if (g_url_label != nullptr)
        lv_label_set_text(g_url_label, url.c_str());
    g_push_url_changed.store(true);
    close_url_dialog();
}

void url_keyboard_cb(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED ||
        g_url_input == nullptr || !lv_k230_touch_accept_click())
        return;

    lv_obj_t *keyboard = static_cast<lv_obj_t *>(
        lv_event_get_current_target(event));
    const uint32_t button = lv_buttonmatrix_get_selected_button(keyboard);
    const char *key = lv_buttonmatrix_get_button_text(keyboard, button);
    if (key == nullptr)
        return;

    if ((g_url_quick_mode && button == 3) ||
        (!g_url_quick_mode && button == 19)) {
        lv_textarea_delete_char(g_url_input);
    } else if ((g_url_quick_mode && button == 7) ||
               (!g_url_quick_mode && button == 34)) {
        lv_textarea_set_text(g_url_input, "");
    } else if (g_url_quick_mode && button == 12) {
        set_url_editor_mode(false);
    } else if (!g_url_quick_mode && button == 30) {
        set_url_editor_mode(true);
    } else if (!g_url_quick_mode && button == 31) {
        const lv_keyboard_mode_t mode = lv_keyboard_get_mode(keyboard);
        lv_keyboard_set_mode(
            keyboard, mode == LV_KEYBOARD_MODE_TEXT_UPPER
                          ? LV_KEYBOARD_MODE_TEXT_LOWER
                          : LV_KEYBOARD_MODE_TEXT_UPPER);
    } else if ((g_url_quick_mode && button == 15) ||
               (!g_url_quick_mode && button == 35)) {
        save_url_editor();
    } else {
        lv_textarea_add_text(g_url_input, key);
    }
}

void edit_url_cb(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
        !lv_k230_touch_accept_click() || g_url_dialog != nullptr)
        return;
    g_url_dialog = lv_obj_create(lv_screen_active());
    lv_obj_set_size(g_url_dialog, 620, 456);
    lv_obj_center(g_url_dialog);
    style_card(g_url_dialog);
    lv_obj_set_style_pad_all(g_url_dialog, 14, 0);
    lv_obj_clear_flag(g_url_dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = make_label(
        g_url_dialog,
        tr("Advanced RTMP push address", "高级 RTMP 推送地址",
           "進階 RTMP 推送位址", "高度な RTMP 配信先"),
        &ui_font_network_20, 0x172033);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 4, 0);

    lv_obj_t *cancel = lv_button_create(g_url_dialog);
    lv_obj_set_size(cancel, 96, 42);
    lv_obj_align(cancel, LV_ALIGN_TOP_RIGHT, -2, -4);
    lv_obj_set_style_radius(cancel, 14, 0);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(0xEEF2F7), 0);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(0xDCE5F0),
                              LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(cancel, 0, 0);
    lv_obj_set_ext_click_area(cancel, 6);
    lv_obj_add_event_cb(cancel, cancel_url_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *cancel_label = make_label(
        cancel, tr("Cancel", "取消", "取消", "キャンセル"),
        &ui_font_network_20, 0x475569);
    lv_obj_center(cancel_label);

    g_url_hint = make_label(g_url_dialog, "", &ui_font_network_20, 0x64748B);
    lv_obj_set_pos(g_url_hint, 4, 32);
    g_url_input = lv_textarea_create(g_url_dialog);
    lv_obj_set_size(g_url_input, 580, 56);
    lv_obj_align(g_url_input, LV_ALIGN_TOP_MID, 0, 58);
    lv_textarea_set_one_line(g_url_input, true);
    lv_textarea_set_max_length(g_url_input, 300);
    lv_textarea_set_placeholder_text(g_url_input, "192.168.1.100:1935");
    std::string authority;
    std::string path;
    {
        std::lock_guard<std::mutex> lock(g_config_mutex);
        if (!split_rtmp_url(g_config.push_url, authority, path)) {
            authority = "192.168.1.100";
            path = "/live";
        }
    }
    g_url_quick_mode = true;
    g_url_path = path;
    lv_textarea_set_text(g_url_input, authority.c_str());
    lv_textarea_set_cursor_pos(g_url_input, LV_TEXTAREA_CURSOR_LAST);
    lv_obj_set_style_text_font(g_url_input, &lv_font_montserrat_20, 0);
    lv_obj_add_event_cb(g_url_input, url_input_changed_cb,
                        LV_EVENT_VALUE_CHANGED, nullptr);

    g_url_preview = make_label(g_url_dialog, "", &lv_font_montserrat_14,
                               0x526070);
    lv_obj_set_pos(g_url_preview, 4, 121);
    lv_obj_set_width(g_url_preview, 580);
    lv_label_set_long_mode(g_url_preview, LV_LABEL_LONG_DOT);

    g_url_kb_number[3] = tr("DEL", "删除", "刪除", "削除");
    g_url_kb_number[7] = tr("CLEAR", "清空", "清空", "クリア");
    g_url_kb_number[15] = tr("SAVE", "保存", "儲存", "保存");
    g_url_kb_lower[19] = g_url_kb_number[3];
    g_url_kb_lower[34] = g_url_kb_number[7];
    g_url_kb_lower[35] = g_url_kb_number[15];
    g_url_kb_upper[19] = g_url_kb_number[3];
    g_url_kb_upper[34] = g_url_kb_number[7];
    g_url_kb_upper[35] = g_url_kb_number[15];

    g_url_keyboard = lv_keyboard_create(g_url_dialog);
    lv_obj_set_size(g_url_keyboard, 590, 282);
    lv_obj_align(g_url_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_map(g_url_keyboard, LV_KEYBOARD_MODE_NUMBER,
                        g_url_kb_number, g_url_kb_number_ctrl);
    lv_keyboard_set_map(g_url_keyboard, LV_KEYBOARD_MODE_SPECIAL,
                        g_url_kb_number, g_url_kb_number_ctrl);
    lv_keyboard_set_map(g_url_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER,
                        g_url_kb_lower, g_url_kb_text_ctrl);
    lv_keyboard_set_map(g_url_keyboard, LV_KEYBOARD_MODE_TEXT_UPPER,
                        g_url_kb_upper, g_url_kb_text_ctrl);
    lv_keyboard_set_mode(g_url_keyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_popovers(g_url_keyboard, false);
    lv_keyboard_set_textarea(g_url_keyboard, g_url_input);
    lv_obj_set_style_bg_color(g_url_keyboard, lv_color_hex(0xE2E7EF), 0);
    lv_obj_set_style_border_width(g_url_keyboard, 0, 0);
    lv_obj_set_style_radius(g_url_keyboard, 18, 0);
    lv_obj_set_style_pad_all(g_url_keyboard, 7, 0);
    lv_obj_set_style_pad_row(g_url_keyboard, 6, 0);
    lv_obj_set_style_pad_column(g_url_keyboard, 6, 0);
    lv_obj_set_style_radius(g_url_keyboard, 12, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(g_url_keyboard, lv_color_hex(0xFFFFFF),
                              LV_PART_ITEMS);
    lv_obj_set_style_bg_color(g_url_keyboard, lv_color_hex(0xD9E4F5),
                              static_cast<lv_style_selector_t>(LV_PART_ITEMS) |
                                  static_cast<lv_style_selector_t>(
                                      LV_STATE_PRESSED));
    lv_obj_set_style_text_color(g_url_keyboard, lv_color_hex(0x172033),
                                LV_PART_ITEMS);
    lv_obj_set_style_text_font(g_url_keyboard, &ui_font_network_20,
                               LV_PART_ITEMS);
    lv_obj_set_style_bg_color(g_url_keyboard, lv_color_hex(0x315C9F),
                              static_cast<lv_style_selector_t>(LV_PART_ITEMS) |
                                  static_cast<lv_style_selector_t>(
                                      LV_STATE_CHECKED));
    lv_obj_set_style_text_color(g_url_keyboard, lv_color_hex(0xFFFFFF),
                                static_cast<lv_style_selector_t>(LV_PART_ITEMS) |
                                    static_cast<lv_style_selector_t>(
                                        LV_STATE_CHECKED));
    lv_obj_remove_event_cb(g_url_keyboard, lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(g_url_keyboard, url_keyboard_cb,
                        LV_EVENT_VALUE_CHANGED, nullptr);

    set_url_editor_error(false);
    refresh_url_preview();
    lv_obj_move_foreground(g_url_dialog);
}

lv_obj_t *create_switch_row(lv_obj_t *parent, int y, const char *title,
                            const char *subtitle, bool checked, uint8_t bit,
                            int height = 76) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 576, height);
    lv_obj_set_pos(card, 0, y);
    style_card(card);
    lv_obj_t *name = make_label(card, title, &ui_font_network_20, 0x172033);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 2, 0);
    const lv_font_t *detail_font =
        g_language == Language::English ? &lv_font_montserrat_12
                                        : &ui_font_network_20;
    lv_obj_t *detail = make_label(card, subtitle, detail_font, 0x64748B);
    lv_obj_set_width(detail, 472);
    lv_label_set_long_mode(detail, LV_LABEL_LONG_DOT);
    lv_obj_align(detail, LV_ALIGN_BOTTOM_LEFT, 2, -2);
    lv_obj_t *toggle = lv_switch_create(card);
    lv_obj_set_size(toggle, 64, 36);
    lv_obj_align(toggle, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_ext_click_area(toggle, 12);
    if (checked)
        lv_obj_add_state(toggle, LV_STATE_CHECKED);
    lv_obj_add_event_cb(toggle, service_toggle_cb, LV_EVENT_VALUE_CHANGED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(bit)));
    for (int index = 0; index < 3; ++index) {
        if (bit == static_cast<uint8_t>(1U << index)) {
            g_service_toggles[index] = toggle;
            break;
        }
    }
    return toggle;
}

void create_overview(lv_obj_t *page, const std::string &rtsp_url,
                     const std::string &rtmp_url, StreamMode mode,
                     bool streaming) {
    lv_obj_t *hero = lv_obj_create(page);
    lv_obj_set_size(hero, 576, 126);
    lv_obj_set_pos(hero, 0, 0);
    style_card(hero, 0xEAF1FF);
    lv_obj_t *icon = make_label(hero, LV_SYMBOL_VIDEO,
                                &lv_font_montserrat_28, 0x315DA8);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 16, -10);
    lv_obj_t *title = make_label(
        hero, tr("Main video stream", "主视频流", "主視訊串流", "メイン映像"),
        &ui_font_network_20, 0x172033);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 64, 9);
    g_status_label = make_label(
        hero, streaming ? tr("Camera online", "摄像头在线", "攝影機上線",
                             "カメラ稼働中")
                        : tr("Camera unavailable", "摄像头不可用", "攝影機無法使用",
                             "カメラ利用不可"),
        &ui_font_network_20, streaming ? 0x1D6B48 : 0xBA1A1A);
    lv_obj_align(g_status_label, LV_ALIGN_TOP_LEFT, 64, 43);
    g_client_label = make_label(hero, "0 RTMP clients",
                                &lv_font_montserrat_12, 0x526070);
    lv_obj_align(g_client_label, LV_ALIGN_BOTTOM_LEFT, 64, -6);
    lv_obj_t *badge = make_label(hero, mode_name(mode),
                                 &ui_font_network_20, 0x244A8D);
    lv_obj_align(badge, LV_ALIGN_RIGHT_MID, -14, 0);

    lv_obj_t *source_title = make_label(
        page, tr("Video source", "视频源", "視訊來源", "映像ソース"),
        &ui_font_network_20, 0x334155);
    lv_obj_set_pos(source_title, 4, 137);
    const char *labels[] = {
        tr("Rear", "后摄", "後攝", "リア"),
        tr("Front", "前摄", "前攝", "フロント"),
        tr("Dual", "双摄", "雙攝", "デュアル")};
    for (int index = 0; index < static_cast<int>(StreamMode::Count); ++index) {
        lv_obj_t *button = lv_button_create(page);
        g_mode_buttons[index] = button;
        lv_obj_set_size(button, 184, 48);
        lv_obj_set_pos(button, index * 196, 169);
        lv_obj_set_style_radius(button, 16, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_ext_click_area(button, 8);
        lv_obj_add_event_cb(button, mode_cb, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(
                                static_cast<intptr_t>(index)));
        lv_obj_t *label = make_label(button, labels[index],
                                     &ui_font_network_20, 0x334155);
        lv_obj_center(label);
    }
    update_mode_buttons(mode);

    lv_obj_t *addresses = lv_obj_create(page);
    lv_obj_set_size(addresses, 576, 100);
    lv_obj_set_pos(addresses, 0, 230);
    style_card(addresses);
    g_service_summary = make_label(
        addresses, tr("Local playback addresses", "本地播放地址",
                      "本機播放位址", "ローカル再生アドレス"),
        &ui_font_network_20, 0x172033);
    lv_obj_align(g_service_summary, LV_ALIGN_TOP_LEFT, 2, -3);
    lv_obj_t *rtsp = make_label(addresses, rtsp_url.c_str(),
                                &lv_font_montserrat_14, 0x315DA8);
    lv_obj_align(rtsp, LV_ALIGN_TOP_LEFT, 2, 31);
    lv_obj_t *rtmp = make_label(addresses, rtmp_url.c_str(),
                                &lv_font_montserrat_14, 0x315DA8);
    lv_obj_align(rtmp, LV_ALIGN_TOP_LEFT, 2, 57);
}

void create_services(lv_obj_t *page, const std::string &rtsp_url,
                     const std::string &rtmp_url, const AppConfig &config) {
    create_switch_row(page, 0, "RTSP",
                      rtsp_url.c_str(), config.rtsp_enabled, kServiceRtsp);
    create_switch_row(page, 86,
                      tr("RTMP local playback", "RTMP 本地播放",
                         "RTMP 本機播放", "RTMP ローカル再生"),
                      rtmp_url.c_str(), config.rtmp_enabled, kServiceRtmp,
                      96);
    lv_obj_t *note = make_label(
        page,
        tr("Services stop automatically when this app closes.",
           "退出本应用后，所有网络服务会自动停止。",
           "離開本應用後，所有網路服務會自動停止。",
           "アプリ終了時にすべてのサービスを停止します。"),
        &ui_font_network_20, 0x526070);
    lv_obj_set_pos(note, 4, 216);
}

void create_settings(lv_obj_t *page, const AppConfig &config) {
    lv_obj_t *encoder = lv_obj_create(page);
    lv_obj_set_size(encoder, 576, 94);
    lv_obj_set_pos(encoder, 0, 0);
    style_card(encoder);
    lv_obj_t *title = make_label(
        encoder, tr("Video encoding", "视频编码", "視訊編碼", "映像エンコード"),
        &ui_font_network_20, 0x172033);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 2, 0);
    char encoder_detail[64];
    std::snprintf(encoder_detail, sizeof(encoder_detail),
                  "H.264  |  30 fps  |  %u kbps",
                  resolution_bitrate(config.resolution));
    lv_obj_t *value = make_label(encoder, encoder_detail,
                                 &lv_font_montserrat_14, 0x526070);
    lv_obj_align(value, LV_ALIGN_BOTTOM_LEFT, 2, -4);

    const char *resolution_labels[] = {"720p", "1080p"};
    for (int index = 0;
         index < static_cast<int>(VideoResolution::Count); ++index) {
        lv_obj_t *button = lv_button_create(encoder);
        g_resolution_buttons[index] = button;
        lv_obj_set_size(button, 104, 48);
        lv_obj_set_pos(button, 340 + index * 110, 10);
        lv_obj_set_style_radius(button, 15, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_ext_click_area(button, 7);
        lv_obj_add_event_cb(
            button, resolution_cb, LV_EVENT_CLICKED,
            reinterpret_cast<void *>(static_cast<intptr_t>(index)));
        lv_obj_t *label = make_label(button, resolution_labels[index],
                                     &lv_font_montserrat_20, 0x334155);
        lv_obj_center(label);
    }
    update_resolution_buttons(config.resolution);

    create_switch_row(
        page, 104,
        tr("Advanced RTMP push", "高级 RTMP 主动推送",
           "進階 RTMP 主動推送", "高度な RTMP プッシュ"),
        tr("Optional remote live platform; off by default",
           "可选远端直播平台，默认关闭", "選用遠端直播平台，預設關閉",
           "任意の外部配信先（初期値はオフ）"),
        config.push_enabled, kServicePush, 86);
    lv_obj_t *url_card = lv_obj_create(page);
    lv_obj_set_size(url_card, 576, 116);
    lv_obj_set_pos(url_card, 0, 200);
    style_card(url_card);
    lv_obj_t *url_title = make_label(
        url_card, tr("Remote push address", "远端推送地址", "遠端推送位址",
                     "外部プッシュ先"),
        &ui_font_network_20, 0x172033);
    lv_obj_align(url_title, LV_ALIGN_TOP_LEFT, 2, 0);
    g_url_label = make_label(url_card, config.push_url.c_str(),
                             &lv_font_montserrat_20, 0x526070);
    lv_label_set_long_mode(g_url_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(g_url_label, 430);
    lv_obj_align(g_url_label, LV_ALIGN_BOTTOM_LEFT, 2, -8);
    lv_obj_t *edit = lv_button_create(url_card);
    lv_obj_set_size(edit, 104, 48);
    lv_obj_align(edit, LV_ALIGN_BOTTOM_RIGHT, -2, -2);
    lv_obj_set_style_radius(edit, 16, 0);
    lv_obj_set_style_bg_color(edit, lv_color_hex(0xDCE7FF), 0);
    lv_obj_set_style_shadow_width(edit, 0, 0);
    lv_obj_set_ext_click_area(edit, 8);
    lv_obj_add_event_cb(edit, edit_url_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *edit_label = make_label(
        edit, tr("Edit", "编辑", "編輯", "編集"), &ui_font_network_20,
        0x244A8D);
    lv_obj_center(edit_label);
    g_push_status = make_label(page, "", &lv_font_montserrat_12, 0x64748B);
    lv_obj_set_pos(g_push_status, 4, 326);
}

lv_display_t *create_ui(const std::string &ip, StreamMode mode,
                        bool streaming) {
    if (kd_display_init(ST7701_480_640_DSI_V1) != 0)
        return nullptr;
    lv_init();
    lv_display_t *display = lv_k230_display_create(K_VO_LAYER_OSD0, 255);
    if (display == nullptr) {
        lv_deinit();
        kd_display_deinit();
        return nullptr;
    }
    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_270);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_ARGB8888);
    lv_k230_touch_init(0);
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *back = lv_button_create(screen);
    lv_obj_set_size(back, 58, 58);
    lv_obj_set_pos(back, 4, 2);
    lv_obj_set_ext_click_area(back, 18);
    lv_obj_set_style_radius(back, 18, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *back_icon = make_label(back, LV_SYMBOL_LEFT,
                                     &lv_font_montserrat_28, 0x172033);
    lv_obj_center(back_icon);
    lv_obj_t *title = make_label(
        screen, tr("Network Camera", "网络摄像机", "網路攝影機",
                   "ネットワークカメラ"),
        &ui_font_network_20, 0x172033);
    lv_obj_set_pos(title, 72, 18);
    lv_obj_t *lifetime = make_label(
        screen, tr("App active", "应用运行中", "應用執行中", "アプリ実行中"),
        &ui_font_network_20, 0x1D6B48);
    lv_obj_align(lifetime, LV_ALIGN_TOP_RIGHT, -20, 17);

    const char *tab_names[] = {
        tr("Overview", "概览", "概覽", "概要"),
        tr("Services", "协议服务", "協定服務", "サービス"),
        tr("Settings", "设置", "設定", "設定")};
    for (int index = 0; index < static_cast<int>(Page::Count); ++index) {
        lv_obj_t *tab = lv_button_create(screen);
        g_tabs[index] = tab;
        lv_obj_set_size(tab, 176, 44);
        lv_obj_set_pos(tab, 48 + index * 184, 64);
        lv_obj_set_style_radius(tab, 16, 0);
        lv_obj_set_style_shadow_width(tab, 0, 0);
        lv_obj_set_ext_click_area(tab, 5);
        lv_obj_add_event_cb(tab, tab_cb, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(
                                static_cast<intptr_t>(index)));
        lv_obj_t *label = make_label(tab, tab_names[index],
                                     &ui_font_network_20, 0x475569);
        lv_obj_center(label);
    }

    AppConfig config;
    {
        std::lock_guard<std::mutex> lock(g_config_mutex);
        config = g_config;
    }
    const std::string rtsp_url = "rtsp://" + ip + ":8554/live";
    const std::string rtmp_url = "rtmp://" + ip + ":1935/live";
    for (int index = 0; index < static_cast<int>(Page::Count); ++index) {
        lv_obj_t *page = lv_obj_create(screen);
        g_pages[index] = page;
        lv_obj_set_size(page, 576, 350);
        lv_obj_set_pos(page, 32, 120);
        lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(page, 0, 0);
        lv_obj_set_style_pad_all(page, 0, 0);
        lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    }
    create_overview(g_pages[0], rtsp_url, rtmp_url, mode, streaming);
    create_services(g_pages[1], rtsp_url, rtmp_url, config);
    create_settings(g_pages[2], config);
    show_page(g_page);
    lv_refr_now(display);
    return display;
}

void destroy_ui(lv_display_t *display) {
    g_status_label = nullptr;
    g_service_summary = nullptr;
    g_client_label = nullptr;
    g_push_status = nullptr;
    g_url_label = nullptr;
    g_url_dialog = nullptr;
    g_url_input = nullptr;
    g_url_keyboard = nullptr;
    g_url_hint = nullptr;
    g_url_preview = nullptr;
    for (auto &page : g_pages)
        page = nullptr;
    for (auto &tab : g_tabs)
        tab = nullptr;
    for (auto &button : g_mode_buttons)
        button = nullptr;
    for (auto &button : g_resolution_buttons)
        button = nullptr;
    for (auto &toggle : g_service_toggles)
        toggle = nullptr;
    if (display != nullptr)
        lv_display_delete(display);
    lv_deinit();
    kd_display_deinit();
}

bool init_ui_vb() {
    k_vb_config config{};
    config.max_pool_cnt = 64;
    int result = kd_mpi_vb_set_config(&config);
    if (result == K_SUCCESS)
        result = kd_mpi_vb_init();
    return result == K_SUCCESS;
}

void update_runtime_labels(bool streaming, StreamMode mode,
                           VideoResolution resolution,
                           const RtspService &rtsp,
                           const RtmpPlayServer &rtmp,
                           const RtmpPublisher &publisher) {
    if (g_status_label != nullptr) {
        std::string status;
        uint32_t color = 0x1D6B48;
        if (!streaming) {
            status = tr("Camera unavailable", "摄像头不可用", "攝影機無法使用",
                        "カメラ利用不可");
            color = 0xBA1A1A;
        } else {
            status = std::string(tr("Camera online", "摄像头在线", "攝影機上線",
                                    "カメラ稼働中")) +
                     "  |  " + mode_name(mode) + "  |  " +
                     resolution_name(resolution);
        }
        lv_label_set_text(g_status_label, status.c_str());
        lv_obj_set_style_text_color(g_status_label, lv_color_hex(color), 0);
    }
    if (g_client_label != nullptr) {
        char clients[80];
        std::snprintf(clients, sizeof(clients),
                      "%zu RTMP client%s  |  RTSP %s", rtmp.ClientCount(),
                      rtmp.ClientCount() == 1 ? "" : "s",
                      rtsp.Running() ? "ON" : "OFF");
        lv_label_set_text(g_client_label, clients);
    }
    if (g_push_status != nullptr) {
        std::string text;
        switch (publisher.State()) {
        case RtmpPublisherState::Streaming: text = "RTMP push: LIVE"; break;
        case RtmpPublisherState::Connecting: text = "RTMP push: connecting"; break;
        case RtmpPublisherState::Reconnecting: text = "RTMP push: retrying"; break;
        default: text = "RTMP push: OFF"; break;
        }
        lv_label_set_text(g_push_status, text.c_str());
    }
}
}  // namespace

int main(int, char **) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGPIPE, SIG_IGN);

    std::string ip;
    if (!get_wifi_ip(ip)) {
        std::printf("[network-camera] Wi-Fi is not connected\n");
        return 2;
    }
    g_language = load_language();
    g_config = load_config();
    StreamMode active_mode = g_config.source;
    VideoResolution active_resolution = g_config.resolution;
    g_requested_mode.store(active_mode);
    g_requested_resolution.store(active_resolution);
    uint8_t service_bits =
        (g_config.rtsp_enabled ? kServiceRtsp : 0) |
        (g_config.rtmp_enabled ? kServiceRtmp : 0) |
        (g_config.push_enabled ? kServicePush : 0);
    g_requested_services.store(service_bits);

    RtspService rtsp;
    RtmpPlayServer rtmp;
    RtmpPublisher publisher;
    bool rtsp_running =
        (service_bits & kServiceRtsp) != 0 && rtsp.Start();
    bool rtmp_running =
        (service_bits & kServiceRtmp) != 0 && rtmp.Start(1935);
    bool push_running = false;
    if ((service_bits & kServicePush) != 0)
        push_running = publisher.Start(g_config.push_url);
    if (!rtsp_running)
        service_bits &= ~kServiceRtsp;
    if (!rtmp_running)
        service_bits &= ~kServiceRtmp;
    if (!push_running)
        service_bits &= ~kServicePush;
    g_requested_services.store(service_bits);

    FrameHub hub(rtsp, rtmp, publisher);
    hub.Configure(rtsp_running, rtmp_running, push_running);
    CameraSource source;
    bool streaming = source.Start(active_mode, active_resolution, &hub);
    if (streaming) {
        std::lock_guard<std::mutex> lock(g_config_mutex);
        g_config.source = active_mode;
        g_config.resolution = active_resolution;
        g_config.rtsp_enabled = rtsp_running;
        g_config.rtmp_enabled = rtmp_running;
        g_config.push_enabled = push_running;
        save_config_atomic(g_config);
    }

    bool ui_owns_vb = false;
    if (!streaming) {
        ui_owns_vb = init_ui_vb();
        if (!ui_owns_vb) {
            publisher.Stop();
            rtmp.Stop();
            rtsp.Stop();
            return 5;
        }
    }
    lv_display_t *display = create_ui(ip, active_mode, streaming);
    if (display == nullptr) {
        if (ui_owns_vb)
            kd_mpi_vb_exit();
        source.Stop();
        publisher.Stop();
        rtmp.Stop();
        rtsp.Stop();
        return 5;
    }

    auto last_update = std::chrono::steady_clock::now();
    while (!g_exit.load()) {
        uint32_t delay = lv_timer_handler();
        if (delay == LV_NO_TIMER_READY || delay > 40)
            delay = 16;
        usleep(delay * 1000);

        if (g_services_changed.exchange(false)) {
            uint8_t requested = g_requested_services.load();
            if ((requested & kServiceRtsp) != 0 && !rtsp_running)
                rtsp_running = rtsp.Start();
            if ((requested & kServiceRtsp) == 0 && rtsp_running) {
                hub.Configure(false, rtmp_running, push_running);
                rtsp.Stop();
                rtsp_running = false;
            }
            if ((requested & kServiceRtmp) != 0 && !rtmp_running)
                rtmp_running = rtmp.Start(1935);
            if ((requested & kServiceRtmp) == 0 && rtmp_running) {
                hub.Configure(rtsp_running, false, push_running);
                rtmp.Stop();
                rtmp_running = false;
            }
            if ((requested & kServicePush) != 0 && !push_running) {
                std::lock_guard<std::mutex> lock(g_config_mutex);
                push_running = publisher.Start(g_config.push_url);
            }
            if ((requested & kServicePush) == 0 && push_running) {
                hub.Configure(rtsp_running, rtmp_running, false);
                publisher.Stop();
                push_running = false;
            }
            hub.Configure(rtsp_running, rtmp_running, push_running);
            uint8_t actual = (rtsp_running ? kServiceRtsp : 0) |
                             (rtmp_running ? kServiceRtmp : 0) |
                             (push_running ? kServicePush : 0);
            g_requested_services.store(actual);
            {
                std::lock_guard<std::mutex> lock(g_config_mutex);
                g_config.rtsp_enabled = rtsp_running;
                g_config.rtmp_enabled = rtmp_running;
                g_config.push_enabled = push_running;
                save_config_atomic(g_config);
            }
        }

        if (g_push_url_changed.exchange(false) && push_running) {
            hub.Configure(rtsp_running, rtmp_running, false);
            publisher.Stop();
            {
                std::lock_guard<std::mutex> lock(g_config_mutex);
                push_running = publisher.Start(g_config.push_url);
            }
            hub.Configure(rtsp_running, rtmp_running, push_running);
        }

        StreamMode requested_mode = g_requested_mode.load();
        VideoResolution requested_resolution =
            g_requested_resolution.load();
        if (requested_mode != active_mode ||
            requested_resolution != active_resolution) {
            destroy_ui(display);
            display = nullptr;
            if (ui_owns_vb) {
                kd_mpi_vb_exit();
                ui_owns_vb = false;
            }
            source.Stop();
            usleep(120 * 1000);
            streaming = source.Start(requested_mode, requested_resolution,
                                     &hub);
            active_mode = requested_mode;
            active_resolution = requested_resolution;
            if (streaming) {
                std::lock_guard<std::mutex> lock(g_config_mutex);
                g_config.source = active_mode;
                g_config.resolution = active_resolution;
                save_config_atomic(g_config);
            } else {
                ui_owns_vb = init_ui_vb();
            }
            if (!streaming && !ui_owns_vb)
                break;
            display = create_ui(ip, active_mode, streaming);
            if (display == nullptr)
                break;
            last_update = std::chrono::steady_clock::now();
        }

        if (std::chrono::steady_clock::now() - last_update >= 500ms) {
            update_runtime_labels(streaming, active_mode, active_resolution,
                                  rtsp, rtmp, publisher);
            last_update = std::chrono::steady_clock::now();
        }
    }

    if (display != nullptr)
        destroy_ui(display);
    if (ui_owns_vb)
        kd_mpi_vb_exit();
    source.Stop();
    hub.Configure(false, false, false);
    publisher.Stop();
    rtmp.Stop();
    rtsp.Stop();
    std::printf("[network-camera] all app-scoped services stopped\n");
    return 0;
}
