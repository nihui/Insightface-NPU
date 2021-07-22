// Tencent is pleased to support the open source community by making ncnn available.
//
// Copyright (C) 2021 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

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

#include "AndroidLog.h"
#include "algorithm/scrfd.hpp"
//#include "utility/cmdline.hpp"
#include "utility/timer.hpp"
#include <algorithm>
#include <numeric>

#if __ARM_NEON

#include <arm_neon.h>

#endif // __ARM_NEON

#define VXDEVICE "VX"
const float DET_THRESHOLD = 0.30f;
const float NMS_THRESHOLD = 0.45f;

const int MODEL_WIDTH = 640;
const int MODEL_HEIGHT = 384;

const float DEFAULT_SCORE_THRESHOLD = 0.45f;
const float DEFAULT_NMS_THRESHOLD = 0.3f;


static int draw_face(cv::Mat &rgb, std::vector<region> &faces) {
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
//        cv::rotate(image_flip, image_flip, cv::ROTATE_180);
    }

    return 0;
}

static int draw_unsupported(cv::Mat &rgb) {
    const char text[] = "unsupported";

    int baseLine = 0;
    cv::Size label_size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 1.0, 1, &baseLine);

    int y = (rgb.rows - label_size.height) / 2;
    int x = (rgb.cols - label_size.width) / 2;

    cv::rectangle(rgb, cv::Rect(cv::Point(x, y),
                                cv::Size(label_size.width, label_size.height + baseLine)),
                  cv::Scalar(255, 255, 255), -1);

    cv::putText(rgb, text, cv::Point(x, y + label_size.height),
                cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 0));

    return 0;
}

static int draw_fps(cv::Mat &rgb) {
    // resolve moving average
    float avg_fps = 0.f;
    {
        static double t0 = 0.f;
        static float fps_history[10] = {0.f};

//        double t1 = ncnn::get_current_time();
        double t1 = 0.5;
        if (t0 == 0.f) {
            t0 = t1;
            return 0;
        }

        float fps = 1000.f / (t1 - t0);
        t0 = t1;

        for (int i = 9; i >= 1; i--) {
            fps_history[i] = fps_history[i - 1];
        }
        fps_history[0] = fps;

        if (fps_history[9] == 0.f) {
            return 0;
        }

        for (int i = 0; i < 10; i++) {
            avg_fps += fps_history[i];
        }
        avg_fps /= 10.f;
    }

    char text[32];
    sprintf(text, "FPS=%.2f", avg_fps);

    int baseLine = 0;
    cv::Size label_size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);

    int y = 0;
    int x = rgb.cols - label_size.width;

    cv::rectangle(rgb, cv::Rect(cv::Point(x, y),
                                cv::Size(label_size.width, label_size.height + baseLine)),
                  cv::Scalar(255, 255, 255), -1);

    cv::putText(rgb, text, cv::Point(x, y + label_size.height),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0));

    return 0;
}

static SCRFD *scrfdface;

class MyNdkCamera : public NdkCameraWindow {
public:
    virtual void on_image_render(cv::Mat &rgb) const;
};

void MyNdkCamera::on_image_render(cv::Mat &rgb) const {
    if (scrfdface) {
        auto score_threshold = DEFAULT_SCORE_THRESHOLD;
        auto iou_threshold = DEFAULT_NMS_THRESHOLD;
        std::vector<region> faces;

        cv::Mat image_ori, image_flip;
        image_ori = rgb;
//        Timer det_timer;

        cv::rotate(rgb, rgb, cv::ROTATE_180);
        image_flip = rgb;
//        cv::rotate(image_flip, image_flip, cv::ROTATE_180);
        Timer det_timer;
//        det_timer.Start();
        scrfdface->Detect(image_flip, faces, score_threshold, iou_threshold);
        double cost_time = det_timer.TimeCost();
        __android_log_print(ANDROID_LOG_DEBUG, "CostTime", "CostTime %.2f", cost_time);
//        det_timer.Stop();
        draw_face(image_flip, faces);
    } else {
        draw_unsupported(rgb);
    }
}


static MyNdkCamera *g_camera = 0;

extern "C" {

char *model_path = NULL;
char *device = NULL;

JNIEXPORT void JNICALL
Java_com_oal_insightface_FaceTengine_release(JNIEnv *env, jclass) {

    if (scrfdface) {
        delete scrfdface;
    }

}


JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    __android_log_print(ANDROID_LOG_DEBUG, "Tengine", "JNI_OnLoad");

    g_camera = new MyNdkCamera;

    return JNI_VERSION_1_4;
}

