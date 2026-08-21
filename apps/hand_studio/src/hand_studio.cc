#include "hand_studio.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>

#include "vaxp_ai_stream.h"

namespace {
std::string model_path(const std::string &dir, const char *name)
{
    return dir + "/" + name;
}
}

const char *hand_studio_mode_name(HandStudioMode mode)
{
    static const char *const names[] = {
        "Hand Detection", "21 Keypoints", "Static Gesture", "AI Gesture",
    };
    unsigned index = static_cast<unsigned>(mode);
    return index < static_cast<unsigned>(HandStudioMode::Count)
               ? names[index]
               : "Unknown";
}

HandStudio::HandStudio(const std::string &model_dir,
                       FrameCHWSize frame_size, int debug_mode,
                       HandStudioMode initial_mode)
    : model_dir_(model_dir),
      frame_size_(frame_size),
      debug_mode_(debug_mode),
      detector_(const_cast<char *>(
                    model_path(model_dir, "hand_det.kmodel").c_str()),
                0.4f, 0.5f, frame_size, debug_mode),
      requested_mode_(initial_mode),
      active_mode_(HandStudioMode::Count)
{
    load_mode(initial_mode);
    std::cout << "[hand-studio] loaded initial model: " << active_mode_name()
              << std::endl;
}

void HandStudio::request_mode(HandStudioMode mode)
{
    if (mode >= HandStudioMode::Detection && mode < HandStudioMode::Count) {
        requested_mode_.store(mode);
    }
}

HandStudioMode HandStudio::requested_mode() const
{
    return requested_mode_.load();
}

HandStudioMode HandStudio::active_mode() const
{
    return active_mode_;
}

const char *HandStudio::active_mode_name() const
{
    return hand_studio_mode_name(active_mode_);
}

void HandStudio::load_mode(HandStudioMode next)
{
    const bool keep_keypoint = keypoint_ &&
        (active_mode_ == HandStudioMode::Keypoints ||
         active_mode_ == HandStudioMode::StaticGesture) &&
        (next == HandStudioMode::Keypoints ||
         next == HandStudioMode::StaticGesture);
    if (!keep_keypoint) {
        keypoint_.reset();
    }
    recognition_.reset();
    if (next == HandStudioMode::Keypoints ||
        next == HandStudioMode::StaticGesture) {
        if (keypoint_) {
            active_mode_ = next;
            return;
        }
        std::string path = model_path(model_dir_, "handkp_det.kmodel");
        keypoint_.reset(new HandKeypoint(
            const_cast<char *>(path.c_str()), frame_size_, debug_mode_));
    } else if (next == HandStudioMode::ModelGesture) {
        recognition_.reset(new RecognitionRunner(
            model_path(model_dir_, "hand_reco.kmodel"),
            frame_size_, debug_mode_));
    }
    active_mode_ = next;
}

void HandStudio::apply_requested_mode()
{
    const HandStudioMode next = requested_mode_.load();
    if (next == active_mode_) {
        return;
    }

    load_mode(next);
    std::cout << "[hand-studio] switched mode: " << active_mode_name()
              << std::endl;
}

Bbox HandStudio::keypoint_roi(const BoxInfo &hand) const
{
    int width = static_cast<int>(hand.x2 - hand.x1 + 1);
    int height = static_cast<int>(hand.y2 - hand.y1 + 1);
    int radius = static_cast<int>(1.26f * std::max(width, height) / 2);
    int cx = static_cast<int>((hand.x1 + hand.x2) / 2);
    int cy = static_cast<int>((hand.y1 + hand.y2) / 2);
    int x1 = std::max(0, cx - radius);
    int y1 = std::max(0, cy - radius);
    int x2 = std::min(frame_size_.width - 1, cx + radius);
    int y2 = std::min(frame_size_.height - 1, cy + radius);
    return Bbox{static_cast<float>(x1), static_cast<float>(y1),
                static_cast<float>(x2 - x1 + 1),
                static_cast<float>(y2 - y1 + 1)};
}

void HandStudio::draw_detection(cv::Mat &osd, const BoxInfo &hand) const
{
    int x = static_cast<int>(hand.x1 / frame_size_.width * osd.cols);
    int y = static_cast<int>(hand.y1 / frame_size_.height * osd.rows);
    int w = static_cast<int>((hand.x2 - hand.x1) /
                             frame_size_.width * osd.cols);
    int h = static_cast<int>((hand.y2 - hand.y1) /
                             frame_size_.height * osd.rows);
    cv::rectangle(osd, cv::Rect(x, y, w, h),
                  cv::Scalar(48, 220, 120, 255), 3);
    cv::putText(osd, "hand", cv::Point(x, std::max(24, y - 8)),
                cv::FONT_HERSHEY_SIMPLEX, 0.7,
                cv::Scalar(255, 255, 255, 255), 2);
}

