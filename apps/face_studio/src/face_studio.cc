#include "face_studio.h"

#include <cstdio>
#include <iostream>

#include "vaxp_ai_stream.h"

namespace {
std::string model_path(const std::string &dir, const char *name)
{
    return dir + "/" + name;
}
}

const char *face_studio_mode_name(FaceStudioMode mode)
{
    static const char *const names[] = {
        "Face Detection", "Emotion", "Gender",
        "Glasses", "Mask", "Eye Gaze",
    };
    auto index = static_cast<unsigned>(mode);
    return index < static_cast<unsigned>(FaceStudioMode::Count)
               ? names[index]
               : "Unknown";
}

FaceStudio::FaceStudio(const std::string &model_dir,
                       FrameCHWSize frame_size, int debug_mode,
                       FaceStudioMode initial_mode)
    : model_dir_(model_dir),
      frame_size_(frame_size),
      debug_mode_(debug_mode),
      detector_(model_path(model_dir, "face_detection_320.kmodel").c_str(),
                0.6f, 0.2f, frame_size, debug_mode),
      requested_mode_(initial_mode),
      active_mode_(FaceStudioMode::Count)
{
    load_mode(initial_mode);
    std::cout << "[face-studio] loaded initial model: " << active_mode_name()
              << std::endl;
}

void FaceStudio::request_mode(FaceStudioMode mode)
{
    if (mode >= FaceStudioMode::Detection && mode < FaceStudioMode::Count) {
        requested_mode_.store(mode);
    }
}

FaceStudioMode FaceStudio::requested_mode() const
{
    return requested_mode_.load();
}

FaceStudioMode FaceStudio::active_mode() const
{
    return active_mode_;
}

const char *FaceStudio::active_mode_name() const
{
    return face_studio_mode_name(active_mode_);
}

void FaceStudio::unload_secondary_model()
{
    emotion_.reset();
    gender_.reset();
    glasses_.reset();
    mask_.reset();
    eye_gaze_.reset();
}

void FaceStudio::load_mode(FaceStudioMode next)
{
    unload_secondary_model();
    switch (next) {
    case FaceStudioMode::Emotion:
        emotion_.reset(new FaceEmotion(
            const_cast<char *>(model_path(model_dir_, "face_emotion.kmodel").c_str()),
            frame_size_, debug_mode_));
        break;
    case FaceStudioMode::Gender:
        gender_.reset(new FaceGender(
            const_cast<char *>(model_path(model_dir_, "face_gender.kmodel").c_str()),
            frame_size_, debug_mode_));
        break;
    case FaceStudioMode::Glasses:
        glasses_.reset(new FaceGlasses(
            const_cast<char *>(model_path(model_dir_, "face_glasses.kmodel").c_str()),
            frame_size_, debug_mode_));
        break;
    case FaceStudioMode::Mask:
        mask_.reset(new FaceMask(
            const_cast<char *>(model_path(model_dir_, "face_mask.kmodel").c_str()),
            0.5f, frame_size_, debug_mode_));
        break;
    case FaceStudioMode::EyeGaze:
        eye_gaze_.reset(new EyeGaze(
            const_cast<char *>(model_path(model_dir_, "eye_gaze.kmodel").c_str()),
            frame_size_, debug_mode_));
        break;
    case FaceStudioMode::Detection:
    case FaceStudioMode::Count:
        break;
    }
    active_mode_ = next;
}

void FaceStudio::apply_requested_mode()
{
    const FaceStudioMode next = requested_mode_.load();
    if (next == active_mode_) {
        return;
    }

    load_mode(next);
    std::cout << "[face-studio] switched mode: " << active_mode_name()
              << std::endl;
}

