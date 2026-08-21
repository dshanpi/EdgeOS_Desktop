#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <unistd.h>

#include "dual_stream.h"
#include "hal_netmgmt.h"
#include "lv_k230_display.h"
#include "lv_k230_input_touch.h"
#include "lvgl.h"
#include "media.h"
#include "mpi_sensor_api.h"
#include "mpi_vb_api.h"
#include "rtsp_server.h"

using namespace std::chrono_literals;

namespace {
std::atomic<bool> g_exit{false};

enum class StreamMode { Rear = 0, Front, Dual, Count };
std::atomic<StreamMode> g_requested_mode{StreamMode::Rear};
lv_obj_t *g_status_label = nullptr;
lv_obj_t *g_mode_buttons[static_cast<int>(StreamMode::Count)]{};

constexpr const char *kModePath = "/data/dshanpi_rtsp_mode.conf";

const char *mode_name(StreamMode mode) {
    switch (mode) {
    case StreamMode::Rear: return "Rear CSI0";
    case StreamMode::Front: return "Front CSI2";
    case StreamMode::Dual: return "Dual side-by-side";
    default: return "Rear CSI0";
    }
}

StreamMode load_mode() {
    FILE *file = std::fopen(kModePath, "r");
    if (file == nullptr) {
        char backup[96];
        std::snprintf(backup, sizeof(backup), "%s.bak", kModePath);
        file = std::fopen(backup, "r");
    }
    int value = 0;
    if (file != nullptr) {
        if (std::fscanf(file, "%d", &value) != 1)
            value = 0;
        std::fclose(file);
    }
    if (value < 0 || value >= static_cast<int>(StreamMode::Count))
        value = 0;
    return static_cast<StreamMode>(value);
}

void discard_staged_mode() {
    char temporary[96];
    std::snprintf(temporary, sizeof(temporary), "%s.tmp", kModePath);
    unlink(temporary);
}

bool stage_mode(StreamMode mode) {
    char temporary[96];
    std::snprintf(temporary, sizeof(temporary), "%s.tmp", kModePath);
    unlink(temporary);
    FILE *file = std::fopen(temporary, "w");
    if (file == nullptr) {
        std::printf("[rtsp-stream] cannot stage default source: %s\n",
                    std::strerror(errno));
        return false;
    }
    bool failed = std::fprintf(file, "%d\n", static_cast<int>(mode)) < 0;
    if (!failed)
        failed = std::fflush(file) != 0;
    if (!failed)
        failed = fsync(fileno(file)) != 0;
    if (std::fclose(file) != 0)
        failed = true;
    if (failed) {
        std::printf("[rtsp-stream] cannot flush staged default source: %s\n",
                    std::strerror(errno));
        unlink(temporary);
        return false;
    }
    return true;
}

bool commit_staged_mode(StreamMode mode) {
    char temporary[96];
    char backup[96];
    std::snprintf(temporary, sizeof(temporary), "%s.tmp", kModePath);
    std::snprintf(backup, sizeof(backup), "%s.bak", kModePath);

    /*
     * RT-Smart's FAT rename does not replace an existing destination.  Move
     * the previous value aside first so a failed second rename can restore
     * it, and load_mode() can also recover the backup after a power loss.
     */
    unlink(backup);
    bool had_previous = std::rename(kModePath, backup) == 0;
    if (!had_previous && errno != ENOENT) {
        std::printf("[rtsp-stream] cannot back up default source: %s\n",
                    std::strerror(errno));
        unlink(temporary);
        return false;
    }
    if (std::rename(temporary, kModePath) != 0) {
        std::printf("[rtsp-stream] cannot commit default source: %s\n",
                    std::strerror(errno));
        unlink(temporary);
        if (had_previous)
            std::rename(backup, kModePath);
        return false;
    }
    unlink(backup);
    std::printf("[rtsp-stream] saved default source: %s\n",
                mode_name(mode));
    return true;
}

void signal_handler(int) {
    g_exit.store(true);
}

bool get_wifi_ip(std::string &ip) {
    int connected = 0;
    struct ifconfig_t config {};
    if (netmgmt_wlan_sta_isconnected(&connected) != 0 || !connected ||
        netmgmt_utils_get_ifconfig(RT_NET_DEV_WLAN_STA, &config) != 0 ||
        config.ip.addr == 0)
        return false;

    const auto *octet =
        reinterpret_cast<const uint8_t *>(&config.ip.addr);
    char text[16];
    std::snprintf(text, sizeof(text), "%u.%u.%u.%u",
                  octet[0], octet[1], octet[2], octet[3]);
    ip = text;
    return true;
}

bool probe_sensor(int csi, k_vicap_sensor_type &sensor_type) {
    k_vicap_probe_config probe {};
    k_vicap_sensor_info info {};
    probe.csi_num = csi;
    probe.width = 1920;
    probe.height = 1080;
    probe.fps = 30;
    if (kd_mpi_sensor_adapt_get(&probe, &info) != 0)
        return false;
    sensor_type = info.sensor_type;
    return true;
}

class RtspCameraServer : public IOnVEncData {
public:
    void OnVEncData(k_u32, void *data, size_t size, k_venc_pack_type,
                    uint64_t timestamp) override {
        if (started_.load())
            server_.SendVideoData(session_, static_cast<const uint8_t *>(data),
                                  size, timestamp);
    }

