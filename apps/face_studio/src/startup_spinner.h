#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include <opencv2/imgproc.hpp>

#include "ios_overlay.h"
#include "ui_localization.h"

namespace dshanpi_ui {

inline void draw_startup_spinner(cv::Mat &osd, int phase)
{
    const cv::Point center(osd.cols / 2, osd.rows / 2);
    const int start = phase % 360;

    osd.setTo(cv::Scalar(0, 0, 0, 255));
    cv::ellipse(osd, center, cv::Size(25, 25), 0, 0, 360,
                cv::Scalar(52, 52, 52, 255), 4, cv::LINE_AA);
    cv::ellipse(osd, center, cv::Size(25, 25), 0, start, start + 96,
                cv::Scalar(244, 247, 251, 255), 4, cv::LINE_AA);
}

/*
 * Once the camera preview is available, keep it visible while the selected
 * model finishes loading.  This makes restoring a heavier saved mode feel
 * responsive and tells the user exactly which model is being prepared.
 */
inline void draw_model_loading(cv::Mat &osd, int phase,
                               const char *mode_name)
{
    osd.setTo(cv::Scalar(0, 0, 0, 0));

    std::string text;
    switch (current_language()) {
    case Language::SimplifiedChinese:
        text = std::string("正在加载") + mode_name + "...";
        break;
    case Language::TraditionalChinese:
        text = std::string("正在載入") + mode_name + "...";
        break;
    case Language::Japanese:
        text = std::string(mode_name) + "を読み込み中...";
        break;
    case Language::English:
    default:
        text = std::string("Loading ") + mode_name + "...";
        break;
    }
    const int font_size = 20;
    const int text_width_px = text_width(text.c_str(), font_size);
    const int spinner_space = 42;
    const int panel_width = std::min(osd.cols - 24,
                                     text_width_px + spinner_space + 42);
    const cv::Rect panel((osd.cols - panel_width) / 2,
                         osd.rows - 92, panel_width, 54);
    ios_ui::rounded_rect(osd, panel, cv::Scalar(24, 24, 28, 230),
                         cv::FILLED, cv::LINE_AA);

    const cv::Point spinner_center(panel.x + 27,
                                   panel.y + panel.height / 2);
    const int start = phase % 360;
    cv::ellipse(osd, spinner_center, cv::Size(10, 10), 0, 0, 360,
                cv::Scalar(92, 92, 98, 255), 3, cv::LINE_AA);
    cv::ellipse(osd, spinner_center, cv::Size(10, 10), 0,
                start, start + 100,
                cv::Scalar(244, 247, 251, 255), 3, cv::LINE_AA);

    const cv::Point text_origin(panel.x + spinner_space,
                                panel.y + (panel.height + font_size) / 2 - 2);
    draw_text(osd, text.c_str(), text_origin, font_size,
              cv::Scalar(255, 255, 255, 255));
}

template <typename Camera, typename DrawOverlay>
inline void show_model_loading_until_ready(
    Camera &camera, cv::Mat &osd, const std::atomic<bool> &model_ready,
    const char *mode_name, DrawOverlay draw_overlay)
{
    int phase = 0;
    do {
        draw_model_loading(osd, phase, mode_name);
        draw_overlay(osd);
        camera.InsertFrame(osd.data, false);
        if (model_ready.load()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        phase = (phase + 18) % 360;
    } while (!model_ready.load());
}

template <typename Camera>
inline void show_model_loading_until_ready(
    Camera &camera, cv::Mat &osd, const std::atomic<bool> &model_ready,
    const char *mode_name)
{
    show_model_loading_until_ready(
        camera, osd, model_ready, mode_name, [](cv::Mat &) {});
}

/*
 * Runtime model changes must not execute on the preview/inference loop.  Run
 * the expensive reset + kmodel construction on a worker while the caller
 * keeps refreshing a live, animated loading overlay.  The do/while in the
 * renderer guarantees that even a cached or shared model switch displays one
 * acknowledged UI frame before inference resumes.
 */
template <typename Camera, typename Loader, typename DrawOverlay>
inline void load_model_with_feedback(Camera &camera, cv::Mat &osd,
                                     const char *mode_name, Loader loader,
                                     DrawOverlay draw_overlay)
{
    std::atomic<bool> model_ready(false);
    std::thread model_thread([&]() {
        loader();
        model_ready.store(true);
    });
    show_model_loading_until_ready(
        camera, osd, model_ready, mode_name, draw_overlay);
    model_thread.join();
}

}  // namespace dshanpi_ui
