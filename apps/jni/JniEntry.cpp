#include <jni.h>
#include <android/log.h>
#include "thermal/core.hpp"
#include "thermal/jni/BitmapUtils.hpp"
#include "thermal/jni/JniConfig.hpp"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "thermal", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "thermal", __VA_ARGS__)

static jobject native_processStagesWithRoi(JNIEnv *env, jclass,
                                           jobject inBitmap,
                                           jintArray xs, jintArray ys,
                                           jboolean doBilateral,
                                           jint stageSteps, jint maxK,
                                           jboolean refineMode,
                                           jint refineSteps,
                                           jint stageIdx)
{
    // 1) Bitmap -> cv::Mat
    cv::Mat rgba = bmp::bitmapToMat(env, inBitmap); // ARGB_8888 보장 가정

    // 2) ROI
    std::optional<thermal::Polygon> roi;
    if (xs && ys)
    {
        jsize nx = env->GetArrayLength(xs), ny = env->GetArrayLength(ys);
        if (nx > 0 && nx == ny)
        {
            std::vector<int> vx(nx), vy(ny);
            env->GetIntArrayRegion(xs, 0, nx, vx.data());
            env->GetIntArrayRegion(ys, 0, ny, vy.data());
            roi = thermal::Polygon{std::move(vx), std::move(vy)};
        }
    }

    // 3) Params 채우기
    thermal::Params p{};
    p.doBilateral = doBilateral;
    p.stageSteps = stageSteps;
    p.maxK = maxK;
    p.refineMode = refineMode;
    p.refineSteps = refineSteps;
    p.stageIdx = stageIdx;

    // 4) 코어 호출
    auto R = thermal::segmentTempGroups(rgba, roi, p, /*needLabelIds=*/false);

    // 5) Result -> Java NativeResult(List<NativeStagePayload> payloads, usedK, status, message)
    //    payload: (Bitmap bitmap, float mortarPercent, int labelId, float thresholdQ)
    //    -> JNI로 List & 객체 생성
    jclass clsPayload = env->FindClass("com/chul/thermalimaging/model/NativeStagePayload");
    jclass clsResult = env->FindClass("com/chul/thermalimaging/model/NativeResult");
    if (!clsPayload || !clsResult)
    {
        LOGE("FindClass fail");
        return nullptr;
    }

    jmethodID ctorPayload = env->GetMethodID(clsPayload, "<init>", "(Landroid/graphics/Bitmap;FIF)V");
    jmethodID ctorResult = env->GetMethodID(clsResult, "<init>", "(Ljava/util/List;IILjava/lang/String;)V");
    jclass clsArrayList = env->FindClass("java/util/ArrayList");
    jmethodID ctorArrList = env->GetMethodID(clsArrayList, "<init>", "(I)V");
    jmethodID addArrList = env->GetMethodID(clsArrayList, "add", "(Ljava/lang/Object;)Z");

    jobject list = env->NewObject(clsArrayList, ctorArrList, (jint)R.stages.size());

    for (const auto &pl : R.stages)
    {
        jobject bmp = bmp::matToBitmap(env, pl.rgba); // cv::Mat -> Bitmap
        jobject payload = env->NewObject(clsPayload, ctorPayload,
                                         bmp,
                                         (jfloat)pl.mortarPermille,
                                         (jint)pl.labelId,
                                         (jfloat)pl.thresholdQ);
        env->CallBooleanMethod(list, addArrList, payload);
        env->DeleteLocalRef(payload);
        env->DeleteLocalRef(bmp);
    }
    jstring jmsg = env->NewStringUTF(R.message.c_str());
    jobject out = env->NewObject(clsResult, ctorResult, list, (jint)R.usedK, (jint)R.status, jmsg);
    env->DeleteLocalRef(jmsg);
    env->DeleteLocalRef(list);
    return out;
}

// (선택) 앱에서 런타임로 클래스 경로 바꾸기 위한 native API
static void native_setClassPath(JNIEnv *env, jclass, jstring jpath)
{
    const char *p = jpath ? env->GetStringUTFChars(jpath, nullptr) : nullptr;
    JniConfig::setTargetClassPath(p);
    if (p)
        env->ReleaseStringUTFChars(jpath, p);
}

// RegisterNatives
static JNINativeMethod gMethods[] = {
    {(char *)"processStagesWithRoi",
     (char *)"(Landroid/graphics/Bitmap;[I[IZIIZII)Lcom/chul/thermalimaging/model/NativeResult;",
     (void *)native_processStagesWithRoi},
    {(char *)"setNativeClassPath",
     (char *)"(Ljava/lang/String;)V",
     (void *)native_setClassPath},
};

jint JNI_OnLoad(JavaVM *vm, void *)
{
    JNIEnv *env = nullptr;
    if (vm->GetEnv((void **)&env, JNI_VERSION_1_6) != JNI_OK)
        return JNI_ERR;

    // 기본 클래스 경로로 등록
    jclass clazz = JniConfig::findClassGlobal(env, JniConfig::targetClassPath());
    if (!clazz)
    {
        LOGE("FindClass fail: %s", JniConfig::targetClassPath());
        return JNI_ERR;
    }
    if (env->RegisterNatives(clazz, gMethods, sizeof(gMethods) / sizeof(gMethods[0])) != 0)
    {
        LOGE("RegisterNatives fail");
        return JNI_ERR;
    }
    return JNI_VERSION_1_6;
}
