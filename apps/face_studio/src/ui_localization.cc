#include "ui_localization.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <opencv2/imgproc.hpp>

namespace dshanpi_ui {
namespace {

constexpr const char *kSettingsPath = "/data/dshanpi_system.conf";
constexpr const char *kFontPaths[] = {
    "/sdcard/app/dshanpi_ui_font.ttf",
    "/sdcard/res/font/SourceHanSansSC-Normal-Min.ttf",
};

Language load_language()
{
    FILE *file = std::fopen(kSettingsPath, "r");
    if (file == nullptr)
        return Language::English;

    char line[160];
    int value = static_cast<int>(Language::English);
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        if (std::sscanf(line, "language=%d", &value) == 1)
            break;
    }
    std::fclose(file);
    if (value < static_cast<int>(Language::SimplifiedChinese) ||
        value > static_cast<int>(Language::Japanese))
        return Language::English;
    return static_cast<Language>(value);
}

uint32_t next_codepoint(const char *text, size_t length, size_t &offset)
{
    const unsigned char first =
        static_cast<unsigned char>(text[offset++]);
    if (first < 0x80)
        return first;

    int continuation_count = 0;
    uint32_t codepoint = 0;
    if ((first & 0xe0) == 0xc0) {
        continuation_count = 1;
        codepoint = first & 0x1f;
    } else if ((first & 0xf0) == 0xe0) {
        continuation_count = 2;
        codepoint = first & 0x0f;
    } else if ((first & 0xf8) == 0xf0) {
        continuation_count = 3;
        codepoint = first & 0x07;
    } else {
        return '?';
    }

    if (offset + static_cast<size_t>(continuation_count) > length) {
        offset = length;
        return '?';
    }
    for (int index = 0; index < continuation_count; ++index) {
        const unsigned char continuation =
            static_cast<unsigned char>(text[offset++]);
        if ((continuation & 0xc0) != 0x80)
            return '?';
        codepoint = (codepoint << 6) | (continuation & 0x3f);
    }
    return codepoint;
}

struct Glyph {
    cv::Mat bitmap;
    int left = 0;
    int top = 0;
    int advance = 0;
};

class FontRenderer {
public:
    FontRenderer()
    {
        if (FT_Init_FreeType(&library_) != 0) {
            std::cerr << "[ui-language] FreeType initialization failed"
                      << std::endl;
            return;
        }
        for (const char *path : kFontPaths) {
            FT_Face face = nullptr;
            if (FT_New_Face(library_, path, 0, &face) == 0) {
                /* Some RT-Smart fonts expose more than one charmap. Make
                 * Unicode selection explicit before checking glyph
                 * coverage, then retain every usable face as a fallback.
                 * Droid Sans Fallback contains the CJK glyphs but no
                 * half-width ASCII digits/letters, while Source Han fills
                 * that gap. Stopping at the first face turned strings such
                 * as "21\u5173\u952e\u70b9" and "AI\u624b\u52bf" into square boxes. */
                FT_Select_Charmap(face, FT_ENCODING_UNICODE);
                faces_.push_back(face);
                std::cout << "[ui-language] UI font loaded: " << path
                          << std::endl;
            }
        }
        if (faces_.empty())
            std::cerr << "[ui-language] UI fonts not found" << std::endl;
    }

    bool ready() const { return !faces_.empty(); }

    int measure(const char *text, int pixel_size)
    {
        if (text == nullptr || !ready())
            return 0;
        std::lock_guard<std::mutex> lock(mutex_);
        int width = 0;
        const size_t length = std::strlen(text);
        for (size_t offset = 0; offset < length;) {
            const uint32_t codepoint = next_codepoint(text, length, offset);
            width += glyph(codepoint, pixel_size).advance;
        }
        return width;
    }

