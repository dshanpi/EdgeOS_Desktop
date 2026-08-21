#ifndef DSHANPI_FACE_STUDIO_H
#define DSHANPI_FACE_STUDIO_H

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "eye_gaze.h"
#include "face_detection.h"
#include "face_emotion.h"
#include "face_gender.h"
#include "face_glasses.h"
#include "face_mask.h"

enum class FaceStudioMode {
    Detection = 0,
    Emotion,
    Gender,
    Glasses,
    Mask,
    EyeGaze,
    Count,
};

class FaceStudio {
public:
    FaceStudio(const std::string &model_dir, FrameCHWSize frame_size,
               int debug_mode,
               FaceStudioMode initial_mode = FaceStudioMode::Detection);

    void request_mode(FaceStudioMode mode);
    FaceStudioMode requested_mode() const;
    FaceStudioMode active_mode() const;
    const char *active_mode_name() const;
    void apply_requested_mode();
    int process(runtime_tensor &input, cv::Mat &osd);

private:
    void load_mode(FaceStudioMode mode);
    void unload_secondary_model();

    std::string model_dir_;
    FrameCHWSize frame_size_;
    int debug_mode_;
    FaceDetection detector_;
    std::atomic<FaceStudioMode> requested_mode_;
    FaceStudioMode active_mode_;
    std::vector<FaceDetectionInfo> faces_;

    std::unique_ptr<FaceEmotion> emotion_;
    std::unique_ptr<FaceGender> gender_;
    std::unique_ptr<FaceGlasses> glasses_;
    std::unique_ptr<FaceMask> mask_;
    std::unique_ptr<EyeGaze> eye_gaze_;
};

const char *face_studio_mode_name(FaceStudioMode mode);

#endif
