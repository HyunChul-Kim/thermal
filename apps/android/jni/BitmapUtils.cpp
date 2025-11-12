#include "thermal/jni/BitmapUtils.hpp"
#include <android/bitmap.h>

namespace bmp
{
    static inline void throwIf(JNIEnv *env, bool cond, const char *msg)
    {
        if (cond)
        {
            jclass ex = env->FindClass("java/lang/RuntimeException");
            env->ThrowNew(ex, msg);
        }
    }

    jobject matToBitmap(JNIEnv *env, const cv::Mat &rgba)
    {
        // rgba: CV_8UC4 (R,G,B,A)
        // Bitmap config ARGB_8888는 메모리상 BGRA가 일반적이니 채널 스왑 필요 여부 확인
        jclass bitmapCls = env->FindClass("android/graphics/Bitmap");
        jclass configCls = env->FindClass("android/graphics/Bitmap$Config");
        jmethodID valueOf = env->GetStaticMethodID(configCls, "valueOf", "(Ljava/lang/String;)Landroid/graphics/Bitmap$Config;");
        jstring argb = env->NewStringUTF("ARGB_8888");
        jobject config = env->CallStaticObjectMethod(configCls, valueOf, argb);
        env->DeleteLocalRef(argb);

        jmethodID create = env->GetStaticMethodID(bitmapCls, "createBitmap", "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;");
        jobject bmp = env->CallStaticObjectMethod(bitmapCls, create, rgba.cols, rgba.rows, config);
        env->DeleteLocalRef(config);

        AndroidBitmapInfo info;
        void *pixels = nullptr;
        AndroidBitmap_getInfo(env, bmp, &info);
        AndroidBitmap_lockPixels(env, bmp, &pixels);
        // OpenCV RGBA -> Android ARGB
        // 여기선 같은 메모리 순서를 가정하고 memcpy, 플랫폼에 따라 필요시 채널 스왑
        uint8_t *dst = static_cast<uint8_t *>(pixels);
        if (info.stride == (uint32_t)rgba.cols * 4)
        {
            std::memcpy(dst, rgba.data, rgba.total() * 4);
        }
        else
        {
            for (int y = 0; y < rgba.rows; ++y)
            {
                std::memcpy(dst + y * info.stride, rgba.ptr(y), rgba.cols * 4);
            }
        }
        AndroidBitmap_unlockPixels(env, bmp);
        return bmp;
    }

    cv::Mat bitmapToMat(JNIEnv *env, jobject bitmap)
    {
        AndroidBitmapInfo info{};
        int rc = AndroidBitmap_getInfo(env, bitmap, &info);
        throwIf(env, rc != ANDROID_BITMAP_RESULT_SUCCESS, "AndroidBitmap_getInfo failed");
        throwIf(env, info.format != ANDROID_BITMAP_FORMAT_RGBA_8888, "Bitmap must be ARGB_8888");

        void *pixels = nullptr;
        rc = AndroidBitmap_lockPixels(env, bitmap, &pixels);
        throwIf(env, rc != ANDROID_BITMAP_RESULT_SUCCESS, "AndroidBitmap_lockPixels failed");

        cv::Mat rgba(info.height, info.width, CV_8UC4);
        // Android 저장 포맷: ARGB_8888(메모리상은 RGBA와 같은 4바이트 폭)
        // 여기선 그대로 복사해서 CV_8UC4로 취급 (필요하면 채널 swap 추가)
        std::memcpy(rgba.data, pixels, size_t(info.height) * info.stride);

        AndroidBitmap_unlockPixels(env, bitmap);
        return rgba;
    }
}
