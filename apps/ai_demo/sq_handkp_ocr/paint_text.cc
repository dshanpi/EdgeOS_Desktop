#include "paint_text.h"
#include <iostream>

CNTextRenderer::CNTextRenderer(){}

CNTextRenderer::~CNTextRenderer() {
    FT_Done_Face(face_);
    FT_Done_FreeType(ft_);
}

void CNTextRenderer::init(const std::string& fontPath, int fontSize){
    fontSize_=fontSize;
    if (FT_Init_FreeType(&ft_)) {
        throw std::runtime_error("Could not init FreeType Library");
    }
    if (FT_New_Face(ft_, fontPath.c_str(), 0, &face_)) {
        throw std::runtime_error("Failed to load font: " + fontPath);
    }
    FT_Set_Pixel_Sizes(face_, 0, fontSize_);
}

void CNTextRenderer::putText(cv::Mat& img, const std::string& text, cv::Point org, cv::Scalar color) {
    int x = org.x;
    int y = org.y;

    for (size_t i = 0; i < text.length(); ) {
        FT_ULong c = getNextChar(text, i);
        if (glyphCache_.find(c) == glyphCache_.end()) {
            renderGlyph(c);
        }

        const GlyphInfo& info = glyphCache_[c];
        const cv::Mat& glyph = info.bitmap;

        for (int row = 0; row < glyph.rows; ++row) {
            for (int col = 0; col < glyph.cols; ++col) {
                int py = y - info.top + row;
                int px = x + info.left + col;
                if (px >= 0 && py >= 0 && px < img.cols && py < img.rows) {
                    uchar alpha = glyph.at<uchar>(row, col);
                    float alpha_factor = alpha / 255.0f;

                    if (img.channels() == 3) {
                        cv::Vec3b& pixel = img.at<cv::Vec3b>(py, px);
                        for (int c = 0; c < 3; ++c)
                            pixel[c] = static_cast<uchar>((1 - alpha_factor) * pixel[c] + alpha_factor * color[c]);
                    } else if (img.channels() == 4) {
                        cv::Vec4b& pixel = img.at<cv::Vec4b>(py, px);
                        for (int c = 0; c < 3; ++c){
                            pixel[c] = static_cast<uchar>((1 - alpha_factor) * pixel[c] + alpha_factor * color[c]);
                        }
                        // 设置 alpha 通道，不透明或根据 alpha_factor 叠加
                        pixel[3] = std::max(pixel[3], static_cast<uchar>(alpha)); 
                    }
                }
            }
        }

        x += info.advance;
    }
}

FT_ULong CNTextRenderer::getNextChar(const std::string& text, size_t& i) {
    unsigned char ch = text[i];
    FT_ULong c;
    if (ch >= 0xF0) {
        c = ((text[i] & 0x07) << 18) | ((text[i + 1] & 0x3F) << 12) |
            ((text[i + 2] & 0x3F) << 6) | (text[i + 3] & 0x3F);
        i += 4;
    } else if (ch >= 0xE0) {
        c = ((text[i] & 0x0F) << 12) | ((text[i + 1] & 0x3F) << 6) |
            (text[i + 2] & 0x3F);
        i += 3;
    } else if (ch >= 0xC0) {
        c = ((text[i] & 0x1F) << 6) | (text[i + 1] & 0x3F);
        i += 2;
    } else {
        c = text[i];
        i += 1;
    }
    return c;
}

void CNTextRenderer::renderGlyph(FT_ULong c) {
    if (FT_Load_Char(face_, c, FT_LOAD_RENDER)) {
        std::cerr << "Failed to load glyph: " << c << std::endl;
        return;
    }

    FT_GlyphSlot g = face_->glyph;
    cv::Mat bitmap(g->bitmap.rows, g->bitmap.width, CV_8UC1);
    for (int r = 0; r < g->bitmap.rows; ++r) {
        memcpy(bitmap.ptr(r), g->bitmap.buffer + r * g->bitmap.pitch, g->bitmap.width);
    }

    GlyphInfo info;
    info.bitmap = bitmap;
    info.left = g->bitmap_left;
    info.top = g->bitmap_top;
    info.advance = g->advance.x >> 6;

    glyphCache_[c] = std::move(info);
}



