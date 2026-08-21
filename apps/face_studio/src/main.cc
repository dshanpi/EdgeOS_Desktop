#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <unistd.h>

#include "drv_touch.h"
#include "face_studio.h"
#include "mode_persistence.h"
#include "setting.h"
#include "startup_spinner.h"
#include "ui_localization.h"
#include "video_pipeline.h"
#include "ios_overlay.h"
#include "vaxp_ai_stream.h"

static std::atomic<bool> g_stop(false);
static std::atomic<bool> g_menu_open(false);
static std::atomic<bool> g_loading(false);
static FaceStudio *g_studio;

static const char *localized_mode_name(FaceStudioMode mode)
{
    switch (mode) {
    case FaceStudioMode::Detection:
        return dshanpi_ui::localized("Face Detection", "人脸检测",
                                     "人臉偵測", "顔検出");
    case FaceStudioMode::Emotion:
        return dshanpi_ui::localized("Emotion", "表情识别",
                                     "表情辨識", "表情認識");
    case FaceStudioMode::Gender:
        return dshanpi_ui::localized("Gender", "性别识别",
                                     "性別辨識", "性別認識");
    case FaceStudioMode::Glasses:
        return dshanpi_ui::localized("Glasses", "眼镜检测",
                                     "眼鏡偵測", "メガネ検出");
    case FaceStudioMode::Mask:
        return dshanpi_ui::localized("Mask", "口罩检测",
                                     "口罩偵測", "マスク検出");
    case FaceStudioMode::EyeGaze:
        return dshanpi_ui::localized("Eye Gaze", "视线估计",
                                     "視線估計", "視線推定");
    default: return "";
    }
}
/*
 * DongshanPI touch is mounted in native 480x640 orientation while the UI is
 * landscape. This is the same final transform used by the launcher binding.
 */
static void touch_to_screen(const drv_touch_data &point, int &x, int &y)
{
    x = 639 - static_cast<int>(point.y_coordinate);
    y = 479 - static_cast<int>(point.x_coordinate);
}