JNIEXPORT void JNI_OnUnload(JavaVM *vm, void *reserved) {
    __android_log_print(ANDROID_LOG_DEBUG, "Tengine", "JNI_OnUnload");

    {
        if (scrfdface) {
            delete scrfdface;
        }
    }

    delete g_camera;
    g_camera = 0;
}


JNIEXPORT jboolean JNICALL
Java_com_oal_insightface_FaceTengine_loadModel(JNIEnv *env, jobject thiz, jint cpugpu) {
    init_tengine();
    __android_log_print(ANDROID_LOG_DEBUG, "TenigneModel", " init_tengine");
    cv::Size input_shape(MODEL_WIDTH, MODEL_HEIGHT);
    scrfdface = new SCRFD();
    if (cpugpu) {
        model_path = "/sdcard/OAL/scrfd_2.5g_bnkps_uint8.tmfile";
        device = "TIMVX";
    } else {
        model_path = "/sdcard/OAL/scrfd_2.5g_bnkps_sim.tmfile";
        device = "CPU";
    }

    auto ret = scrfdface->Load(model_path, input_shape, device);

    if (!ret) {
        return JNI_FALSE;
    }

    return JNI_TRUE;
}


JNIEXPORT jboolean JNICALL
Java_com_oal_insightface_FaceTengine_detectDraw(JNIEnv *env, jobject thiz, jint jw, jint jh,
                                                jintArray jPixArr) {
    jint *cPixArr = env->GetIntArrayElements(jPixArr, JNI_FALSE);
    if (cPixArr == NULL) {
        return JNI_FALSE;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    // 用传入的数组构建Mat，然后从RGBA转成RGB
    cv::Mat mat_image_src(jh, jw, CV_8UC4, (unsigned char *) cPixArr);
    cv::Mat rgb;
    cvtColor(mat_image_src, rgb, cv::COLOR_RGBA2RGB, 3);

    if (scrfdface) {
        auto score_threshold = DEFAULT_SCORE_THRESHOLD;
        auto iou_threshold = DEFAULT_NMS_THRESHOLD;
        std::vector<region> faces;

        cv::Mat image_ori, image_flip;
        image_ori = rgb;
//        Timer det_timer;

//        cv::rotate(rgb, rgb, cv::ROTATE_180);
        image_flip = rgb;
//        cv::rotate(image_flip, image_flip, cv::ROTATE_180);
        Timer det_timer;
//        det_timer.Start();
        scrfdface->Detect(image_flip, faces, score_threshold, iou_threshold);
        double cost_time = det_timer.TimeCost();
        __android_log_print(ANDROID_LOG_DEBUG, "CostTime", "CostTime %.2f", cost_time);
//        det_timer.Stop();
        draw_face(image_flip, faces);
    } else {
        draw_unsupported(rgb);
    }


    // 将Mat从RGB转回去RGBA刷新java数据
    cvtColor(rgb, mat_image_src, cv::COLOR_RGB2RGBA, 4);
    // 释放掉C数组
    env->ReleaseIntArrayElements(jPixArr, cPixArr, 0);

    return JNI_TRUE;
}


// public native boolean openCamera(int facing);
JNIEXPORT jboolean JNICALL
Java_com_oal_insightface_FaceTengine_openCamera(JNIEnv *env, jobject thiz, jint facing) {
    if (facing < 0 || facing > 1)
        return JNI_FALSE;

    __android_log_print(ANDROID_LOG_DEBUG, "Tengine", "openCamera %d", facing);

    g_camera->open((int) facing);

    return JNI_TRUE;
}

// public native boolean closeCamera();
JNIEXPORT jboolean JNICALL
Java_com_oal_insightface_FaceTengine_closeCamera(JNIEnv *env, jobject thiz) {
    __android_log_print(ANDROID_LOG_DEBUG, "Tengine", "closeCamera");

    g_camera->close();

    return JNI_TRUE;
}

// public native boolean setOutputWindow(Surface surface);
JNIEXPORT jboolean JNICALL
Java_com_oal_insightface_FaceTengine_setOutputWindow(JNIEnv *env, jobject thiz, jobject surface) {
    ANativeWindow *win = ANativeWindow_fromSurface(env, surface);

    __android_log_print(ANDROID_LOG_DEBUG, "Tengine", "setOutputWindow %p", win);

    g_camera->set_window(win);

    return JNI_TRUE;
}

}