    int InitServer() {
        if (server_ready_)
            return 0;

        /*
         * A previous RTSP process can leave the TCP endpoint unavailable for
         * a short time on RT-Smart.  Retrying is safe now that KdRtspServer
         * also releases its scheduler/environment on a failed Init().
         */
        constexpr int kBindAttempts = 8;
        for (int attempt = 1; attempt <= kBindAttempts; ++attempt) {
            if (server_.Init(8554, nullptr) == 0)
                break;
            server_.DeInit();
            if (attempt == kBindAttempts)
                return -1;
            std::printf("[rtsp-stream] port 8554 busy, retrying (%d/%d)\n",
                        attempt, kBindAttempts);
            usleep(250 * 1000);
        }

        SessionAttr attr {};
        attr.with_audio = false;
        attr.with_audio_backchannel = false;
        attr.with_video = true;
        attr.video_type = VideoType::kVideoTypeH264;
        if (server_.CreateSession(session_, attr) < 0) {
            server_.DeInit();
            return -1;
        }
        server_.Start();
        server_ready_ = true;
        return 0;
    }

    int StartSource(StreamMode mode, KdMediaInputConfig &config) {
        if (!server_ready_)
            return -1;
        mode_ = mode;
        if (mode_ == StreamMode::Dual) {
            if (dual_.Init(this) < 0) {
                dual_.DeInit();
                return -1;
            }
            dual_ready_ = true;
        } else {
            if (media_.Init(config) < 0)
                return -1;
            media_ready_ = true;
            if (media_.CreateVcapVEnc(this) < 0)
                return -1;
            venc_ready_ = true;
        }
        started_.store(true);
        int result = mode_ == StreamMode::Dual
                         ? dual_.Start()
                         : media_.StartVcapVEnc();
        if (result < 0) {
            started_.store(false);
            StopSource();
            return -1;
        }
        return 0;
    }

    void StopSource() {
        bool was_started = started_.exchange(false);
        if (mode_ == StreamMode::Dual) {
            if (dual_ready_) {
                dual_.DeInit();
                dual_ready_ = false;
            }
        } else if (was_started && media_ready_) {
            media_.StopVcapVEnc();
        }
        if (venc_ready_) {
            media_.DestroyVcapVEnc();
            venc_ready_ = false;
        }
        if (media_ready_) {
            media_.Deinit();
            media_ready_ = false;
        }
    }

    void DeInit() {
        StopSource();
        /* DeInit() stops the event loop before closing the listener.  Keep a
         * single teardown path so port 8554 is closed exactly once. */
        server_.DeInit();
        server_ready_ = false;
    }

private:
    KdRtspServer server_;
    KdMedia media_;
    DualRtspPipeline dual_;
    const std::string session_{"live"};
    StreamMode mode_{StreamMode::Rear};
    std::atomic<bool> started_{false};
    bool media_ready_{false};
    bool venc_ready_{false};
    bool dual_ready_{false};
    bool server_ready_{false};
};

void back_cb(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED &&
        lv_k230_touch_accept_click())
        g_exit.store(true);
}

void update_mode_buttons(StreamMode active) {
    for (int index = 0; index < static_cast<int>(StreamMode::Count); ++index) {
        if (g_mode_buttons[index] == nullptr)
            continue;
        bool selected = index == static_cast<int>(active);
        lv_obj_set_style_bg_color(
            g_mode_buttons[index],
            lv_color_hex(selected ? 0x3972E6 : 0x273449), 0);
        lv_obj_set_style_border_width(g_mode_buttons[index],
                                      selected ? 0 : 1, 0);
        lv_obj_set_style_border_color(g_mode_buttons[index],
                                      lv_color_hex(0x43536B), 0);
    }
}

void set_status(const char *text, uint32_t color) {
    if (g_status_label == nullptr)
        return;
    lv_label_set_text(g_status_label, text);
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(color), 0);
}

