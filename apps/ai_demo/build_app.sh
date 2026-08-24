#!/bin/bash

# Get the full path of this script
SCRIPT=$(realpath -s "$0")
SCRIPTPATH=$(dirname "$SCRIPT")

# Define SDK_SRC_ROOT_DIR as the base root directory
export SDK_SRC_ROOT_DIR=$(realpath "${SCRIPTPATH}/../../../../../")

# Define other paths relative to SDK_SRC_ROOT_DIR
export SDK_RTSMART_SRC_DIR="${SDK_SRC_ROOT_DIR}/src/rtsmart/"
export MPP_SRC_DIR="${SDK_RTSMART_SRC_DIR}/mpp/"
export FREETYPE_SRC_DIR="${SDK_RTSMART_SRC_DIR}/libs/3rd-party/freetype/"
export NNCASE_SRC_DIR="${SDK_RTSMART_SRC_DIR}/libs/nncase/"
export OPENCV_SRC_DIR="${SDK_RTSMART_SRC_DIR}/libs/opencv/"
export AI_POC_RESOURCE_DIR="${SCRIPTPATH}/resources/ai_poc"

# set cross build toolchain
export PATH="${PATH}:${K230_TOOLCHAIN_BIN:-${HOME}/.kendryte/k230_toolchains/riscv64-linux-musleabi_for_x86_64-pc-linux-gnu/bin}"

rm -rf k230_bin

# assemble all test cases
k230_bin=`pwd`/k230_bin
mkdir -p ${k230_bin}
build_out=`pwd`/build
mkdir -p ${build_out}

subdirs=$(find . -mindepth 1 -maxdepth 1 -type d)

if [ "$#" -eq 0 ]; then
    selected_projects="all"
else
    selected_projects="$*"
fi
echo "Selected AI projects: ${selected_projects}"

