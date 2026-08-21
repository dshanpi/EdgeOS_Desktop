#pragma once

#include <algorithm>
#include <opencv2/opencv.hpp>

namespace ios_ui {

/* Shared landscape touch extent for the top-left back control.  The visible
 * button stays compact, but the corner itself is always a valid target. */
constexpr int kBackTouchExtent = 104;

inline cv::Scalar glass() { return cv::Scalar(38, 38, 40, 224); }
inline cv::Scalar panel() { return cv::Scalar(30, 30, 32, 242); }
inline cv::Scalar accent() { return cv::Scalar(246, 122, 10, 248); }
inline cv::Scalar secondary() { return cv::Scalar(174, 174, 178, 255); }
inline cv::Scalar white() { return cv::Scalar(255, 255, 255, 255); }

inline void rounded_rect(cv::Mat &image, const cv::Rect &input,
                         const cv::Scalar &color, int thickness,
                         int line_type = cv::LINE_8, int shift = 0)
{
    cv::Rect bounds(0, 0, image.cols, image.rows);
    cv::Rect rect = input & bounds;
    if (rect.empty()) return;
    if (thickness != cv::FILLED) {
        cv::rectangle(image, rect, color, thickness, line_type, shift);
        return;
    }
    int radius = std::max(4, std::min(18, std::min(rect.width, rect.height) / 3));
    cv::rectangle(image,
                  cv::Rect(rect.x + radius, rect.y,
                           std::max(1, rect.width - radius * 2), rect.height),
                  color, cv::FILLED, line_type, shift);
    cv::rectangle(image,
                  cv::Rect(rect.x, rect.y + radius,
                           rect.width, std::max(1, rect.height - radius * 2)),
                  color, cv::FILLED, line_type, shift);
    cv::circle(image, {rect.x + radius, rect.y + radius}, radius,
               color, cv::FILLED, line_type, shift);
    cv::circle(image, {rect.x + rect.width - radius - 1, rect.y + radius},
               radius, color, cv::FILLED, line_type, shift);
    cv::circle(image, {rect.x + radius, rect.y + rect.height - radius - 1},
               radius, color, cv::FILLED, line_type, shift);
    cv::circle(image,
               {rect.x + rect.width - radius - 1,
                rect.y + rect.height - radius - 1},
               radius, color, cv::FILLED, line_type, shift);
}

inline void capsule_label(cv::Mat &image, const cv::Rect &rect,
                          const char *label, bool selected = false)
{
    rounded_rect(image, rect, selected ? accent() : glass(), cv::FILLED);
    int baseline = 0;
    cv::Size size = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX,
                                    0.60, 2, &baseline);
    cv::Point origin(rect.x + (rect.width - size.width) / 2,
                     rect.y + (rect.height + size.height) / 2);
    cv::putText(image, label, origin, cv::FONT_HERSHEY_SIMPLEX,
                0.60, white(), 2, cv::LINE_AA);
}

}  // namespace ios_ui