    void draw(cv::Mat &image, const char *text, cv::Point baseline,
              int pixel_size, cv::Scalar color)
    {
        if (text == nullptr || !ready())
            return;
        std::lock_guard<std::mutex> lock(mutex_);
        int pen_x = baseline.x;
        const size_t length = std::strlen(text);
        for (size_t offset = 0; offset < length;) {
            const uint32_t codepoint = next_codepoint(text, length, offset);
            const Glyph &item = glyph(codepoint, pixel_size);
            blend_glyph(image, item, pen_x, baseline.y, color);
            pen_x += item.advance;
        }
    }

private:
    const Glyph &glyph(uint32_t codepoint, int pixel_size)
    {
        const uint64_t key =
            (static_cast<uint64_t>(pixel_size) << 32) | codepoint;
        auto found = glyphs_.find(key);
        if (found != glyphs_.end())
            return found->second;

        Glyph item;
        FT_Face face = face_for(codepoint);
        uint32_t rendered_codepoint = codepoint;
        if (face == nullptr) {
            rendered_codepoint = '?';
            face = face_for(rendered_codepoint);
        }
        if (face == nullptr)
            face = faces_.front();
        FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixel_size));
        if (FT_Load_Char(face, rendered_codepoint, FT_LOAD_RENDER) != 0 &&
            rendered_codepoint != '?') {
            rendered_codepoint = '?';
            FT_Face fallback = face_for(rendered_codepoint);
            if (fallback != nullptr) {
                face = fallback;
                FT_Set_Pixel_Sizes(face, 0,
                                   static_cast<FT_UInt>(pixel_size));
            }
            FT_Load_Char(face, rendered_codepoint, FT_LOAD_RENDER);
        }
        const FT_GlyphSlot slot = face->glyph;
        item.left = slot->bitmap_left;
        item.top = slot->bitmap_top;
        item.advance = std::max(1, static_cast<int>(slot->advance.x >> 6));
        item.bitmap = cv::Mat(slot->bitmap.rows, slot->bitmap.width, CV_8UC1);
        for (unsigned int row = 0; row < slot->bitmap.rows; ++row) {
            const unsigned char *source =
                slot->bitmap.buffer + row * slot->bitmap.pitch;
            std::memcpy(item.bitmap.ptr(static_cast<int>(row)), source,
                        slot->bitmap.width);
        }
        return glyphs_.emplace(key, std::move(item)).first->second;
    }

    FT_Face face_for(uint32_t codepoint) const
    {
        for (FT_Face face : faces_) {
            if (FT_Get_Char_Index(face, codepoint) != 0)
                return face;
        }
        return nullptr;
    }

    static void blend_glyph(cv::Mat &image, const Glyph &glyph,
                            int pen_x, int baseline_y, cv::Scalar color)
    {
        for (int row = 0; row < glyph.bitmap.rows; ++row) {
            const int y = baseline_y - glyph.top + row;
            if (y < 0 || y >= image.rows)
                continue;
            for (int column = 0; column < glyph.bitmap.cols; ++column) {
                const int x = pen_x + glyph.left + column;
                if (x < 0 || x >= image.cols)
                    continue;
                const float source_alpha =
                    glyph.bitmap.at<unsigned char>(row, column) / 255.0f;
                if (source_alpha <= 0.0f)
                    continue;
                if (image.channels() == 4) {
                    cv::Vec4b &pixel = image.at<cv::Vec4b>(y, x);
                    const float destination_alpha = pixel[3] / 255.0f;
                    const float output_alpha = source_alpha +
                        destination_alpha * (1.0f - source_alpha);
                    for (int channel = 0; channel < 3; ++channel) {
                        const float value =
                            (color[channel] * source_alpha +
                             pixel[channel] * destination_alpha *
                                 (1.0f - source_alpha)) /
                            std::max(output_alpha, 0.001f);
                        pixel[channel] = static_cast<unsigned char>(
                            std::max(0.0f, std::min(value, 255.0f)));
                    }
                    pixel[3] = static_cast<unsigned char>(
                        std::round(output_alpha * 255.0f));
                } else if (image.channels() == 3) {
                    cv::Vec3b &pixel = image.at<cv::Vec3b>(y, x);
                    for (int channel = 0; channel < 3; ++channel) {
                        pixel[channel] = static_cast<unsigned char>(
                            pixel[channel] * (1.0f - source_alpha) +
                            color[channel] * source_alpha);
                    }
                }
            }
        }
    }

    FT_Library library_ = nullptr;
    std::vector<FT_Face> faces_;
    std::map<uint64_t, Glyph> glyphs_;
    std::mutex mutex_;
};

FontRenderer &renderer()
{
    /* Intentionally retain the renderer until process teardown. RT-Smart
     * reclaims the child process as a unit, avoiding static destruction order
     * issues between FreeType and OpenCV. */
    static FontRenderer *instance = new FontRenderer();
    return *instance;
}

} // namespace

Language current_language()
{
    static const Language language = load_language();
    return language;
}

const char *localized(const char *english,
                      const char *simplified_chinese,
                      const char *traditional_chinese,
                      const char *japanese)
{
    switch (current_language()) {
    case Language::SimplifiedChinese: return simplified_chinese;
    case Language::TraditionalChinese: return traditional_chinese;
    case Language::Japanese: return japanese;
    case Language::English:
    default: return english;
    }
}