int HandStudio::process(runtime_tensor &input, cv::Mat &osd)
{
    std::vector<dshanpi_vaxp_ai_detection_t> wire_detections;
    std::vector<dshanpi_vaxp_ai_pose_t> wire_poses;
    std::vector<std::vector<dshanpi_vaxp_ai_keypoint_t>> wire_keypoints;
    std::vector<std::string> wire_labels;
    hands_.clear();
    detector_.pre_process(input);
    detector_.inference();
    detector_.post_process(hands_);
    osd.setTo(cv::Scalar(0, 0, 0, 0));
    wire_detections.reserve(hands_.size());
    wire_poses.reserve(hands_.size());
    wire_keypoints.reserve(hands_.size());
    wire_labels.reserve(hands_.size());

    for (const BoxInfo &hand : hands_) {
        if (active_mode_ == HandStudioMode::Detection) {
            draw_detection(osd, hand);
            wire_detections.push_back({
                0, 0, hand.score, hand.x1, hand.y1,
                hand.x2 - hand.x1, hand.y2 - hand.y1, 0, "hand"});
            continue;
        }
        if (active_mode_ == HandStudioMode::ModelGesture) {
            Bbox roi = keypoint_roi(hand);
            std::string result = recognition_->process(
                input, roi.x, roi.y, roi.w, roi.h, osd);
            const size_t separator = result.rfind(':');
            float confidence = 0.0f;
            if (separator != std::string::npos) {
                confidence = std::strtof(result.c_str() + separator + 1,
                                         nullptr);
                result.resize(separator);
            }
            static const char *const labels[] = {
                "gun", "other", "yeah", "five"};
            uint16_t class_id = 1;
            for (uint16_t index = 0; index < 4; ++index) {
                if (result == labels[index]) {
                    class_id = index;
                    break;
                }
            }
            wire_labels.push_back(result);
            wire_detections.push_back({
                class_id, 0, confidence, roi.x, roi.y, roi.w, roi.h, 0,
                wire_labels.back().c_str()});
            continue;
        }

        Bbox roi = keypoint_roi(hand);
        keypoint_->pre_process(input, roi);
        keypoint_->inference();
        keypoint_->post_process(roi);
        std::string label;
        if (active_mode_ == HandStudioMode::StaticGesture) {
            label = keypoint_->h_gesture(keypoint_->hand_angle());
        }
        Bbox draw_box{hand.x1, hand.y1,
                      hand.x2 - hand.x1, hand.y2 - hand.y1};
        keypoint_->draw_result(osd, label, draw_box);
        std::vector<dshanpi_vaxp_ai_keypoint_t> points;
        points.reserve(keypoint_->results.size() / 2u);
        for (size_t point = 0; point + 1 < keypoint_->results.size();
             point += 2) {
            points.push_back({
                static_cast<float>(keypoint_->results[point]),
                static_cast<float>(keypoint_->results[point + 1]), 1.0f});
        }
        wire_keypoints.push_back(std::move(points));
        wire_labels.push_back(label);
        wire_poses.push_back({
            0, hand.score, draw_box.x, draw_box.y, draw_box.w, draw_box.h,
            wire_keypoints.back().data(), wire_keypoints.back().size(),
            wire_labels.back().c_str()});
    }
    const uint16_t mode_id = static_cast<uint16_t>(active_mode_) + 1u;
    if (active_mode_ == HandStudioMode::Keypoints ||
        active_mode_ == HandStudioMode::StaticGesture) {
        dshanpi_vaxp_ai_publish_poses(
            static_cast<uint16_t>(0x0300u + mode_id),
            static_cast<uint16_t>(0x0300u + mode_id),
            hand_studio_mode_name(active_mode_), 0,
            wire_poses.data(), wire_poses.size());
    } else {
        dshanpi_vaxp_ai_publish_detections(
            static_cast<uint16_t>(0x0300u + mode_id),
            static_cast<uint16_t>(0x0300u + mode_id),
            VAXP_TASK_DETECTION, hand_studio_mode_name(active_mode_), 0,
            wire_detections.data(), wire_detections.size());
    }
    return 0;
}
