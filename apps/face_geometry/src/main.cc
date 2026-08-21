#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include <unistd.h>
#include "drv_touch.h"
#include "geometry_studio.h"
#include "../../face_studio/src/mode_persistence.h"
#include "setting.h"
#include "video_pipeline.h"
#include "../../face_studio/src/ios_overlay.h"
#include "../../face_studio/src/startup_spinner.h"
#include "../../face_studio/src/ui_localization.h"
#include "vaxp_ai_stream.h"

static std::atomic<bool> g_stop(false);
static std::atomic<bool> g_menu_open(false);
static std::atomic<bool> g_loading(false);
static GeometryStudio *g_studio;

static const char *localized_mode_name(GeometryMode mode)
{
    switch (mode) {
    case GeometryMode::Pose:
        return dshanpi_ui::localized("Face Pose", "人脸姿态",
                                     "人臉姿態", "顔姿勢");
    case GeometryMode::Mesh:
        return dshanpi_ui::localized("Face Mesh", "人脸网格",
                                     "人臉網格", "顔メッシュ");
    case GeometryMode::Parse:
        return dshanpi_ui::localized("Face Parse", "人脸解析",
                                     "人臉解析", "顔解析");
    case GeometryMode::Alignment:
        return dshanpi_ui::localized("Face Alignment", "人脸对齐",
                                     "人臉對齊", "顔位置合わせ");
    default: return "";
    }
}

static void touch_to_screen(const drv_touch_data &point, int &x, int &y)
{
    x = 639 - static_cast<int>(point.y_coordinate);
    y = 479 - static_cast<int>(point.x_coordinate);
}

static void touch_proc()
{
    drv_touch_inst_t *touch = nullptr;
    if (drv_touch_inst_create(0, &touch) != 0) {
        std::cerr << "[face-geometry] touch initialization failed" << std::endl;
        return;
    }
    auto last_tap = std::chrono::steady_clock::now() -
                    std::chrono::milliseconds(500);
    while (!g_stop.load()) {
        drv_touch_data points[DRV_TOUCH_POINT_NUMBER_MAX];
        const int count =
            drv_touch_read(touch, points, DRV_TOUCH_POINT_NUMBER_MAX);
        if (count <= 0) {
            usleep(10000);
            continue;
        }
        const auto &point = points[0];
        const auto now = std::chrono::steady_clock::now();
        if (point.event == DRV_TOUCH_EVENT_DOWN &&
            now - last_tap >= std::chrono::milliseconds(250)) {
            last_tap = now;
            int x, y;
            touch_to_screen(point, x, y);
            if (y < ios_ui::kBackTouchExtent &&
                x < ios_ui::kBackTouchExtent) {
                g_stop = true;
            } else if (!g_loading.load() && y < 88 && x > 500) {
                g_menu_open = !g_menu_open.load();
            } else if (!g_loading.load() && g_menu_open.load() &&
                       x >= 330 && x <= 630 &&
                       y >= 82 && y < 322) {
                const int index = (y - 82) / 60;
                if (index >= 0 &&
                    index < static_cast<int>(GeometryMode::Count)) {
                    const auto mode = static_cast<GeometryMode>(index);
                    if (mode != g_studio->requested_mode()) {
                        g_studio->request_mode(mode);
                        g_menu_open = false;
                        g_loading = true;
                        dshanpi_mode_state::save(
                            "face_geometry", index,
                            static_cast<int>(GeometryMode::Count));
                    }
                    g_menu_open = false;
                }
            }
        }
    }
    drv_touch_inst_destroy(&touch);
}

static void draw_loading(cv::Mat &osd)
{
    ios_ui::rounded_rect(osd, cv::Rect(170, 190, 300, 100),
                  cv::Scalar(28, 32, 40, 245), cv::FILLED);
    dshanpi_ui::draw_text_centered(
        osd, dshanpi_ui::common_text(dshanpi_ui::CommonText::LoadingModel),
        cv::Rect(170, 190, 300, 100), 22,
        cv::Scalar(255, 255, 255, 255));
}

static void draw_controls(cv::Mat &osd)
{
    ios_ui::rounded_rect(osd, cv::Rect(12, 12, 58, 58),
                  cv::Scalar(40, 40, 40, 220), cv::FILLED);
    cv::putText(osd, "<", cv::Point(32, 50),
                cv::FONT_HERSHEY_SIMPLEX, 0.65,
                cv::Scalar(255, 255, 255, 255), 2);
    dshanpi_ui::draw_mode_header(
        osd, localized_mode_name(g_studio->requested_mode()), 58);
    ios_ui::rounded_rect(osd, cv::Rect(500, 12, 128, 58),
                  cv::Scalar(40, 40, 40, 220), cv::FILLED);
    dshanpi_ui::draw_text_centered(
        osd, dshanpi_ui::common_text(dshanpi_ui::CommonText::Mode),
        cv::Rect(500, 12, 128, 58), 21,
        cv::Scalar(255, 255, 255, 255));
    if (!g_menu_open.load())
        return;
    ios_ui::rounded_rect(osd, cv::Rect(326, 76, 302, 252),
                  cv::Scalar(28, 32, 40, 238), cv::FILLED);
    for (int i = 0; i < static_cast<int>(GeometryMode::Count); ++i) {
        const GeometryMode mode = static_cast<GeometryMode>(i);
        const int top = 82 + i * 60;
        if (mode == g_studio->requested_mode())
            ios_ui::rounded_rect(osd, cv::Rect(334, top, 286, 52),
                          cv::Scalar(103, 58, 183, 245), cv::FILLED);
        dshanpi_ui::draw_text(osd, localized_mode_name(mode),
                              cv::Point(350, top + 36), 21,
                              cv::Scalar(255, 255, 255, 255));
    }
}

