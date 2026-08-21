#ifndef DSHANPI_UI_LOCALIZATION_H
#define DSHANPI_UI_LOCALIZATION_H

#include <opencv2/core.hpp>

namespace dshanpi_ui {

enum class Language {
    SimplifiedChinese = 0,
    TraditionalChinese = 1,
    English = 2,
    Japanese = 3,
};

enum class CommonText {
    Mode,
    LoadingModel,
    Tune,
    Defaults,
    Save,
    Saved,
    SaveFailed,
    HsvHint,
};

Language current_language();

const char *localized(const char *english,
                      const char *simplified_chinese,
                      const char *traditional_chinese,
                      const char *japanese);

const char *common_text(CommonText text);

void draw_text(cv::Mat &image, const char *utf8, cv::Point baseline,
               int pixel_size, cv::Scalar color);

void draw_text_centered(cv::Mat &image, const char *utf8,
                        const cv::Rect &bounds, int pixel_size,
                        cv::Scalar color);

/* Draw the active AI mode in a consistent, screen-centered top capsule. */
void draw_mode_header(cv::Mat &image, const char *utf8, int height = 52);

int text_width(const char *utf8, int pixel_size);

} // namespace dshanpi_ui

#endif
