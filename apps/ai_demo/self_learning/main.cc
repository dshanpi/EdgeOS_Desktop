#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>

#include "drv_touch.h"
#include "nanotrack_crop.h"
#include "nanotrack_src.h"
#include "nanotrack_tracker.h"
#include "self_learning.h"
#include "video_pipeline.h"
#include "vaxp_ai_stream.h"
#include "../../face_studio/src/ios_overlay.h"
#include "../../face_studio/src/startup_spinner.h"
#include "../../face_studio/src/ui_localization.h"

namespace {
constexpr uint16_t kLearningPipelineId = 0x0A01;
constexpr uint16_t kLearningModelId = 0x0A01;
std::atomic<bool> g_stop(false);
std::atomic<bool> g_dragging(false);
std::atomic<bool> g_learn_pending(false);
std::atomic<bool> g_clear_pending(false);
std::atomic<bool> g_has_roi(false);
std::atomic<bool> g_invalid_roi(false);
std::atomic<int> g_x0(0), g_y0(0), g_x1(0), g_y1(0);

void touch_to_screen(const drv_touch_data &p, int &x, int &y)
{
    x = 639 - static_cast<int>(p.y_coordinate);
    y = 479 - static_cast<int>(p.x_coordinate);
    x = std::max(0, std::min(639, x));
    y = std::max(0, std::min(479, y));
}

void touch_proc()
{
    drv_touch_inst_t *touch = nullptr;
    if (drv_touch_inst_create(0, &touch) != 0) {
        std::cerr << "[self-learning] touch initialization failed\n";
        return;
    }
    bool active = false;
    int start_x = 0, start_y = 0;
    int last_x = 0, last_y = 0;
    int min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    auto last_sample = std::chrono::steady_clock::now();
    const auto gesture_idle = std::chrono::milliseconds(180);
    auto finish_selection = [&]() {
        g_x0 = min_x;
        g_y0 = min_y;
        g_x1 = max_x;
        g_y1 = max_y;
        active = false;
        g_dragging = false;
        const int box_width = max_x - min_x;
        const int box_height = max_y - min_y;
        std::cout << "[self-learning-touch] box=(" << min_x << ","
                  << min_y << ")->(" << max_x << "," << max_y
                  << "), span=" << box_width << "x" << box_height
                  << std::endl;
        if (box_width < 24 || box_height < 24) {
            g_has_roi = false;
            g_invalid_roi = true;
        } else {
            g_learn_pending = true;
        }
    };
    while (!g_stop.load()) {
        drv_touch_data points[DRV_TOUCH_POINT_NUMBER_MAX];
        int count = drv_touch_read(touch, points, DRV_TOUCH_POINT_NUMBER_MAX);
        const auto now = std::chrono::steady_clock::now();
        if (count <= 0) {
            if (active && now - last_sample >= gesture_idle)
                finish_selection();
            usleep(8000);
            continue;
        }
        int x, y;
        touch_to_screen(points[0], x, y);
        if (points[0].event == DRV_TOUCH_EVENT_NONE) continue;

        /* This touch stack emits a DOWN/UP pair for nearly every coordinate
         * while a finger slides. Treat samples close in time as one gesture
         * instead of completing a zero-sized box on every UP packet. */
        if (active && now - last_sample >= gesture_idle)
            finish_selection();

        if (!active) {
            if (y < ios_ui::kBackTouchExtent &&
                x < ios_ui::kBackTouchExtent) {
                g_stop = true;
                continue;
            }
            if (y < 72 && x > 510) { g_clear_pending = true; continue; }
            start_x = x;
            start_y = y;
            last_x = x;
            last_y = y;
            min_x = max_x = x;
            min_y = max_y = y;
            g_x0 = g_x1 = x;
            g_y0 = g_y1 = y;
            g_has_roi = true;
            active = true;
            g_dragging = true;
        } else {
            g_x1 = x;
            g_y1 = y;
            last_x = x;
            last_y = y;
            min_x = std::min(min_x, x);
            min_y = std::min(min_y, y);
            max_x = std::max(max_x, x);
            max_y = std::max(max_y, y);
        }
        last_sample = now;
    }
    drv_touch_inst_destroy(&touch);
}

cv::Rect selected_roi()
{
    if (!g_has_roi.load()) return cv::Rect();
    int left = std::min(g_x0.load(), g_x1.load());
    int top = std::min(g_y0.load(), g_y1.load());
    int right = std::max(g_x0.load(), g_x1.load());
    int bottom = std::max(g_y0.load(), g_y1.load());
    return cv::Rect(left, top, std::max(1, right - left),
                    std::max(1, bottom - top));
}

uint16_t learning_class_id(const std::string &label)
{
    unsigned int one_based = 0;
    if (std::sscanf(label.c_str(), "Class %u", &one_based) == 1 &&
        one_based > 0 && one_based <= UINT16_MAX)
        return static_cast<uint16_t>(one_based - 1);
    return 0;
}

void publish_learning_result(const cv::Rect &roi,
                             const std::vector<ClassResult> &results,
                             float tracking_score)
{
    if (roi.width <= 0 || roi.height <= 0 || results.empty()) {
        dshanpi_vaxp_ai_publish_detections(
            kLearningPipelineId, kLearningModelId, VAXP_TASK_TRACKING,
            "AI Learning", 0, nullptr, 0);
        return;
    }
    char metrics[48];
    std::snprintf(metrics, sizeof(metrics), "tracking_score=%.4f",
                  tracking_score);
    dshanpi_vaxp_ai_detection_t object{
        learning_class_id(results[0].res), 1, results[0].score,
        static_cast<float>(roi.x), static_cast<float>(roi.y),
        static_cast<float>(roi.width), static_cast<float>(roi.height),
        0, results[0].res.c_str(), metrics};
    dshanpi_vaxp_ai_publish_detections(
        kLearningPipelineId, kLearningModelId, VAXP_TASK_TRACKING,
        "AI Learning", 0, &object, 1);
}

void draw_ui(cv::Mat &osd, const cv::Rect &roi, size_t class_count,
             const std::vector<ClassResult> &results, const char *message)
{
    ios_ui::rounded_rect(osd, cv::Rect(12, 12, 52, 52),
                  cv::Scalar(40, 40, 40, 230), cv::FILLED);
    cv::putText(osd, "<", cv::Point(29, 46),
                cv::FONT_HERSHEY_SIMPLEX, .62,
                cv::Scalar(255, 255, 255, 255), 2);
    dshanpi_ui::draw_mode_header(
        osd,
        dshanpi_ui::localized("AI Learning", "AI 学习",
                              "AI 學習", "AI学習"),
        52);
    ios_ui::rounded_rect(osd, cv::Rect(510, 12, 118, 52),
                  cv::Scalar(40, 40, 40, 230), cv::FILLED);
    dshanpi_ui::draw_text_centered(
        osd,
        dshanpi_ui::localized("Clear", "清除", "清除", "消去"),
        cv::Rect(510, 12, 118, 52), 20,
        cv::Scalar(255, 255, 255, 255));
    if (roi.width > 0 && roi.height > 0)
        ios_ui::rounded_rect(osd, roi, g_dragging.load()
                      ? cv::Scalar(0, 255, 255, 255)
                      : cv::Scalar(0, 255, 0, 255), 3);

    char info[96];
    if (!results.empty())
        snprintf(info, sizeof(info), "%s  %.1f%%", results[0].res.c_str(),
                 results[0].score * 100.0f);
    else
        snprintf(info, sizeof(info), "Classes: %u", (unsigned)class_count);
    ios_ui::rounded_rect(osd, cv::Rect(12, 408, 616, 58),
                  cv::Scalar(28, 32, 40, 220), cv::FILLED);
    cv::putText(osd, message && message[0] ? message : info,
                cv::Point(28, 446), cv::FONT_HERSHEY_SIMPLEX, .65,
                cv::Scalar(255, 255, 255, 255), 2);
}
} // namespace