const char *common_text(CommonText text)
{
    switch (text) {
    case CommonText::Mode:
        return localized("Mode", "模式", "模式", "モード");
    case CommonText::LoadingModel:
        return localized("Loading model...", "正在加载模型...",
                         "正在載入模型...", "モデルを読み込み中...");
    case CommonText::Tune:
        return localized("Tune", "参数", "參數", "調整");
    case CommonText::Defaults:
        return localized("Defaults", "恢复默认", "恢復預設", "初期値");
    case CommonText::Save:
        return localized("Save", "保存", "儲存", "保存");
    case CommonText::Saved:
        return localized("Saved", "已保存", "已儲存", "保存済み");
    case CommonText::SaveFailed:
        return localized("Save failed", "保存失败", "儲存失敗", "保存失敗");
    case CommonText::HsvHint:
        return localized("HSV: H 0-179, S/V 0-255",
                         "HSV：H 0-179，S/V 0-255",
                         "HSV：H 0-179，S/V 0-255",
                         "HSV：H 0-179、S/V 0-255");
    }
    return "";
}

void draw_text(cv::Mat &image, const char *utf8, cv::Point baseline,
               int pixel_size, cv::Scalar color)
{
    if (renderer().ready()) {
        renderer().draw(image, utf8, baseline, pixel_size, color);
        return;
    }
    cv::putText(image, utf8, baseline, cv::FONT_HERSHEY_SIMPLEX,
                pixel_size / 30.0, color, 2);
}

int text_width(const char *utf8, int pixel_size)
{
    if (renderer().ready())
        return renderer().measure(utf8, pixel_size);
    return cv::getTextSize(utf8, cv::FONT_HERSHEY_SIMPLEX,
                           pixel_size / 30.0, 2, nullptr).width;
}

void draw_text_centered(cv::Mat &image, const char *utf8,
                        const cv::Rect &bounds, int pixel_size,
                        cv::Scalar color)
{
    const int width = text_width(utf8, pixel_size);
    const cv::Point baseline(
        bounds.x + std::max(0, (bounds.width - width) / 2),
        bounds.y + (bounds.height + pixel_size) / 2 - 2);
    draw_text(image, utf8, baseline, pixel_size, color);
}

void draw_mode_header(cv::Mat &image, const char *utf8, int height)
{
    if (image.empty() || utf8 == nullptr || utf8[0] == '\0')
        return;

    const int width = std::min(252, std::max(1, image.cols - 388));
    const cv::Rect bounds((image.cols - width) / 2, 12, width,
                          std::max(40, height));
    const int radius = std::min(16, bounds.height / 3);
    const cv::Scalar panel(34, 38, 48, 226);
    cv::rectangle(image,
                  cv::Rect(bounds.x + radius, bounds.y,
                           bounds.width - radius * 2, bounds.height),
                  panel, cv::FILLED);
    cv::rectangle(image,
                  cv::Rect(bounds.x, bounds.y + radius,
                           bounds.width, bounds.height - radius * 2),
                  panel, cv::FILLED);
    cv::circle(image, cv::Point(bounds.x + radius, bounds.y + radius),
               radius, panel, cv::FILLED);
    cv::circle(image,
               cv::Point(bounds.x + bounds.width - radius - 1,
                         bounds.y + radius),
               radius, panel, cv::FILLED);
    cv::circle(image,
               cv::Point(bounds.x + radius,
                         bounds.y + bounds.height - radius - 1),
               radius, panel, cv::FILLED);
    cv::circle(image,
               cv::Point(bounds.x + bounds.width - radius - 1,
                         bounds.y + bounds.height - radius - 1),
               radius, panel, cv::FILLED);

    int pixel_size = 21;
    while (pixel_size > 17 && text_width(utf8, pixel_size) > width - 28)
        --pixel_size;
    draw_text_centered(image, utf8, bounds, pixel_size,
                       cv::Scalar(255, 255, 255, 255));
    cv::line(image,
             cv::Point(bounds.x + bounds.width / 2 - 24,
                       bounds.y + bounds.height - 3),
             cv::Point(bounds.x + bounds.width / 2 + 24,
                       bounds.y + bounds.height - 3),
             cv::Scalar(246, 122, 10, 255), 3, cv::LINE_AA);
}

} // namespace dshanpi_ui
