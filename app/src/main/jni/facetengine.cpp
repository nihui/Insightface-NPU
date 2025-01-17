//
// Created by bli on 2021/7/16.
//

#include <android/asset_manager_jni.h>
#include <android/native_window_jni.h>
#include <android/native_window.h>
#include <android/log.h>
#include <jni.h>
#include <string>
#include <vector>

#include "ndkcamera.h"

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui.hpp>

#include "AndroidLog.h"
#include "scrfd.hpp"
#include "utility/timer.hpp"
#include <algorithm>
#include <numeric>

#if __ARM_NEON

#include <arm_neon.h>

#endif // __ARM_NEON

#define VXDEVICE "VX"
const int MODEL_WIDTH = 640;
const int MODEL_HEIGHT = 384;

const float DEFAULT_SCORE_THRESHOLD = 0.45f;
const float DEFAULT_NMS_THRESHOLD = 0.3f;

static SCRFD *scrfdFace;


//Draw Face
static int drawFace(cv::Mat &rgb, std::vector<region> &faces) {
    for (auto &face : faces) {
        fprintf(stderr, "%.5f at %.2f %.2f %.2f x %.2f\n", face.confidence, face.box.x, face.box.y,
                face.box.w, face.box.h);
        // box
        cv::Rect2f rect(face.box.x, face.box.y, face.box.w, face.box.h);
        cv::Mat image_flip = rgb;
        // draw box
        cv::rectangle(image_flip, rect, cv::Scalar(0, 0, 255), 2);
        std::string box_confidence = "DET: " + std::to_string(face.confidence).substr(0, 5);
        cv::putText(image_flip, box_confidence, rect.tl() + cv::Point2f(5, -10),
                    cv::FONT_HERSHEY_TRIPLEX, 0.6f, cv::Scalar(255, 255, 0));

        cv::circle(image_flip, cv::Point(face.landmark[0].x, face.landmark[0].y), 2,
                   cv::Scalar(255, 255, 0), -1);
        cv::circle(image_flip, cv::Point(face.landmark[1].x, face.landmark[1].y), 2,
                   cv::Scalar(255, 255, 0), -1);
        cv::circle(image_flip, cv::Point(face.landmark[2].x, face.landmark[2].y), 2,
                   cv::Scalar(255, 255, 0), -1);
        cv::circle(image_flip, cv::Point(face.landmark[3].x, face.landmark[3].y), 2,
                   cv::Scalar(255, 255, 0), -1);
        cv::circle(image_flip, cv::Point(face.landmark[4].x, face.landmark[4].y), 2,
                   cv::Scalar(255, 255, 0), -1);
    }
    return 0;
}


class FaceNdkCamera : public NdkCameraWindow {
public:
    virtual void on_image_render(cv::Mat &rgb) const;
};

static FaceNdkCamera *gCamera = 0;

/*
 * Do Detect
 */
void FaceNdkCamera::on_image_render(cv::Mat &rgb) const {
    if (scrfdFace) {
        auto score_threshold = DEFAULT_SCORE_THRESHOLD;
        auto iou_threshold = DEFAULT_NMS_THRESHOLD;
        std::vector<region> faces;
        cv::Mat image_flip;
        image_flip = rgb;
        Timer det_timer;
//        LOGD("Start detect face");
        scrfdFace->Detect(image_flip, faces, score_threshold, iou_threshold);
//        LOGD("Successfully detect face");
        double cost_time = det_timer.TimeCost();
//        LOGD("CostTime %.2f", cost_time);
        drawFace(image_flip, faces);

    }
}


/*
 * JNI Code
 */
extern "C" {

char *model_path = NULL;
char *device = NULL;


JNIEXPORT void JNICALL
Java_com_oal_insightface_FaceTengine_release(JNIEnv *env, jclass) {
    if (scrfdFace) {
        delete scrfdFace;
    }

}

/*
 * Initial Camera
 */
JNIEXPORT jint
JNI_OnLoad(JavaVM
           *vm,
           void *reserved
) {
    LOGD("JNI_OnLoad");
    gCamera = new FaceNdkCamera;
    gCamera->camera_facing = 1; //TIM3
    return
            JNI_VERSION_1_4;
}

JNIEXPORT void JNI_OnUnload(JavaVM *vm, void *reserved) {
    LOGD("JNI_OnUnload");
    {
        if (scrfdFace) {
            delete scrfdFace;
        }
    }
    delete gCamera;
    gCamera = 0;
}

/*
 * Load Tenigne Model
 */
JNIEXPORT jboolean
JNICALL
Java_com_oal_insightface_FaceTengine_loadModel(JNIEnv *env, jobject
thiz, int cpuNpu) {
    init_tengine();

    cv::Size input_shape(MODEL_WIDTH, MODEL_HEIGHT);
    scrfdFace = new SCRFD();

    if (cpuNpu) {
        model_path = "/sdcard/OAL/scrfd_2.5g_bnkps_uint8.tmfile";
        device = "TIMVX";
    } else {
        model_path = "/sdcard/OAL/scrfd_2.5g_bnkps_sim.tmfile";
        device = "CPU";
    }

    auto ret = scrfdFace->Load(model_path, input_shape, device);
    if (!ret) {
        return
                JNI_FALSE;
    }
    LOGD("Successfully loaded model");

    return
            JNI_TRUE;
}


// public native boolean openCamera(int facing);
JNIEXPORT jboolean
JNICALL
Java_com_oal_insightface_FaceTengine_openCamera(JNIEnv *env, jobject
thiz,
                                                jint facing
) {
    if (facing < 0 || facing > 1)
        return
                JNI_FALSE;
    LOGD("openCamera %d", facing);
    gCamera->open((int) facing);
    return
            JNI_TRUE;
}

// public native boolean closeCamera();
JNIEXPORT jboolean
JNICALL
Java_com_oal_insightface_FaceTengine_closeCamera(JNIEnv *env, jobject
thiz) {
    LOGD("closeCamera");
    gCamera->

            close();

    return
            JNI_TRUE;
}

// public native boolean setOutputWindow(Surface surface);
JNIEXPORT jboolean
JNICALL
Java_com_oal_insightface_FaceTengine_setOutputWindow(JNIEnv *env, jobject
thiz, jobject surface) {
    ANativeWindow *win = ANativeWindow_fromSurface(env, surface);
    LOGD("setOutputWindow %p", win);
    gCamera->set_window(win);

    return
            JNI_TRUE;
}

}
