#ifndef DSHANPI_GEOMETRY_STUDIO_H
#define DSHANPI_GEOMETRY_STUDIO_H
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "face_alignment.h"
#include "face_detection.h"
#include "face_mesh.h"
#include "face_parse.h"
#include "face_pose.h"

enum class GeometryMode { Pose = 0, Mesh, Parse, Alignment, Count };

class GeometryStudio {
public:
    GeometryStudio(const std::string &model_dir, FrameCHWSize frame_size,
                   int debug_mode,
                   GeometryMode initial_mode = GeometryMode::Pose);
    void request_mode(GeometryMode mode);
    GeometryMode requested_mode() const;
    GeometryMode active_mode() const;
    void apply_requested_mode();
    int process(runtime_tensor &input, cv::Mat &osd);
private:
    void load_mode(GeometryMode mode);
    void unload_geometry_model();
    std::string model_dir_;
    FrameCHWSize frame_size_;
    int debug_mode_;
    FaceDetection detector_;
    std::atomic<GeometryMode> requested_mode_;
    GeometryMode active_mode_;
    std::vector<FaceDetectionInfo> faces_;
    std::unique_ptr<FacePose> pose_;
    std::unique_ptr<FaceMesh> mesh_;
    std::unique_ptr<FaceParse> parse_;
    std::unique_ptr<FaceAlignment> alignment_;
};
const char *geometry_mode_name(GeometryMode mode);
#endif