static void touch_proc()
{
    drv_touch_inst_t *touch = nullptr;
    if (drv_touch_inst_create(0, &touch) != 0) {
        std::cerr << "[face-studio] touch initialization failed" << std::endl;
        return;
    }

    auto last_tap = std::chrono::steady_clock::now() -
                    std::chrono::milliseconds(500);
    while (!g_stop.load()) {
        drv_touch_data points[DRV_TOUCH_POINT_NUMBER_MAX];
        int count = drv_touch_read(touch, points, DRV_TOUCH_POINT_NUMBER_MAX);
        if (count <= 0) {
            usleep(10000);
            continue;
        }
        const auto &point = points[0];
        const auto now = std::chrono::steady_clock::now();
        if (point.event == DRV_TOUCH_EVENT_DOWN &&
            now - last_tap >= std::chrono::milliseconds(250)) {
            last_tap = now;
            int x;
            int y;
            touch_to_screen(point, x, y);
            std::cout << "[face-studio-touch] point=(" << x << "," << y
                      << ")" << std::endl;
            if (y < ios_ui::kBackTouchExtent &&
                x < ios_ui::kBackTouchExtent) {
                g_stop = true;
            } else if (!g_loading.load() && y < 88 && x > 500) {
                g_menu_open = !g_menu_open.load();
            } else if (!g_loading.load() && g_menu_open.load() &&
                       x >= 330 && x <= 630 &&
                       y >= 82 && y < 442) {
                int index = (y - 82) / 60;
                if (index >= 0 &&
                    index < static_cast<int>(FaceStudioMode::Count)) {
                    const auto mode = static_cast<FaceStudioMode>(index);
                    if (mode != g_studio->requested_mode()) {
                        g_studio->request_mode(mode);
                        g_menu_open = false;
                        g_loading = true;
                        dshanpi_mode_state::save(
                            "face_studio", index,
                            static_cast<int>(FaceStudioMode::Count));
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

    if (!g_menu_open.load()) {
        return;
    }
    ios_ui::rounded_rect(osd, cv::Rect(326, 76, 302, 372),
                  cv::Scalar(28, 32, 40, 238), cv::FILLED);
    for (int i = 0; i < static_cast<int>(FaceStudioMode::Count); ++i) {
        FaceStudioMode mode = static_cast<FaceStudioMode>(i);
        int top = 82 + i * 60;
        if (mode == g_studio->requested_mode()) {
            ios_ui::rounded_rect(osd, cv::Rect(334, top, 286, 52),
                          ios_ui::accent(), cv::FILLED);
        }
        dshanpi_ui::draw_text(osd, localized_mode_name(mode),
                              cv::Point(350, top + 36), 21,
                              cv::Scalar(255, 255, 255, 255));
    }
}

int main(int argc, char **argv)
{
    const auto startup_begin = std::chrono::steady_clock::now();
    int csi = argc > 1 ? atoi(argv[1]) : 2;
    int debug = argc > 2 ? atoi(argv[2]) : 0;
    if (csi != 0 && csi != 2) {
        csi = 2;
    }
    std::cout << "[face-studio] session running" << std::endl;

    const dshanpi_vaxp_ai_config_t vaxp_config = {
        "Face Studio", static_cast<uint8_t>(csi),
        AI_FRAME_WIDTH, AI_FRAME_HEIGHT,
        VAXP_CAP_FACE_DETECT | VAXP_CAP_FACE_RECOGNIZE |
            VAXP_CAP_MULTI_MODEL};
    if (dshanpi_vaxp_ai_start(&vaxp_config) != 0)
        std::cerr << "[face-studio] UART2 VAXP output unavailable"
                  << std::endl;

    /*
     * Keep every camera, tensor and nncase object inside this scope. Starting
     * the launcher before these destructors have run races its VB/VO setup
     * against the old Session and leaves the panel black.
     */
    {
        FrameCHWSize frame_size = {
            AI_FRAME_CHANNEL, AI_FRAME_HEIGHT, AI_FRAME_WIDTH
        };
        const auto initial_mode = static_cast<FaceStudioMode>(
            dshanpi_mode_state::load(
                "face_studio", static_cast<int>(FaceStudioMode::Detection),
                static_cast<int>(FaceStudioMode::Count)));
        std::unique_ptr<FaceStudio> studio;
        std::atomic<bool> model_ready(false);
        std::thread model_thread([&]() {
            studio.reset(new FaceStudio(
                "/sdcard/app/face_studio/models", frame_size, debug,
                initial_mode));
            model_ready = true;
        });
        CameraManager camera(debug, csi);
        if (camera.Create() != 0) {
            std::cerr << "[face-studio] camera session creation failed"
                      << std::endl;
            return 1;
        }

        cv::Mat osd(OSD_HEIGHT, OSD_WIDTH, CV_8UC4,
                    cv::Scalar(0, 0, 0, 0));
        /* Keep the live preview visible and identify the restored model while
         * its nncase objects finish loading on the parallel worker. */
        dshanpi_ui::show_model_loading_until_ready(
            camera, osd, model_ready, localized_mode_name(initial_mode));
        std::cout << "[face-studio-startup] preview visible after "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - startup_begin)
                         .count()
                  << " ms" << std::endl;
        model_thread.join();
        std::cout << "[face-studio-startup] model ready after "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - startup_begin)
                         .count()
                  << " ms" << std::endl;
        g_studio = studio.get();
        std::thread touch_thread(touch_proc);
        dims_t shape{1, AI_FRAME_CHANNEL, AI_FRAME_HEIGHT, AI_FRAME_WIDTH};
        runtime_tensor input = host_runtime_tensor::create(
            typecode_t::dt_uint8, shape, hrt::pool_shared)
            .expect("face studio rotated tensor");
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
                const FaceStudioMode next = studio->requested_mode();
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
                .expect("face studio tensor sync");
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

    /* The launcher is the persistent parent process. It is blocked in
     * waitpid() and will rebuild LVGL after this child has fully exited. */
    usleep(200000);
    std::cout << "[face-studio] resources released; returning to supervisor"
              << std::endl;
    /*
     * Return normally. RT-Smart's _exit path forcibly reclaimed the C++
     * process heap and tripped rt_memheap_free before nncase/static runtime
     * destructors had released their allocations.
     */
    return 0;
}
