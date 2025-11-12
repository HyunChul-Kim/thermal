#pragma once
#include <jni.h>
#include <opencv2/core.hpp>

namespace bmp
{
    jobject matToBitmap(JNIEnv *env, const cv::Mat &rgba); // ARGB_8888 Bitmap 반환
    cv::Mat bitmapToMat(JNIEnv *env, jobject bitmap);
}