int main(int argc, char **argv)
{
    const char *model = argc > 1 ? argv[1] : "recognition.kmodel";
    const float threshold = argc > 2 ? atof(argv[2]) : .5f;
    const int topk = argc > 3 ? atoi(argv[3]) : 1;
    const int debug = argc > 4 ? atoi(argv[4]) : 0;

    dshanpi_vaxp_ai_config_t vaxp_config{
        "AI Learning", 0, AI_FRAME_WIDTH, AI_FRAME_HEIGHT,
        VAXP_CAP_DETECTION | VAXP_CAP_CLASSIFICATION |
            VAXP_CAP_TRACKING};
    dshanpi_vaxp_ai_start(&vaxp_config);
    FrameCHWSize image_size{AI_FRAME_CHANNEL, AI_FRAME_HEIGHT, AI_FRAME_WIDTH};
    PipeLine pipeline(debug);
    if (pipeline.Create() != 0) return 1;
    std::atomic<bool> startup_running(true);
    std::thread startup_thread([&]() {
        cv::Mat startup(OSD_HEIGHT, OSD_WIDTH, CV_8UC4);
        int phase = 0;
        while (startup_running.load()) {
            dshanpi_ui::draw_startup_spinner(startup, phase);
            pipeline.InsertFrame(startup.data);
            phase = (phase + 18) % 360;
            usleep(40000);
        }
    });
    SelfLearning learner(const_cast<char *>(model), threshold, topk,
                         image_size, debug);
    NanoTrackCrop tracker_template(
        const_cast<char *>("/sdcard/app/self_learning/cropped_test127.kmodel"),
        image_size, debug);
    NanoTrackSrc tracker_search(
        const_cast<char *>("/sdcard/app/self_learning/nanotrack_backbone_sim.kmodel"),
        image_size, debug);
    NanoTrackTracker tracker_head(
        const_cast<char *>("/sdcard/app/self_learning/nanotracker_head_calib_k230.kmodel"),
        image_size, 0.20f, debug);
    const char *database_path = "/data/self_learning.db";
    learner.load_objects(database_path);
    startup_running = false;
    startup_thread.join();
    std::thread touch_thread(touch_proc);
    cv::Mat osd(OSD_HEIGHT, OSD_WIDTH, CV_8UC4,
                cv::Scalar(0, 0, 0, 0));
    dims_t shape{1, AI_FRAME_CHANNEL, AI_FRAME_HEIGHT, AI_FRAME_WIDTH};
    std::vector<ClassResult> results;
    std::string message = "Drag a box around an object to learn";
    int message_frames = 180;
    bool inference_ready = false;
    bool tracking_ready = false;
    int tracking_lost_frames = 0;
    std::vector<float> tracking_template;
    std::vector<float> tracking_search;
    Bbox tracked_box{};

    while (!g_stop.load()) {
        DumpRes frame;
        pipeline.GetFrame(frame);
        runtime_tensor input = host_runtime_tensor::create(
            typecode_t::dt_uint8, shape,
            {(gsl::byte *)frame.virt_addr, compute_size(shape)}, false,
            hrt::pool_shared, frame.phy_addr).expect("self-learning input");
        hrt::sync(input, sync_op_t::sync_write_back, true)
            .expect("self-learning tensor sync");

        cv::Rect roi = selected_roi() &
                       cv::Rect(0, 0, AI_FRAME_WIDTH, AI_FRAME_HEIGHT);
        if (g_clear_pending.exchange(false)) {
            learner.clear_objects();
            learner.save_objects(database_path);
            g_has_roi = false;
            inference_ready = false;
            tracking_ready = false;
            tracking_template.clear();
            tracking_search.clear();
            results.clear();
            message = "Cleared - drag a box to learn";
            message_frames = 120;
        }
        if (g_invalid_roi.exchange(false)) {
            message = "Box too small - drag again";
            message_frames = 120;
        }
        if (g_learn_pending.exchange(false) && roi.width >= 32 &&
            roi.height >= 32) {
            osd.setTo(cv::Scalar(0, 0, 0, 0));
            draw_ui(osd, roi, learner.object_count(), results, "Learning...");
            pipeline.InsertFrame(osd.data);
            learner.set_roi(roi.x, roi.y, roi.width, roi.height);
            learner.pre_process(input);
            learner.inference();
            std::string name = "Class " + std::to_string(learner.object_count() + 1);
            learner.register_object(name);
            learner.save_objects(database_path);

            /* Initialize NanoTracker from the exact ROI drawn by the user.
             * The template feature remains fixed; the search and head models
             * update the ROI on every following camera frame. */
            tracker_template.set_target(roi.x, roi.y, roi.width, roi.height);
            tracker_template.pre_process(input);
            tracker_template.inference();
            tracking_template.clear();
            tracker_template.post_process(tracking_template);
            float center[2] = {roi.x + roi.width * 0.5f,
                               roi.y + roi.height * 0.5f};
            float rect_size[2] = {(float)roi.width, (float)roi.height};
            tracker_head.set_center(center);
            tracker_head.set_rect_size(rect_size);
            tracker_search.set_center(center);
            tracker_search.set_rect_size(rect_size);
            tracking_ready = !tracking_template.empty();
            tracking_lost_frames = 0;
            inference_ready = true;
            message = name + " learned - tracking started";
            message_frames = 150;
        }

        if (tracking_ready && !g_dragging.load()) {
            tracker_search.set_center(tracker_head.get_center());
            tracker_search.set_rect_size(tracker_head.get_rect_size());
            tracker_search.pre_process(input);
            tracker_search.inference();
            tracking_search.clear();
            tracker_search.post_process(tracking_search);
            tracker_head.pre_process(tracking_template, tracking_search);
            tracker_head.inference();
            tracker_head.post_process(tracked_box);

            if (tracked_box.score >= 0.20f) {
                tracking_lost_frames = 0;
                int left = std::max(0, (int)tracked_box.x);
                int top = std::max(0, (int)tracked_box.y);
                int right = std::min(AI_FRAME_WIDTH,
                                     left + std::max(1, (int)tracked_box.w));
                int bottom = std::min(AI_FRAME_HEIGHT,
                                      top + std::max(1, (int)tracked_box.h));
                g_x0 = left;
                g_y0 = top;
                g_x1 = right;
                g_y1 = bottom;
                g_has_roi = right - left >= 16 && bottom - top >= 16;
                roi = selected_roi() &
                      cv::Rect(0, 0, AI_FRAME_WIDTH, AI_FRAME_HEIGHT);
            } else if (++tracking_lost_frames >= 8) {
                tracking_ready = false;
                inference_ready = false;
                g_has_roi = false;
                results.clear();
                message = "Target lost - draw a new box";
                message_frames = 180;
            }
        }

        results.clear();
        if (inference_ready && learner.object_count() > 0 &&
            !g_dragging.load() && roi.width > 0 && roi.height > 0) {
            learner.set_roi(roi.x, roi.y, roi.width, roi.height);
            learner.pre_process(input);
            learner.inference();
            learner.post_process(results);
        }
        publish_learning_result(roi, results,
                                tracking_ready ? tracked_box.score : 0.0f);
        osd.setTo(cv::Scalar(0, 0, 0, 0));
        const char *ui_message = "";
        if (!inference_ready) {
            ui_message = message_frames-- > 0
                             ? message.c_str()
                             : "Drag a box around an object to learn";
        } else if (message_frames-- > 0) {
            ui_message = message.c_str();
        }
        draw_ui(osd, roi, learner.object_count(), results,
                ui_message);
        pipeline.InsertFrame(osd.data);
        pipeline.ReleaseFrame(frame);
    }
    touch_thread.join();
    dshanpi_vaxp_ai_stop();
    pipeline.Destroy();
    return 0;
}
