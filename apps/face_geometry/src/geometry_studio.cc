#include "geometry_studio.h"
#include <cstdio>
#include <iostream>
#include "setting.h"
#include "vaxp_ai_stream.h"

namespace {
std::string model_path(const std::string &dir, const char *name)
{
    return dir + "/" + name;
}
}

const char *geometry_mode_name(GeometryMode mode)
{
    static const char *const names[] = {
        "Face Pose", "Face Mesh", "Face Parse", "Face Alignment",
    };
    const auto index = static_cast<unsigned>(mode);
    return index < static_cast<unsigned>(GeometryMode::Count)
               ? names[index] : "Unknown";
}

GeometryStudio::GeometryStudio(const std::string &model_dir,
                               FrameCHWSize frame_size, int debug_mode,
                               GeometryMode initial_mode)
    : model_dir_(model_dir), frame_size_(frame_size), debug_mode_(debug_mode),
      detector_(model_path(model_dir, "face_detection_320.kmodel").c_str(),
                0.6f, 0.2f, frame_size, debug_mode),
      requested_mode_(initial_mode), active_mode_(GeometryMode::Count)
{
    load_mode(initial_mode);
    std::cout << "[face-geometry] loaded initial model: "
              << geometry_mode_name(active_mode_) << std::endl;
}

void GeometryStudio::request_mode(GeometryMode mode)
{
    if (mode >= GeometryMode::Pose && mode < GeometryMode::Count)
        requested_mode_.store(mode);
}

GeometryMode GeometryStudio::requested_mode() const
{
    return requested_mode_.load();
}

GeometryMode GeometryStudio::active_mode() const { return active_mode_; }

void GeometryStudio::unload_geometry_model()
{
    pose_.reset();
    mesh_.reset();
    parse_.reset();
    alignment_.reset();
}

void GeometryStudio::load_mode(GeometryMode next)
{
    unload_geometry_model();
    switch (next) {
    case GeometryMode::Pose:
        pose_.reset(new FacePose(const_cast<char *>(
            model_path(model_dir_, "face_pose.kmodel").c_str()),
            frame_size_, debug_mode_));
        break;
    case GeometryMode::Mesh:
        mesh_.reset(new FaceMesh(
            const_cast<char *>(model_path(
                model_dir_, "face_alignment.kmodel").c_str()),
            const_cast<char *>(model_path(
                model_dir_, "face_alignment_post.kmodel").c_str()),
            frame_size_, debug_mode_));
        break;
    case GeometryMode::Parse:
        parse_.reset(new FaceParse(const_cast<char *>(
            model_path(model_dir_, "face_parse.kmodel").c_str()),
            frame_size_, debug_mode_));
        break;
    case GeometryMode::Alignment:
        alignment_.reset(new FaceAlignment(
            const_cast<char *>(model_path(
                model_dir_, "face_alignment.kmodel").c_str()),
            const_cast<char *>(model_path(
                model_dir_, "face_alignment_post.kmodel").c_str()),
            frame_size_, debug_mode_));
        break;
    case GeometryMode::Count:
        break;
    }
    active_mode_ = next;
}

void GeometryStudio::apply_requested_mode()
{
    const GeometryMode next = requested_mode_.load();
    if (next == active_mode_)
        return;
    load_mode(next);
    std::cout << "[face-geometry] switched mode: "
              << geometry_mode_name(active_mode_) << std::endl;
}