int FaceStudio::process(runtime_tensor &input, cv::Mat &osd)
{
    std::vector<dshanpi_vaxp_ai_face_t> wire_faces;
    std::vector<std::string> wire_labels;
    faces_.clear();
    detector_.pre_process(input);
    detector_.inference();
    detector_.post_process(frame_size_, faces_);
    osd.setTo(cv::Scalar(0, 0, 0, 0));

    wire_faces.reserve(faces_.size());
    wire_labels.reserve(faces_.size());

    if (active_mode_ == FaceStudioMode::Detection) {
        detector_.draw_result(osd, faces_, false);
        for (const auto &face : faces_) {
            wire_labels.emplace_back("face");
            wire_faces.push_back({
                0, 0, face.score, 0.0f,
                face.bbox.x, face.bbox.y, face.bbox.w, face.bbox.h,
                wire_labels.back().c_str()});
        }
        dshanpi_vaxp_ai_publish_faces(
            0x0101, 0x0101, VAXP_TASK_FACE_DETECT,
            face_studio_mode_name(active_mode_), 0,
            wire_faces.data(), wire_faces.size());
        return 0;
    }

    for (auto &face : faces_) {
        switch (active_mode_) {
        case FaceStudioMode::Emotion: {
            FaceEmotionInfo result;
            emotion_->pre_process(input, face.sparse_kps.points);
            emotion_->inference();
            emotion_->post_process(result);
            emotion_->draw_result(osd, face.bbox, result, false);
            wire_labels.push_back(result.label);
            wire_faces.push_back({
                0, 0, face.score, result.score,
                face.bbox.x, face.bbox.y, face.bbox.w, face.bbox.h,
                wire_labels.back().c_str()});
            break;
        }
        case FaceStudioMode::Gender: {
            FaceGenderInfo result;
            gender_->pre_process(input, face.bbox);
            gender_->inference();
            gender_->post_process(result);
            gender_->draw_result(osd, face.bbox, result, false);
            wire_labels.push_back(result.gender);
            wire_faces.push_back({
                0, 0, face.score, result.score,
                face.bbox.x, face.bbox.y, face.bbox.w, face.bbox.h,
                wire_labels.back().c_str()});
            break;
        }
        case FaceStudioMode::Glasses: {
            FaceGlassesInfo result;
            glasses_->pre_process(input, face.sparse_kps.points);
            glasses_->inference();
            glasses_->post_process(result);
            glasses_->draw_result(osd, face.bbox, result, false);
            wire_labels.push_back(result.label);
            wire_faces.push_back({
                0, 0, face.score, result.score,
                face.bbox.x, face.bbox.y, face.bbox.w, face.bbox.h,
                wire_labels.back().c_str()});
            break;
        }
        case FaceStudioMode::Mask: {
            FaceMaskInfo result;
            mask_->pre_process(input, face.sparse_kps.points);
            mask_->inference();
            mask_->post_process(result);
            mask_->draw_result(osd, face.bbox, result, false);
            wire_labels.push_back(result.label);
            wire_faces.push_back({
                0, 0, face.score,
                result.label == "mask" ? result.score : 1.0f - result.score,
                face.bbox.x, face.bbox.y, face.bbox.w, face.bbox.h,
                wire_labels.back().c_str()});
            break;
        }
        case FaceStudioMode::EyeGaze: {
            EyeGazeInfo result;
            eye_gaze_->pre_process(input, face.bbox);
            eye_gaze_->inference();
            eye_gaze_->post_process(result);
            eye_gaze_->draw_result(osd, face.bbox, result, false);
            {
                char text[96];
                std::snprintf(text, sizeof(text), "yaw=%.4f,pitch=%.4f",
                              result.yaw, result.pitch);
                wire_labels.emplace_back(text);
            }
            wire_faces.push_back({
                0, 0, face.score, 0.0f,
                face.bbox.x, face.bbox.y, face.bbox.w, face.bbox.h,
                wire_labels.back().c_str()});
            break;
        }
        default:
            break;
        }
    }
    const uint16_t mode_id = static_cast<uint16_t>(active_mode_) + 1u;
    dshanpi_vaxp_ai_publish_faces(
        static_cast<uint16_t>(0x0100u + mode_id),
        static_cast<uint16_t>(0x0100u + mode_id),
        VAXP_TASK_FACE_RECOGNIZE, face_studio_mode_name(active_mode_), 0,
        wire_faces.data(), wire_faces.size());
    return 0;
}
