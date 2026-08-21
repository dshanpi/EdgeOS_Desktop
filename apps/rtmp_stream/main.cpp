#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
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
#include "rtmp_publisher.h"

namespace {
std::atomic<bool> g_exit{false};

enum class StreamMode { Rear = 0, Front, Dual, Count };
std::atomic<StreamMode> g_requested_mode{StreamMode::Rear};
std::atomic<bool> g_url_changed{false};
std::mutex g_url_mutex;
std::string g_requested_url;
lv_obj_t *g_status_label = nullptr;
lv_obj_t *g_url_label = nullptr;
lv_obj_t *g_url_dialog = nullptr;
lv_obj_t *g_url_input = nullptr;
lv_obj_t *g_mode_buttons[static_cast<int>(StreamMode::Count)]{};

constexpr const char *kModePath = "/data/dshanpi_rtmp_mode.conf";
constexpr const char *kUrlPath = "/data/dshanpi_rtmp_url.conf";
constexpr const char *kDefaultUrl = "rtmp://192.168.1.100/live/canmv";

const char *mode_name(StreamMode mode) {
    switch (mode) {
    case StreamMode::Rear: return "Rear CSI0";
    case StreamMode::Front: return "Front CSI2";
    case StreamMode::Dual: return "Dual side-by-side";
    default: return "Rear CSI0";
    }
}

bool save_text_atomic(const char *path, const std::string &value) {
    char temporary[128];
    char backup[128];
    std::snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    std::snprintf(backup, sizeof(backup), "%s.bak", path);
    unlink(temporary);
    FILE *file = std::fopen(temporary, "w");
    if (file == nullptr)
        return false;
    bool failed = std::fprintf(file, "%s\n", value.c_str()) < 0 ||
                  std::fflush(file) != 0 || fsync(fileno(file)) != 0;
    if (std::fclose(file) != 0)
        failed = true;
    if (failed) {
        unlink(temporary);
        return false;
    }
    unlink(backup);
    bool had_previous = std::rename(path, backup) == 0;
    if (!had_previous && errno != ENOENT) {
        unlink(temporary);
        return false;
    }
    if (std::rename(temporary, path) != 0) {
        if (had_previous)
            std::rename(backup, path);
        unlink(temporary);
        return false;
    }
    unlink(backup);
    return true;
}

std::string load_text(const char *path, const char *fallback) {
    char backup[128];
    std::snprintf(backup, sizeof(backup), "%s.bak", path);
    FILE *file = std::fopen(path, "r");
    if (file == nullptr)
        file = std::fopen(backup, "r");
    if (file == nullptr)
        return fallback;
    char value[320]{};
    if (std::fgets(value, sizeof(value), file) == nullptr)
        value[0] = '\0';
    std::fclose(file);
    value[std::strcspn(value, "\r\n")] = '\0';
    return value[0] == '\0' ? fallback : value;
}

StreamMode load_mode() {
    std::string value = load_text(kModePath, "0");
    int mode = std::atoi(value.c_str());
    if (mode < 0 || mode >= static_cast<int>(StreamMode::Count))
        mode = 0;
    return static_cast<StreamMode>(mode);
}

void save_mode(StreamMode mode) {
    if (save_text_atomic(kModePath,
                         std::to_string(static_cast<int>(mode))))
        std::printf("[rtmp-stream] saved default source: %s\n",
                    mode_name(mode));
    else
        std::printf("[rtmp-stream] cannot save default source: %s\n",
                    std::strerror(errno));
}

void signal_handler(int) {
    g_exit.store(true);
}

bool wifi_connected() {
    int connected = 0;
    struct ifconfig_t config{};
    return netmgmt_wlan_sta_isconnected(&connected) == 0 && connected &&
           netmgmt_utils_get_ifconfig(RT_NET_DEV_WLAN_STA, &config) == 0 &&
           config.ip.addr != 0;
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

class CameraSource {
public:
    int Start(StreamMode mode, IOnVEncData *sink) {
        mode_ = mode;
        if (mode == StreamMode::Dual) {
            if (dual_.Init(sink) < 0) {
                dual_.DeInit();
                return -1;
            }
            dual_ready_ = true;
            if (dual_.Start() < 0) {
                Stop();
                return -1;
            }
            started_ = true;
            return 0;
        }

        KdMediaInputConfig config;
        config.video_valid = true;
        config.video_type = KdMediaVideoType::kVideoTypeH264;
        config.venc_width = 1280;
        config.venc_height = 720;
        config.bitrate_kbps = 2500;
        int csi = mode == StreamMode::Front ? 2 : 0;
        config.mirror = mode == StreamMode::Front
                            ? VICAP_MIRROR_VER
                            : VICAP_MIRROR_NONE;
        if (!probe_sensor(csi, config.sensor_type) ||
            media_.Init(config) < 0)
            return -1;
        media_ready_ = true;
        if (media_.CreateVcapVEnc(sink) < 0) {
            Stop();
            return -1;
        }
        venc_ready_ = true;
        if (media_.StartVcapVEnc() < 0) {
            Stop();
            return -1;
        }
        started_ = true;
        return 0;
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

void set_status(const std::string &text, uint32_t color) {
    if (g_status_label == nullptr)
        return;
    lv_label_set_text(g_status_label, text.c_str());
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(color), 0);
}

void update_mode_buttons(StreamMode selected) {
    for (int index = 0; index < static_cast<int>(StreamMode::Count);
         ++index) {
        if (g_mode_buttons[index] == nullptr)
            continue;
        bool active = index == static_cast<int>(selected);
        lv_obj_set_style_bg_color(g_mode_buttons[index],
                                  lv_color_hex(active ? 0x6750A4
                                                      : 0x303B4C), 0);
        lv_obj_set_style_bg_opa(g_mode_buttons[index], LV_OPA_COVER, 0);
    }
}

void close_url_dialog() {
    if (g_url_dialog != nullptr)
        lv_obj_delete(g_url_dialog);
    g_url_dialog = nullptr;
    g_url_input = nullptr;
}

bool valid_rtmp_url(const char *url) {
    if (url == nullptr || std::strncmp(url, "rtmp://", 7) != 0)
        return false;
    const char *path = std::strchr(url + 7, '/');
    return path != nullptr && path[1] != '\0' &&
           std::strchr(path + 1, '/') != nullptr;
}

void keyboard_cb(lv_event_t *event) {
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_CANCEL) {
        close_url_dialog();
        return;
    }
    if (code != LV_EVENT_READY || g_url_input == nullptr)
        return;
    const char *text = lv_textarea_get_text(g_url_input);
    if (!valid_rtmp_url(text)) {
        lv_obj_set_style_border_color(g_url_input,
                                      lv_color_hex(0xFF8A80), 0);
        set_status(LV_SYMBOL_WARNING "  Use rtmp://server/app/stream-key",
                   0xFF8A80);
        return;
    }
    std::string url(text);
    if (!save_text_atomic(kUrlPath, url)) {
        set_status(LV_SYMBOL_WARNING "  Cannot save RTMP address", 0xFF8A80);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_url_mutex);
        g_requested_url = url;
    }
    g_url_changed.store(true);
    if (g_url_label != nullptr)
        lv_label_set_text(g_url_label, url.c_str());
    close_url_dialog();
}

void edit_url_cb(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
        !lv_k230_touch_accept_click() || g_url_dialog != nullptr)
        return;
    lv_obj_t *screen = lv_screen_active();
    g_url_dialog = lv_obj_create(screen);
    lv_obj_set_size(g_url_dialog, 620, 452);
    lv_obj_center(g_url_dialog);
    lv_obj_set_style_radius(g_url_dialog, 24, 0);
    lv_obj_set_style_bg_color(g_url_dialog, lv_color_hex(0x202B3B), 0);
    lv_obj_set_style_border_width(g_url_dialog, 0, 0);
    lv_obj_clear_flag(g_url_dialog, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(g_url_dialog);
    lv_label_set_text(title, "RTMP publishing address");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 2);