for subdir in $(ls -d */); do
        if [ ! -d "$subdir" ]; then
                echo "$subdir 不是一个目录，跳过..."
                continue
        fi 

        subdir_name=$(basename $subdir)
        # 检查子目录是否为目录"A"，如果是，则跳过
        if [ "$subdir_name" = "cmake" ] || [[ "$subdir_name" = "k230_bin" ]] || [ "$subdir_name" = "shell" ] || [ "$subdir_name" = "build" ] || [ "$subdir_name" = "common_files" ] || [ "$subdir_name" = "libs" ] || [ "$subdir_name" = ".git" ]; then
                continue
        fi

        if [ "$selected_projects" = "all" ] ||
           [[ " ${selected_projects} " == *" ${subdir_name} "* ]]; then
                set -x
                build_dir="${build_out}/${subdir_name}"

                mkdir -p "${build_dir}"

                pushd "${build_dir}"

                echo "******************$subdir_name 开始编译******************"

                # 只在第一次生成 build_dir 时调用 cmake
                if [ ! -f "${build_dir}/CMakeCache.txt" ]; then
                cmake \
                        -DCMAKE_BUILD_TYPE=Release \
                        -DCMAKE_INSTALL_PREFIX="${build_dir}" \
                        -DCMAKE_TOOLCHAIN_FILE="${SCRIPTPATH}/cmake/Riscv64.cmake" \
                        -D${subdir_name}=ON \
                        "${SCRIPTPATH}"
                fi

                # 增量编译
                if make -C "${build_dir}" -j"${BUILD_JOBS:-2}"; then
                        make -C "${build_dir}" install
                        echo "******************$subdir_name 编译完成******************"
                else
                        echo "******************$subdir_name 编译失败******************"
                fi

                popd
                set +x
        else
                continue
        fi


        if [ "$subdir_name" = "face_detection" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/face_detection.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_detection_320.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_detection_640.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/1024x624.jpg ${k230_bin}/$subdir_name
                cp -a shell/face_detect_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "anomaly_det" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/anomaly_det.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/anomaly_det.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/000.png ${k230_bin}/$subdir_name 
                cp -a ${AI_POC_RESOURCE_DIR}/utils/memory.bin ${k230_bin}/$subdir_name
                cp -a shell/anomaly_det_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "bytetrack" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/bytetrack.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/bytetrack_yolov5n.kmodel ${k230_bin}/$subdir_name
                cp -a shell/bytetrack_isp.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "crosswalk_detect" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/crosswalk_detect.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/crosswalk.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/cw.jpg ${k230_bin}/$subdir_name 
                cp -a shell/crosswalk_detect_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "distraction_reminder" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/distraction_reminder.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_detection_320.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_pose.kmodel ${k230_bin}/$subdir_name
                cp -a shell/distraction_reminder_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "dms_system" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/dms.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_detection_320.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/hand_det.kmodel ${k230_bin}/$subdir_name
                cp -a shell/dms_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "dynamic_gesture" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/dynamic_gesture.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/hand_det.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/handkp_det.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/gesture.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/utils/shang.bin ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/utils/xia.bin ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/utils/zuo.bin ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/utils/you.bin ${k230_bin}/$subdir_name
                cp -a shell/gesture*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "eye_gaze" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/eye_gaze.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_detection_320.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/eye_gaze.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/1024x1111.jpg ${k230_bin}/$subdir_name 
                cp -a shell/eye_gaze_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "face_alignment" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/face_alignment.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_detection_320.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_alignment.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_alignment_post.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/1024x768.jpg ${k230_bin}/$subdir_name 
                cp -a ${AI_POC_RESOURCE_DIR}/utils/bfm_tri.bin ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/utils/ncc_code.bin ${k230_bin}/$subdir_name
                cp -a shell/face_alignment_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "face_emotion" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/face_emotion.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_detection_320.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_emotion.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/1024x768.jpg ${k230_bin}/$subdir_name
                cp -a shell/face_emotion_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "face_gender" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/face_gender.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_detection_320.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_gender.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/1024x768.jpg ${k230_bin}/$subdir_name
                cp -a shell/face_gender_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "face_glasses" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/face_glasses.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_detection_320.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_glasses.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/1024x768.jpg ${k230_bin}/$subdir_name
                cp -a shell/face_glasses_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "face_landmark" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/face_landmark.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_detection_320.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_landmark.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/1024x1331.jpg ${k230_bin}/$subdir_name
                cp -a shell/face_landmark_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "face_mask" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/face_mask.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_detection_320.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_mask.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/1024x768.jpg ${k230_bin}/$subdir_name
                cp -a shell/face_mask_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "face_mesh" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/face_mesh.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_detection_320.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_alignment.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_alignment_post.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/1024x768.jpg ${k230_bin}/$subdir_name 
                cp -a ${AI_POC_RESOURCE_DIR}/utils/bfm_tri.bin ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/utils/ncc_code.bin ${k230_bin}/$subdir_name
                cp -a shell/face_mesh_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "face_parse" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/face_parse.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_detection_320.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_parse.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/1024x768.jpg ${k230_bin}/$subdir_name
                cp -a shell/face_parse_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "face_pose" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/face_pose.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_detection_320.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_pose.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/1024x768.jpg ${k230_bin}/$subdir_name
                cp -a shell/face_pose_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "face_verification" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/face_verification.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_detection_320.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/face_recognition.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/identification_card.png ${k230_bin}/$subdir_name 
                cp -a ${AI_POC_RESOURCE_DIR}/images/person.png ${k230_bin}/$subdir_name 
                cp -a shell/face_verification_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "falldown_detect" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/falldown_detect.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/yolov5n-falldown.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/falldown_elder.jpg ${k230_bin}/$subdir_name            
                cp -a shell/falldown_detect_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "finger_guessing" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/finger_guessing.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/hand_det.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/handkp_det.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/utils/shitou.bin ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/utils/bu.bin ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/utils/jiandao.bin ${k230_bin}/$subdir_name
                cp -a shell/finger_guessing_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "fitness" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/fitness.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/yolov8n-pose.kmodel ${k230_bin}/$subdir_name
                cp -a shell/fitness_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "head_detection" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/head_detection.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/head_detection.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/640x340.jpg ${k230_bin}/$subdir_name
                cp -a shell/head_detect_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "helmet_detect" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/helmet_detect.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/helmet.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/helmet.jpg ${k230_bin}/$subdir_name 
                cp -a shell/helmet_detect_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "licence_det" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/licence_det.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/LPD_640.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/licence.jpg ${k230_bin}/$subdir_name
                cp -a shell/licence_detect_image.sh ${k230_bin}/$subdir_name
                cp -a shell/licence_detect_isp.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "licence_det_rec" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/licence_det_rec.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/LPD_640.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/licence_reco.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/licence.jpg ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/utils/SourceHanSansSC-Normal-Min.ttf  ${k230_bin}/$subdir_name
                cp -a shell/licence_detect_rec_image.sh ${k230_bin}/$subdir_name
                cp -a shell/licence_detect_rec_isp.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "object_detect_yolov8n" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/ob_det.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/yolov8n_320.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/yolov8n_640.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/bus.jpg ${k230_bin}/$subdir_name            
                cp -a shell/ob_detect_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "ocr" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/ocr_reco.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/ocr_det_int16.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/ocr_rec_int16.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/333.jpg ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/utils/dict_ocr.txt  ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/utils/SourceHanSansSC-Normal-Min.ttf  ${k230_bin}/$subdir_name
                cp -a shell/ocr_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "person_detect" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/person_detect.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/person_detect_yolov5n.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/bus.jpg ${k230_bin}/$subdir_name            
                cp -a shell/person_detect_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "person_attr" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/person_attr.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/person_attr_yolov5n.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/person_pulc.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/hrnet_demo.jpg ${k230_bin}/$subdir_name            
                cp -a shell/person_attr_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "pose_detect" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/pose_detect.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/yolov8n-pose.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/bus.jpg ${k230_bin}/$subdir_name            
                cp -a shell/pose_detect_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "person_distance" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/person_distance.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/person_detect_yolov5n.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/input_pd.jpg ${k230_bin}/$subdir_name
                cp -a shell/person_distance_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "pphumanseg" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/pphumanseg.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/human_seg_2023mar.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/1000.jpg ${k230_bin}/$subdir_name            
                cp -a shell/pphumanseg_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "segment_yolov8n" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/seg.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/yolov8n_seg_320.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/yolov8n_seg_640.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/bus.jpg ${k230_bin}/$subdir_name 
                cp -a shell/segment_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "yolop_lane_seg" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/yolop.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/yolop.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/road.jpg ${k230_bin}/$subdir_name
                cp -a shell/yolop_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "vehicle_attr" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/vehicle_attr.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/vehicle_attr_yolov5n.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/vehicle.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/car.jpg ${k230_bin}/$subdir_name            
                cp -a shell/vehicle_attr_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "traffic_light_detect" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/traffic_light_detect.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/traffic.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/traffic.jpg ${k230_bin}/$subdir_name 
                cp -a shell/traffic_light_detect_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "smoke_detect" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/smoke_detect.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/yolov5s_smoke_best.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/smoke1.jpg ${k230_bin}/$subdir_name 
                cp -a shell/smoke_detect_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "sq_hand_det" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/sq_hand_det.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/hand_det.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/input_hd.jpg ${k230_bin}/$subdir_name
                cp -a shell/handdet_cpp_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "sq_handkp_class" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/sq_handkp_class.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/hand_det.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/handkp_det.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/input_hd.jpg ${k230_bin}/$subdir_name
                cp -a shell/handkpclass_cpp_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "sq_handkp_det" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/sq_handkp_det.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/hand_det.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/handkp_det.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/input_hd.jpg ${k230_bin}/$subdir_name
                cp -a shell/handkpdet_cpp_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "sq_handreco" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/sq_handreco.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/hand_det.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/hand_reco.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/input_hd.jpg ${k230_bin}/$subdir_name
                cp -a shell/handreco_cpp_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "puzzle_game" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/puzzle_game.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/hand_det.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/handkp_det.kmodel ${k230_bin}/$subdir_name
                cp -a shell/puzzle_game_isp.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "nanotracker" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/nanotracker.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/cropped_test127.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/nanotrack_backbone_sim.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/nanotracker_head_calib_k230.kmodel ${k230_bin}/$subdir_name
                cp -a shell/nanotracker_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "kws" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/kws.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/kws.kmodel ${k230_bin}/$subdir_name
                cp -ar ${AI_POC_RESOURCE_DIR}/utils/reply_wav/ ${k230_bin}/$subdir_name
                cp -a shell/kws.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "tts_zh" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/tts_zh.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/zh_fastspeech_1.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/zh_fastspeech_2.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/hifigan.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/utils/wav_play.elf ${k230_bin}/$subdir_name 
                cp -ar ${AI_POC_RESOURCE_DIR}/utils/file ${k230_bin}/$subdir_name 
                cp -a shell/tts_zh.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "translate_en_ch" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/translate_en_ch.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/translate_encoder.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/translate_decoder.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/utils/trans_src.model ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/utils/trans_tag.model ${k230_bin}/$subdir_name
                cp -a shell/translate_en_ch.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "space_resize" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/space_resize.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/hand_det.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/handkp_det.kmodel ${k230_bin}/$subdir_name
                cp -a shell/space_resize_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "virtual_keyboard" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/virtual_keyboard.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/hand_det.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/handkp_det.kmodel ${k230_bin}/$subdir_name
                cp -a shell/virtual_keyboard.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "self_learning" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/self_learning.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/recognition.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/cropped_test127.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/nanotrack_backbone_sim.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/nanotracker_head_calib_k230.kmodel ${k230_bin}/$subdir_name
                cp -a shell/self_learning.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "sq_handkp_flower" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/sq_handkp_flower.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/hand_det.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/handkp_det.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/flower_rec.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/input_flower.jpg ${k230_bin}/$subdir_name
                cp -a shell/handkpflower_*.sh ${k230_bin}/$subdir_name
        fi

        if [ "$subdir_name" = "sq_handkp_ocr" ]; then
                mkdir -p ${k230_bin}/$subdir_name
                cp ${build_dir}/bin/sq_handkp_ocr.elf ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/hand_det.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/handkp_det.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/ocr_det_int16.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/kmodel/ocr_rec_int16.kmodel ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/utils/dict_ocr.txt  ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/utils/SourceHanSansSC-Normal-Min.ttf  ${k230_bin}/$subdir_name
                cp -a ${AI_POC_RESOURCE_DIR}/images/handkp_ocr_img.jpg ${k230_bin}/$subdir_name
                cp -a shell/handkpocr_cpp_*.sh ${k230_bin}/$subdir_name
        fi
done