void mode_cb(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
        !lv_k230_touch_accept_click())
        return;
    intptr_t value = reinterpret_cast<intptr_t>(
        lv_event_get_user_data(event));
    if (value < 0 || value >= static_cast<int>(StreamMode::Count))
        return;
    StreamMode mode = static_cast<StreamMode>(value);
    g_requested_mode.store(mode);
    update_mode_buttons(mode);
    set_status(LV_SYMBOL_REFRESH "  Switching camera source...", 0xF6C85F);
}

lv_display_t *create_status_ui(const std::string &url, StreamMode mode) {
    if (kd_display_init(ST7701_480_640_DSI_V1) != 0)
        return nullptr;
    lv_init();
    lv_display_t *display =
        lv_k230_display_create(K_VO_LAYER_OSD0, 255);
    if (display == nullptr) {
        lv_deinit();
        kd_display_deinit();
        return nullptr;
    }
    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_270);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_ARGB8888);
    lv_k230_touch_init(0);

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101722), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *back = lv_button_create(screen);
    lv_obj_set_size(back, 64, 64);
    lv_obj_set_pos(back, 14, 14);
    lv_obj_set_ext_click_area(back, 24);
    lv_obj_set_style_radius(back, 18, 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *back_text = lv_label_create(back);
    lv_label_set_text(back_text, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_text, &lv_font_montserrat_20, 0);
    lv_obj_center(back_text);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "RTSP Stream");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

    const char *mode_labels[] = {"Rear CSI0", "Front CSI2", "Dual | Side"};
    constexpr int button_width = 154;
    constexpr int button_gap = 8;
    constexpr int button_start =
        (640 - button_width * 3 - button_gap * 2) / 2;
    for (int index = 0; index < static_cast<int>(StreamMode::Count); ++index) {
        lv_obj_t *button = lv_button_create(screen);
        g_mode_buttons[index] = button;
        lv_obj_set_size(button, button_width, 52);
        lv_obj_set_pos(button,
                       button_start + index * (button_width + button_gap),
                       88);
        lv_obj_set_style_radius(button, 16, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_ext_click_area(button, 8);
        lv_obj_add_event_cb(button, mode_cb, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(
                                static_cast<intptr_t>(index)));
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, mode_labels[index]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(label);
    }
    update_mode_buttons(mode);

    g_status_label = lv_label_create(screen);
    lv_label_set_text(g_status_label, LV_SYMBOL_REFRESH "  Starting stream...");
    lv_obj_set_style_text_font(g_status_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(0xF6C85F), 0);
    lv_obj_align(g_status_label, LV_ALIGN_TOP_MID, 0, 166);

    lv_obj_t *card = lv_obj_create(screen);
    lv_obj_set_size(card, 570, 108);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 42);
    lv_obj_set_style_radius(card, 22, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x202B3B), 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *hint = lv_label_create(card);
    lv_label_set_text(hint, "RTSP address");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x9CA9BA), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 10, 6);
    lv_obj_t *address = lv_label_create(card);
    lv_label_set_text(address, url.c_str());
    lv_obj_set_style_text_color(address, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(address, &lv_font_montserrat_20, 0);
    lv_obj_align(address, LV_ALIGN_LEFT_MID, 10, 18);

    lv_obj_t *footer = lv_label_create(screen);
    lv_label_set_text(footer, "Open this address with VLC");
    lv_obj_set_style_text_color(footer, lv_color_hex(0x9CA9BA), 0);
    lv_obj_set_style_text_font(footer, &lv_font_montserrat_16, 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_refr_now(display);
    return display;
}

void destroy_status_ui(lv_display_t *display) {
    g_status_label = nullptr;
    for (auto &button : g_mode_buttons)
        button = nullptr;
    if (display != nullptr)
        lv_display_delete(display);
    lv_deinit();
    kd_display_deinit();
}

bool init_status_ui_vb() {
    k_vb_config config {};
    config.max_pool_cnt = 64;
    int result = kd_mpi_vb_set_config(&config);
    if (result == K_SUCCESS)
        result = kd_mpi_vb_init();
    if (result != K_SUCCESS)
        std::printf("[rtsp-stream] status UI VB initialization failed: %d\n",
                    result);
    return result == K_SUCCESS;
}

void deinit_status_ui_vb() {
    int result = kd_mpi_vb_exit();
    if (result != K_SUCCESS)
        std::printf("[rtsp-stream] status UI VB release failed: %d\n",
                    result);
}
}  // namespace