    g_url_input = lv_textarea_create(g_url_dialog);
    lv_obj_set_size(g_url_input, 570, 52);
    lv_obj_align(g_url_input, LV_ALIGN_TOP_MID, 0, 42);
    lv_textarea_set_one_line(g_url_input, true);
    lv_textarea_set_max_length(g_url_input, 255);
    const char *current = g_url_label != nullptr
                              ? lv_label_get_text(g_url_label)
                              : kDefaultUrl;
    lv_textarea_set_text(g_url_input, current);
    lv_obj_set_style_text_font(g_url_input, &lv_font_montserrat_16, 0);

    lv_obj_t *hint = lv_label_create(g_url_dialog);
    lv_label_set_text(hint,
                      "Example: rtmp://server/live/stream-key\n"
                      "Press the keyboard check mark to save and reconnect.");
    lv_obj_set_style_text_color(hint, lv_color_hex(0xB8C4D4), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 10, 105);

    lv_obj_t *keyboard = lv_keyboard_create(g_url_dialog);
    lv_obj_set_size(keyboard, 590, 270);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_popovers(keyboard, false);
    lv_keyboard_set_textarea(keyboard, g_url_input);
    lv_obj_add_event_cb(keyboard, keyboard_cb, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(keyboard, keyboard_cb, LV_EVENT_CANCEL, nullptr);
    lv_obj_move_foreground(g_url_dialog);
}

void back_cb(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED &&
        lv_k230_touch_accept_click())
        g_exit.store(true);
}

