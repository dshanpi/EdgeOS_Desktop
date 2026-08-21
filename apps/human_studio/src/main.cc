#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <unistd.h>
#include "drv_touch.h"
#include "human_studio.h"
#include "../../face_studio/src/mode_persistence.h"
#include "setting.h"
#include "video_pipeline.h"
#include "../../face_studio/src/ios_overlay.h"
#include "../../face_studio/src/startup_spinner.h"
#include "../../face_studio/src/ui_localization.h"
#include "vaxp_ai_stream.h"

static std::atomic<bool> g_stop(false), g_menu(false), g_loading(false);
static HumanStudio *g_studio;

static const char *localized_mode_name(HumanStudioMode mode) {
    switch (mode) {
    case HumanStudioMode::Detection:
        return dshanpi_ui::localized("Person Detection", "人体检测",
                                     "人體偵測", "人物検出");
    case HumanStudioMode::Pose:
        return dshanpi_ui::localized("Body Pose", "人体姿态",
                                     "人體姿態", "人体姿勢");
    case HumanStudioMode::Fitness:
        return dshanpi_ui::localized("Fitness Counter", "健身计数",
                                     "健身計數", "運動カウンター");
    case HumanStudioMode::FallSafety:
        return dshanpi_ui::localized("Fall Safety", "跌倒检测",
                                     "跌倒偵測", "転倒検出");
    default: return "";
    }
}

static void touch_proc() {
    drv_touch_inst_t *touch = nullptr;
    if (drv_touch_inst_create(0, &touch) != 0) return;
    auto last = std::chrono::steady_clock::now() - std::chrono::milliseconds(500);
    while (!g_stop.load()) {
        drv_touch_data p[DRV_TOUCH_POINT_NUMBER_MAX];
        int count = drv_touch_read(touch, p, DRV_TOUCH_POINT_NUMBER_MAX);
        if (count <= 0) { usleep(10000); continue; }
        auto now = std::chrono::steady_clock::now();
        if (p[0].event != DRV_TOUCH_EVENT_DOWN ||
            now - last < std::chrono::milliseconds(250)) continue;
        last = now;
        int x = 639 - static_cast<int>(p[0].y_coordinate);
        int y = 479 - static_cast<int>(p[0].x_coordinate);
        if (y < ios_ui::kBackTouchExtent &&
            x < ios_ui::kBackTouchExtent) g_stop = true;
        else if (!g_loading.load() && y < 88 && x > 500)
            g_menu = !g_menu.load();
        else if (!g_loading.load() && g_menu.load() &&
                 x >= 330 && x <= 630 && y >= 82 && y < 322) {
            int index = (y - 82) / 60;
            if (index >= 0 && index < static_cast<int>(HumanStudioMode::Count)) {
                HumanStudioMode mode = static_cast<HumanStudioMode>(index);
                if (mode != g_studio->requested_mode()) {
                    g_studio->request_mode(mode);
                    g_menu = false;
                    g_loading = true;
                    dshanpi_mode_state::save(
                        "human_studio", index,
                        static_cast<int>(HumanStudioMode::Count));
                }
                g_menu = false;
            }
        }
    }
    drv_touch_inst_destroy(&touch);
}

static void draw_controls(cv::Mat &osd) {
    ios_ui::rounded_rect(osd, cv::Rect(12, 12, 58, 58),
                  cv::Scalar(40, 40, 40, 220), cv::FILLED);
    cv::putText(osd, "<", cv::Point(32, 50),
                cv::FONT_HERSHEY_SIMPLEX, .65,
                cv::Scalar(255, 255, 255, 255), 2);
    dshanpi_ui::draw_mode_header(
        osd, localized_mode_name(g_studio->requested_mode()), 58);
    ios_ui::rounded_rect(osd, cv::Rect(500, 12, 128, 58),
                  cv::Scalar(40, 40, 40, 220), cv::FILLED);
    dshanpi_ui::draw_text_centered(
        osd, dshanpi_ui::common_text(dshanpi_ui::CommonText::Mode),
        cv::Rect(500, 12, 128, 58), 21,
        cv::Scalar(255, 255, 255, 255));
    if (!g_menu.load()) return;
    ios_ui::rounded_rect(osd, cv::Rect(326, 76, 302, 252),
                  cv::Scalar(28, 32, 40, 238), cv::FILLED);
    for (int i = 0; i < static_cast<int>(HumanStudioMode::Count); ++i) {
        HumanStudioMode mode = static_cast<HumanStudioMode>(i);
        int top = 82 + i * 60;
        if (mode == g_studio->requested_mode())
            ios_ui::rounded_rect(osd, cv::Rect(334, top, 286, 52),
                          ios_ui::accent(), cv::FILLED);
        dshanpi_ui::draw_text(osd, localized_mode_name(mode),
                              cv::Point(350, top + 36), 21,
                              cv::Scalar(255, 255, 255, 255));
    }
}