int GeometryStudio::process(runtime_tensor &input, cv::Mat &osd)
{
    std::vector<dshanpi_vaxp_ai_pose_t> wire_poses;
    std::vector<std::vector<dshanpi_vaxp_ai_keypoint_t>> wire_keypoints;
    std::vector<std::string> wire_labels;
    std::vector<dshanpi_vaxp_ai_detection_t> wire_segments;
    faces_.clear();
    detector_.pre_process(input);
    detector_.inference();
    detector_.post_process(frame_size_, faces_);
    osd.setTo(cv::Scalar(0, 0, 0, 0));
    wire_poses.reserve(faces_.size());
    wire_keypoints.reserve(faces_.size());
    wire_labels.reserve(faces_.size());
    wire_segments.reserve(faces_.size());
    for (auto &face : faces_) {
        switch (active_mode_) {
        case GeometryMode::Pose: {
            FacePoseInfo result;
            pose_->pre_process(input, face.bbox);
            pose_->inference();
            pose_->post_process(result);
            pose_->draw_result(osd, face.bbox, result, false);
            {
                char text[112];
                std::snprintf(text, sizeof(text),
                              "roll=%.3f,yaw=%.3f,pitch=%.3f",
                              result.roll, result.yaw, result.pitch);
                wire_labels.emplace_back(text);
            }
            wire_poses.push_back({
                0, face.score,
                face.bbox.x, face.bbox.y, face.bbox.w, face.bbox.h,
                nullptr, 0, wire_labels.back().c_str()});
            break;
        }
        case GeometryMode::Mesh: {
            std::vector<float> vertices;
            mesh_->pre_process(input, face.bbox);
            mesh_->inference();
            mesh_->post_process({OSD_CHANNEL, OSD_HEIGHT, OSD_WIDTH},
                                vertices, false);
            mesh_->get_mesh(osd, vertices, false);
            {
                const size_t vertex_count = vertices.size() / 3u;
                const size_t sample_count = std::min<size_t>(64, vertex_count);
                std::vector<dshanpi_vaxp_ai_keypoint_t> points;
                points.reserve(sample_count);
                for (size_t point = 0; point < sample_count; ++point) {
                    const size_t source = point * vertex_count / sample_count;
                    points.push_back({vertices[source],
                                      vertices[source + vertex_count], 1.0f});
                }
                wire_keypoints.push_back(std::move(points));
            }
            wire_labels.emplace_back("face mesh");
            wire_poses.push_back({
                0, face.score,
                face.bbox.x, face.bbox.y, face.bbox.w, face.bbox.h,
                wire_keypoints.back().data(), wire_keypoints.back().size(),
                wire_labels.back().c_str()});
            break;
        }
        case GeometryMode::Parse:
            parse_->pre_process(input, face.bbox);
            parse_->inference();
            parse_->post_process(osd, face.bbox, false);
            wire_segments.push_back({
                0, 0, face.score,
                face.bbox.x, face.bbox.y, face.bbox.w, face.bbox.h,
                0, "face"});
            break;
        case GeometryMode::Alignment: {
            std::vector<float> vertices;
            alignment_->pre_process(input, face.bbox);
            alignment_->inference();
            alignment_->post_process(
                {OSD_CHANNEL, OSD_HEIGHT, OSD_WIDTH}, vertices, false);
            alignment_->get_pncc(osd, vertices);
            {
                const size_t vertex_count = vertices.size() / 3u;
                const size_t sample_count = std::min<size_t>(64, vertex_count);
                std::vector<dshanpi_vaxp_ai_keypoint_t> points;
                points.reserve(sample_count);
                for (size_t point = 0; point < sample_count; ++point) {
                    const size_t source = point * vertex_count / sample_count;
                    points.push_back({vertices[source],
                                      vertices[source + vertex_count], 1.0f});
                }
                wire_keypoints.push_back(std::move(points));
            }
            wire_labels.emplace_back("face alignment");
            wire_poses.push_back({
                0, face.score,
                face.bbox.x, face.bbox.y, face.bbox.w, face.bbox.h,
                wire_keypoints.back().data(), wire_keypoints.back().size(),
                wire_labels.back().c_str()});
            break;
        }
        case GeometryMode::Count:
            break;
        }
    }
    const uint16_t mode_id = static_cast<uint16_t>(active_mode_) + 1u;
    if (active_mode_ == GeometryMode::Parse) {
        dshanpi_vaxp_ai_publish_segments(
            static_cast<uint16_t>(0x0200u + mode_id),
            static_cast<uint16_t>(0x0200u + mode_id),
            geometry_mode_name(active_mode_), 0,
            wire_segments.data(), wire_segments.size());
    } else {
        dshanpi_vaxp_ai_publish_poses(
            static_cast<uint16_t>(0x0200u + mode_id),
            static_cast<uint16_t>(0x0200u + mode_id),
            geometry_mode_name(active_mode_), 0,
            wire_poses.data(), wire_poses.size());
    }
    return 0;
}