void mode_cb(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
        !lv_k230_touch_accept_click() || g_url_dialog != nullptr)
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
    lv_label_set_text(title, "RTMP Stream");
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
    lv_label_set_text(g_status_label,
                      LV_SYMBOL_REFRESH "  Connecting to RTMP server...");
    lv_obj_set_style_text_font(g_status_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(0xF6C85F), 0);
    lv_obj_align(g_status_label, LV_ALIGN_TOP_MID, 0, 158);

    lv_obj_t *card = lv_obj_create(screen);
    lv_obj_set_size(card, 570, 125);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 48);
    lv_obj_set_style_radius(card, 22, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x202B3B), 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *hint = lv_label_create(card);
    lv_label_set_text(hint, "RTMP publishing address");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x9CA9BA), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 10, 4);

    g_url_label = lv_label_create(card);
    lv_label_set_text(g_url_label, url.c_str());
    lv_label_set_long_mode(g_url_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_size(g_url_label, 430, 36);
    lv_obj_set_style_text_color(g_url_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_url_label, &lv_font_montserrat_16, 0);
    lv_obj_align(g_url_label, LV_ALIGN_BOTTOM_LEFT, 10, -7);

    lv_obj_t *edit = lv_button_create(card);
    lv_obj_set_size(edit, 94, 50);
    lv_obj_align(edit, LV_ALIGN_BOTTOM_RIGHT, -2, -4);
    lv_obj_set_style_radius(edit, 16, 0);
    lv_obj_set_ext_click_area(edit, 8);
    lv_obj_add_event_cb(edit, edit_url_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *edit_text = lv_label_create(edit);
    lv_label_set_text(edit_text, LV_SYMBOL_EDIT " Edit");
    lv_obj_set_style_text_font(edit_text, &lv_font_montserrat_16, 0);
    lv_obj_center(edit_text);

    lv_obj_t *footer = lv_label_create(screen);
    lv_label_set_text(footer,
                      "H.264 720p  |  Automatic reconnect  |  Video only");
    lv_obj_set_style_text_color(footer, lv_color_hex(0x9CA9BA), 0);
    lv_obj_set_style_text_font(footer, &lv_font_montserrat_14, 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_refr_now(display);
    return display;
}

void destroy_status_ui(lv_display_t *display) {
    g_status_label = nullptr;
    g_url_label = nullptr;
    g_url_dialog = nullptr;
    g_url_input = nullptr;
    for (auto &button : g_mode_buttons)
        button = nullptr;
    if (display != nullptr)
        lv_display_delete(display);
    lv_deinit();
    kd_display_deinit();
}

bool init_status_ui_vb() {
    k_vb_config config{};
    config.max_pool_cnt = 64;
    int result = kd_mpi_vb_set_config(&config);
    if (result == K_SUCCESS)
        result = kd_mpi_vb_init();
    return result == K_SUCCESS;
}
}  // namespace

int main(int argc, char **argv) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGPIPE, SIG_IGN);

    if (!wifi_connected()) {
        std::printf("[rtmp-stream] Wi-Fi is not connected\n");
        return 2;
    }

    StreamMode active_mode = load_mode();
    if (argc > 2) {
        int requested_mode = std::atoi(argv[2]);
        if (requested_mode >= 0 &&
            requested_mode < static_cast<int>(StreamMode::Count))
            active_mode = static_cast<StreamMode>(requested_mode);
    }
    std::string active_url = load_text(kUrlPath, kDefaultUrl);
    if (argc > 1 && std::strncmp(argv[1], "rtmp://", 7) == 0)
        active_url = argv[1];
    g_requested_mode.store(active_mode);
    {
        std::lock_guard<std::mutex> lock(g_url_mutex);
        g_requested_url = active_url;
    }
    std::printf("[rtmp-stream] selected source: %s, target: %s\n",
                mode_name(active_mode), active_url.c_str());

    CameraSource source;
    RtmpPublisher publisher;
    publisher.Start(active_url);
    bool source_started = source.Start(active_mode, &publisher) == 0;
    if (source_started)
        save_mode(active_mode);
    bool status_ui_owns_vb = false;
    if (!source_started) {
        publisher.Stop();
        status_ui_owns_vb = init_status_ui_vb();
        if (!status_ui_owns_vb)
            return 5;
    }

    lv_display_t *display = create_status_ui(active_url, active_mode);
    if (display == nullptr) {
        if (status_ui_owns_vb)
            kd_mpi_vb_exit();
        publisher.Stop();
        source.Stop();
        return 5;
    }

    std::string last_status;
    while (!g_exit.load()) {
        uint32_t delay = lv_timer_handler();
        if (delay == LV_NO_TIMER_READY || delay > 50)
            delay = 20;
        usleep(delay * 1000);

        std::string status;
        uint32_t status_color = 0xF6C85F;
        if (!source_started) {
            status = LV_SYMBOL_WARNING "  Camera source unavailable";
            status_color = 0xFF8A80;
        } else {
            RtmpPublisherState state = publisher.State();
            status = publisher.StatusDetail();
            if (state == RtmpPublisherState::Streaming) {
                status = LV_SYMBOL_WIFI "  Live  |  " +
                         std::string(mode_name(active_mode));
                uint64_t dropped = publisher.DroppedFrames();
                if (dropped != 0)
                    status += "  |  drop " + std::to_string(dropped);
                status_color = 0x67E8A5;
            } else if (state == RtmpPublisherState::Reconnecting) {
                status_color = 0xFFB86B;
            } else if (state == RtmpPublisherState::Stopped) {
                status_color = 0xFF8A80;
            }
        }
        if (status != last_status) {
            set_status(status, status_color);
            last_status = status;
        }

        bool change_url = g_url_changed.exchange(false);
        StreamMode requested = g_requested_mode.load();
        bool change_mode = requested != active_mode;
        if (!change_url && !change_mode)
            continue;

        if (change_url) {
            std::lock_guard<std::mutex> lock(g_url_mutex);
            active_url = g_requested_url;
        }
        if (change_mode)
            active_mode = requested;
        set_status(LV_SYMBOL_REFRESH "  Restarting publisher...", 0xF6C85F);
        lv_refr_now(display);

        destroy_status_ui(display);
        display = nullptr;
        if (status_ui_owns_vb) {
            kd_mpi_vb_exit();
            status_ui_owns_vb = false;
        }
        publisher.Stop();
        source.Stop();
        usleep(120 * 1000);

        publisher.Start(active_url);
        source_started = source.Start(active_mode, &publisher) == 0;
        if (source_started)
            save_mode(active_mode);
        else {
            publisher.Stop();
            status_ui_owns_vb = init_status_ui_vb();
        }
        if (!source_started && !status_ui_owns_vb)
            break;
        display = create_status_ui(active_url, active_mode);
        if (display == nullptr)
            break;
        last_status.clear();
    }

    if (display != nullptr) {
        set_status("Stopping stream...", 0x9CA9BA);
        lv_refr_now(display);
        destroy_status_ui(display);
    }
    if (status_ui_owns_vb)
        kd_mpi_vb_exit();
    publisher.Stop();
    source.Stop();
    std::printf("[rtmp-stream] exited\n");
    return 0;
}