static void draw_loading(cv::Mat &osd) {
    ios_ui::rounded_rect(osd, cv::Rect(170, 190, 300, 100),
                  cv::Scalar(28, 32, 40, 245), cv::FILLED);
    dshanpi_ui::draw_text_centered(
        osd, dshanpi_ui::common_text(dshanpi_ui::CommonText::LoadingModel),
        cv::Rect(170, 190, 300, 100), 22,
        cv::Scalar(255, 255, 255, 255));
}

int main(int argc, char **argv) {
    int csi = argc > 1 ? atoi(argv[1]) : 2;
    int debug = argc > 2 ? atoi(argv[2]) : 0;
    if (csi != 0 && csi != 2) csi = 2;
    const dshanpi_vaxp_ai_config_t vaxp_config = {
        "Human Studio", static_cast<uint8_t>(csi),
        AI_FRAME_WIDTH, AI_FRAME_HEIGHT,
        VAXP_CAP_DETECTION | VAXP_CAP_POSE | VAXP_CAP_MULTI_MODEL};
    if (dshanpi_vaxp_ai_start(&vaxp_config) != 0) {
        std::cerr << "[human-studio] UART2 VAXP output unavailable"
                  << std::endl;
    } else {
        static const char *const person_labels[] = {"person"};
        static const char *const fall_labels[] = {"Fall", "NoFall"};
        dshanpi_vaxp_ai_register_classes(0x0401, person_labels, 1);
        dshanpi_vaxp_ai_register_classes(0x0404, fall_labels, 2);
    }
    FrameCHWSize size{AI_FRAME_CHANNEL, AI_FRAME_HEIGHT, AI_FRAME_WIDTH};
    const auto initial_mode = static_cast<HumanStudioMode>(
        dshanpi_mode_state::load(
            "human_studio", static_cast<int>(HumanStudioMode::Detection),
            static_cast<int>(HumanStudioMode::Count)));
    std::unique_ptr<HumanStudio> studio;
    std::atomic<bool> model_ready(false);
    std::thread model_thread([&]() {
        studio.reset(new HumanStudio(
            "/sdcard/app/human_studio/models", size, debug, initial_mode));
        model_ready = true;
    });
    CameraManager camera(debug, csi);
    if (camera.Create() != 0) return 1;
    cv::Mat osd(OSD_HEIGHT, OSD_WIDTH, CV_8UC4, cv::Scalar(0, 0, 0, 0));
    dshanpi_ui::show_model_loading_until_ready(
        camera, osd, model_ready, localized_mode_name(initial_mode));
    model_thread.join();
    g_studio = studio.get();
    std::thread touch(touch_proc);
    dims_t shape{1, AI_FRAME_CHANNEL, AI_FRAME_HEIGHT, AI_FRAME_WIDTH};
    runtime_tensor input = host_runtime_tensor::create(
        typecode_t::dt_uint8, shape, hrt::pool_shared).expect("human tensor");
    auto buf = input.impl()->to_host().unwrap()->buffer().as_host().unwrap()
        .map(map_access_::map_write).unwrap().buffer();
    const size_t plane = AI_FRAME_WIDTH * AI_FRAME_HEIGHT;
    while (!g_stop.load()) {
        DumpRes frame;
        if (camera.GetFrame(frame) != 0) { usleep(10000); continue; }
        if (g_loading.load()) {
            const HumanStudioMode next = studio->requested_mode();
            camera.ReleaseFrame(frame);
            dshanpi_ui::load_model_with_feedback(
                camera, osd, localized_mode_name(next),
                [&]() { studio->apply_requested_mode(); },
                [&](cv::Mat &loading_osd) { draw_controls(loading_osd); });
            g_loading = false;
            continue;
        }
        const uint8_t *src = reinterpret_cast<const uint8_t *>(frame.virt_addr);
        uint8_t *dst = reinterpret_cast<uint8_t *>(buf.data());
        for (int c = 0; c < AI_FRAME_CHANNEL; ++c)
            std::reverse_copy(src + c * plane, src + (c + 1) * plane,
                              dst + c * plane);
        hrt::sync(input, sync_op_t::sync_write_back, true).expect("human sync");
        studio->process(input, osd);
        draw_controls(osd);
        camera.InsertFrame(osd.data);
        camera.ReleaseFrame(frame);
    }
    touch.join();
    g_studio = nullptr;
    dshanpi_vaxp_ai_stop();
    camera.Destroy();
    usleep(200000);
    return 0;
}
