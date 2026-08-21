#include <opencv2/opencv.hpp>
#include <ft2build.h>
#include <unordered_map>
#include FT_FREETYPE_H

class CNTextRenderer {
public:
    CNTextRenderer();
    ~CNTextRenderer();
    void init(const std::string& fontPath, int fontSize);
    void putText(cv::Mat& img, const std::string& text, cv::Point org, cv::Scalar color);

private:
    FT_Library ft_;
    FT_Face face_;
    int fontSize_;
    struct GlyphInfo {
        cv::Mat bitmap;
        int left;
        int top;
        int advance;
    };
    std::unordered_map<FT_ULong, GlyphInfo> glyphCache_;

    FT_ULong getNextChar(const std::string& text, size_t& i);
    void renderGlyph(FT_ULong c);
};
    