int main(int, char **) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGPIPE, SIG_IGN);

    std::string ip;
    if (!get_wifi_ip(ip)) {
        std::printf("[rtsp-stream] Wi-Fi is not connected\n");
        return 2;
    }

    const std::string url = "rtsp://" + ip + ":8554/live";
    StreamMode active_mode = load_mode();
    g_requested_mode.store(active_mode);
    std::printf("[rtsp-stream] selected source: %s, URL: %s\n",
                mode_name(active_mode), url.c_str());

    RtspCameraServer server;
    if (server.InitServer() < 0) {
        std::printf("[rtsp-stream] unable to open RTSP port 8554\n");
        return 4;
    }

    auto start_source = [&](StreamMode mode) {
        KdMediaInputConfig config;
        config.video_valid = true;
        config.video_type = KdMediaVideoType::kVideoTypeH264;
        config.venc_width = 1280;
        config.venc_height = 720;
        config.bitrate_kbps = 2500;
        if (mode != StreamMode::Dual) {
            int csi = mode == StreamMode::Front ? 2 : 0;
            /* RTSP clients render encoded frames without the local VO
             * rotation.  CSI0 needs no mirror and CSI2 needs only the
             * vertical correction; adding a horizontal component makes both
             * sources appear left/right reversed to the client. */
            config.mirror = mode == StreamMode::Front
                                ? VICAP_MIRROR_VER
                                : VICAP_MIRROR_NONE;
            if (!probe_sensor(csi, config.sensor_type)) {
                std::printf("[rtsp-stream] unable to detect camera on CSI%d\n",
                            csi);
                return false;
            }
        }
        if (server.StartSource(mode, config) < 0) {
            server.StopSource();
            return false;
        }
        return true;
    };

    /*
     * lv_k230_display_create() allocates its OSD buffers from VB.  The
     * desktop worker intentionally releases VB before launching us, so the
     * stream must initialize VB before the status display is created.
     *
     * Keep the same ordering during a source switch as well: delete the
     * display first, stop the source (which exits VB), start the new source,
     * then recreate the display.  Otherwise KdMedia::Deinit() can invalidate
     * the VB pool while LVGL still owns OSD blocks.
     */
    bool mode_staged = stage_mode(active_mode);
    bool streaming = start_source(active_mode);
    if (streaming && mode_staged)
        commit_staged_mode(active_mode);
    else
        discard_staged_mode();
    bool status_ui_owns_vb = false;
    if (!streaming) {
        status_ui_owns_vb = init_status_ui_vb();
        if (!status_ui_owns_vb)
            return 5;
    }

    lv_display_t *display = create_status_ui(url, active_mode);
    if (display == nullptr) {
        std::printf("[rtsp-stream] status UI initialization failed\n");
        if (status_ui_owns_vb)
            deinit_status_ui_vb();
        server.DeInit();
        return 5;
    }
    if (streaming) {
        char status[96];
        std::snprintf(status, sizeof(status), LV_SYMBOL_WIFI "  Streaming  |  %s",
                      mode_name(active_mode));
        set_status(status, 0x67E8A5);
    } else {
        set_status(LV_SYMBOL_WARNING "  Camera source unavailable", 0xFF8A80);
    }
    lv_refr_now(display);

    while (!g_exit.load()) {
        uint32_t delay = lv_timer_handler();
        if (delay == LV_NO_TIMER_READY || delay > 50)
            delay = 20;
        usleep(delay * 1000);

        StreamMode requested = g_requested_mode.load();
        if (requested == active_mode)
            continue;
        set_status(LV_SYMBOL_REFRESH "  Switching camera source...", 0xF6C85F);
        lv_refr_now(display);

        destroy_status_ui(display);
        display = nullptr;
        if (status_ui_owns_vb) {
            deinit_status_ui_vb();
            status_ui_owns_vb = false;
        }
        server.StopSource();
        streaming = false;
        usleep(120 * 1000);
        active_mode = requested;
        mode_staged = stage_mode(active_mode);
        streaming = start_source(active_mode);
        if (streaming && mode_staged)
            commit_staged_mode(active_mode);
        else
            discard_staged_mode();
        if (!streaming)
            status_ui_owns_vb = init_status_ui_vb();
        if (!streaming && !status_ui_owns_vb) {
            std::printf("[rtsp-stream] unable to restore status UI VB\n");
            break;
        }
        display = create_status_ui(url, active_mode);
        if (display == nullptr) {
            std::printf("[rtsp-stream] unable to recreate status UI\n");
            break;
        }
        if (streaming) {
            char status[96];
            std::snprintf(status, sizeof(status),
                          LV_SYMBOL_WIFI "  Streaming  |  %s",
                          mode_name(active_mode));
            set_status(status, 0x67E8A5);
        } else {
            set_status(LV_SYMBOL_WARNING "  Camera source unavailable",
                       0xFF8A80);
        }
        update_mode_buttons(active_mode);
        lv_refr_now(display);
    }
    if (streaming)
        set_status("Stopping stream...", 0x9CA9BA);
    if (display != nullptr) {
        lv_refr_now(display);
        destroy_status_ui(display);
    }
    if (status_ui_owns_vb)
        deinit_status_ui_vb();
    server.DeInit();
    return 0;
}