int main(int argc, char **argv)
{
    int csi = argc > 1 ? atoi(argv[1]) : 2;
    const int debug = argc > 2 ? atoi(argv[2]) : 0;
    if (csi != 0 && csi != 2)
        csi = 2;
    if (chdir("/sdcard/app/face_geometry") != 0) {
        std::cerr << "[face-geometry] cannot enter resource directory"
                  << std::endl;
        return 1;
    }
    std::cout << "[face-geometry] session running" << std::endl;
    const dshanpi_vaxp_ai_config_t vaxp_config = {
        "Face Geometry", static_cast<uint8_t>(csi),
        AI_FRAME_WIDTH, AI_FRAME_HEIGHT,
        VAXP_CAP_FACE_DETECT | VAXP_CAP_POSE |
            VAXP_CAP_SEGMENTATION | VAXP_CAP_MULTI_MODEL};
    if (dshanpi_vaxp_ai_start(&vaxp_config) != 0)
        std::cerr << "[face-geometry] UART2 VAXP output unavailable"
                  << std::endl;
    {
        FrameCHWSize frame_size = {
            AI_FRAME_CHANNEL, AI_FRAME_HEIGHT, AI_FRAME_WIDTH
        };
        const auto initial_mode = static_cast<GeometryMode>(
            dshanpi_mode_state::load(
                "face_geometry", static_cast<int>(GeometryMode::Pose),
                static_cast<int>(GeometryMode::Count)));
        std::unique_ptr<GeometryStudio> studio;
        std::atomic<bool> model_ready(false);
        std::thread model_thread([&]() {
            studio.reset(new GeometryStudio(
                "models", frame_size, debug, initial_mode));
            model_ready = true;
        });
        CameraManager camera(debug, csi);
        if (camera.Create() != 0) {
            std::cerr << "[face-geometry] camera session creation failed"
                      << std::endl;
            return 1;
        }
        cv::Mat osd(OSD_HEIGHT, OSD_WIDTH, CV_8UC4,
                    cv::Scalar(0, 0, 0, 0));
        dshanpi_ui::show_model_loading_until_ready(
            camera, osd, model_ready, localized_mode_name(initial_mode));
        model_thread.join();
        g_studio = studio.get();
        std::thread touch_thread(touch_proc);
        dims_t shape{1, AI_FRAME_CHANNEL, AI_FRAME_HEIGHT, AI_FRAME_WIDTH};
        runtime_tensor input = host_runtime_tensor::create(
            typecode_t::dt_uint8, shape, hrt::pool_shared)
            .expect("face geometry rotated tensor");
        auto input_buf = input.impl()->to_host().unwrap()->buffer()
            .as_host().unwrap().map(map_access_::map_write).unwrap().buffer();
        const size_t plane = AI_FRAME_WIDTH * AI_FRAME_HEIGHT;
        while (!g_stop.load()) {
            DumpRes frame;
            if (camera.GetFrame(frame) != 0) {
                usleep(10000);
                continue;
            }
            if (g_loading.load()) {
                const GeometryMode next = studio->requested_mode();
                camera.ReleaseFrame(frame);
                dshanpi_ui::load_model_with_feedback(
                    camera, osd, localized_mode_name(next),
                    [&]() { studio->apply_requested_mode(); },
                    [&](cv::Mat &loading_osd) {
                        draw_controls(loading_osd);
                    });
                g_loading = false;
                continue;
            }
            const auto *src =
                reinterpret_cast<const uint8_t *>(frame.virt_addr);
            auto *dst = reinterpret_cast<uint8_t *>(input_buf.data());
            for (int channel = 0; channel < AI_FRAME_CHANNEL; ++channel) {
                std::reverse_copy(src + channel * plane,
                                  src + (channel + 1) * plane,
                                  dst + channel * plane);
            }
            hrt::sync(input, sync_op_t::sync_write_back, true)
                .expect("face geometry tensor sync");
            studio->process(input, osd);
            draw_controls(osd);
            camera.InsertFrame(osd.data);
            camera.ReleaseFrame(frame);
        }
        touch_thread.join();
        g_studio = nullptr;
        dshanpi_vaxp_ai_stop();
        camera.Destroy();
    }
    usleep(200000);
    std::cout << "[face-geometry] resources released; returning to supervisor"
              << std::endl;
    return 0;
}
