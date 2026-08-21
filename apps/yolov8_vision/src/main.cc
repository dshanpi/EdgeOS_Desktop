#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <unistd.h>
#include "drv_touch.h"
#include "model_runner.h"
#include "setting.h"
#include "video_pipeline.h"
#include "../../face_studio/src/ios_overlay.h"
#include "../../face_studio/src/mode_persistence.h"
#include "../../face_studio/src/startup_spinner.h"
#include "../../face_studio/src/ui_localization.h"
#include "ai_label_tables.h"
#include "vaxp_ai_stream.h"

enum class Mode { Detection = 0, Segmentation, Count };
static const char *mode_name(Mode mode) {
    return mode == Mode::Detection ? "Object Detection" : "Instance Segmentation";
}
static const char *localized_mode_name(Mode mode) {
    return mode == Mode::Detection
        ? dshanpi_ui::localized("Object Detection", "目标检测",
                                "目標偵測", "物体検出")
        : dshanpi_ui::localized("Instance Segmentation", "实例分割",
                                "實例分割", "インスタンス分割");
}
static std::atomic<bool> g_stop(false), g_menu(false), g_switch(false);
static std::atomic<Mode> g_requested(Mode::Detection);
static Mode g_active = Mode::Detection;

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
        else if (!g_switch.load() && y < 88 && x > 500)
            g_menu = !g_menu.load();
        else if (!g_switch.load() && g_menu.load() &&
                 x >= 330 && x <= 630 &&
                 y >= 82 && y < 202) {
            Mode mode = static_cast<Mode>((y - 82) / 60);
            if (mode != g_requested.load()) {
                g_requested = mode;
                g_menu = false;
                g_switch = true;
                dshanpi_mode_state::save(
                    "yolov8_vision", static_cast<int>(mode),
                    static_cast<int>(Mode::Count));
            }
            g_menu = false;
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
        osd, localized_mode_name(g_requested.load()), 58);
    ios_ui::rounded_rect(osd, cv::Rect(500, 12, 128, 58),
                  cv::Scalar(40, 40, 40, 220), cv::FILLED);
    dshanpi_ui::draw_text_centered(
        osd, dshanpi_ui::common_text(dshanpi_ui::CommonText::Mode),
        cv::Rect(500, 12, 128, 58), 21,
        cv::Scalar(255, 255, 255, 255));
    if (!g_menu.load()) return;
    ios_ui::rounded_rect(osd, cv::Rect(326, 76, 302, 132),
                  cv::Scalar(28, 32, 40, 238), cv::FILLED);
    for (int i = 0; i < static_cast<int>(Mode::Count); ++i) {
        int top = 82 + i * 60;
        if (static_cast<Mode>(i) == g_requested.load())
            ios_ui::rounded_rect(osd, cv::Rect(334, top, 286, 52),
                          ios_ui::accent(), cv::FILLED);
        dshanpi_ui::draw_text(osd,
                              localized_mode_name(static_cast<Mode>(i)),
                              cv::Point(350, top + 36), 20,
                              cv::Scalar(255, 255, 255, 255));
    }
}

static std::unique_ptr<ModelRunner> load_runner(Mode mode,
                                                FrameCHWSize size, int debug) {
    const std::string dir = "/sdcard/app/yolov8_vision/models/";
    if (mode == Mode::Segmentation)
        return create_segmentation_runner(dir + "yolov8n_seg_320.kmodel",
                                          size, debug);
    return create_detection_runner(dir + "yolov8n_320.kmodel", size, debug);
}

int main(int argc, char **argv) {
    int csi = argc > 1 ? atoi(argv[1]) : 2;
    int debug = argc > 2 ? atoi(argv[2]) : 0;
    if (csi != 0 && csi != 2) csi = 2;
    const dshanpi_vaxp_ai_config_t vaxp_config = {
        "Object Detection", static_cast<uint8_t>(csi),
        AI_FRAME_WIDTH, AI_FRAME_HEIGHT,
        VAXP_CAP_DETECTION | VAXP_CAP_SEGMENTATION |
            VAXP_CAP_MULTI_MODEL};
    if (dshanpi_vaxp_ai_start(&vaxp_config) != 0) {
        std::cerr << "[yolov8-vision] UART2 VAXP output unavailable"
                  << std::endl;
    } else {
        dshanpi_vaxp_ai_register_classes(
            0x0701, dshanpi_coco80_labels, DSHANPI_COCO80_COUNT);
        dshanpi_vaxp_ai_register_classes(
            0x0702, dshanpi_coco80_labels, DSHANPI_COCO80_COUNT);
    }
    g_active = static_cast<Mode>(dshanpi_mode_state::load(
        "yolov8_vision", static_cast<int>(Mode::Detection),
        static_cast<int>(Mode::Count)));
    g_requested = g_active;
    FrameCHWSize size{AI_FRAME_CHANNEL, AI_FRAME_HEIGHT, AI_FRAME_WIDTH};
    std::unique_ptr<ModelRunner> runner;
    std::atomic<bool> model_ready(false);
    std::thread model_thread([&]() {
        runner = load_runner(g_active, size, debug);
        std::cout << "[yolov8-vision] loaded initial model: "
                  << mode_name(g_active) << std::endl;
        model_ready = true;
    });
    CameraManager camera(debug, csi);
    if (camera.Create() != 0) return 1;
    cv::Mat osd(OSD_HEIGHT, OSD_WIDTH, CV_8UC4, cv::Scalar(0, 0, 0, 0));
    dshanpi_ui::show_model_loading_until_ready(
        camera, osd, model_ready, localized_mode_name(g_active));
    model_thread.join();
    std::thread touch(touch_proc);
    dims_t shape{1, AI_FRAME_CHANNEL, AI_FRAME_HEIGHT, AI_FRAME_WIDTH};
    runtime_tensor input = host_runtime_tensor::create(
        typecode_t::dt_uint8, shape, hrt::pool_shared).expect("yolov8 tensor");
    auto buf = input.impl()->to_host().unwrap()->buffer().as_host().unwrap()
        .map(map_access_::map_write).unwrap().buffer();
    const size_t plane = AI_FRAME_WIDTH * AI_FRAME_HEIGHT;
    while (!g_stop.load()) {
        DumpRes frame;
        if (camera.GetFrame(frame) != 0) { usleep(10000); continue; }
        if (g_switch.load()) {
            const Mode next = g_requested.load();
            camera.ReleaseFrame(frame);
            dshanpi_ui::load_model_with_feedback(
                camera, osd, localized_mode_name(next),
                [&]() {
                    runner.reset();
                    runner = load_runner(next, size, debug);
                },
                [&](cv::Mat &loading_osd) { draw_controls(loading_osd); });
            g_active = next;
            g_switch = false;
            std::cout << "[yolov8-vision] switched mode: "
                      << mode_name(g_active) << std::endl;
            continue;
        }
        const uint8_t *src = reinterpret_cast<const uint8_t *>(frame.virt_addr);
        uint8_t *dst = reinterpret_cast<uint8_t *>(buf.data());
        for (int c = 0; c < AI_FRAME_CHANNEL; ++c)
            std::reverse_copy(src + c * plane, src + (c + 1) * plane,
                              dst + c * plane);
        hrt::sync(input, sync_op_t::sync_write_back, true).expect("sync");
        osd.setTo(cv::Scalar(0, 0, 0, 0));
        runner->process(input, osd);
        draw_controls(osd);
        camera.InsertFrame(osd.data);
        camera.ReleaseFrame(frame);
    }
    touch.join();
    runner.reset();
    dshanpi_vaxp_ai_stop();
    camera.Destroy();
    usleep(200000);
    return 0;
}
