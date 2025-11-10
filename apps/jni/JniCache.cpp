#include "thermal/jni/JniCache.hpp"
#include "thermal/jni/BitmapUtils.hpp" // cv::Mat -> Bitmap 변환 유틸
#include <string>

namespace jni
{

    static Cache g_cache;
    Cache &cache() { return g_cache; }

    static jclass findGlobal(JNIEnv *env, const char *name)
    {
        jclass local = env->FindClass(name);
        if (!local)
            return nullptr;
        jclass global = (jclass)env->NewGlobalRef(local);
        env->DeleteLocalRef(local);
        return global;
    }

    bool Cache::init(JNIEnv *env)
    {
        // ArrayList
        cls_ArrayList = findGlobal(env, "java/util/ArrayList");
        ctor_ArrayList = env->GetMethodID(cls_ArrayList, "<init>", "()V");
        mid_ArrayList_add = env->GetMethodID(cls_ArrayList, "add", "(Ljava/lang/Object;)Z");

        // StagePayload
        cls_StagePayload = findGlobal(env, "com/chul/thermalimaging/model/NativeStagePayload");
        // 예: (Landroid/graphics/Bitmap;FIF)V  -> Bitmap, percent(float), labelId(int), thresholdQ(float)
        // 실제 시그니처를 앱 코드와 반드시 맞춰줘!
        ctor_StagePayload = env->GetMethodID(
            cls_StagePayload, "<init>",
            "(Landroid/graphics/Bitmap;FIF)V");

        // NativeResult
        cls_NativeResult = findGlobal(env, "com/chul/thermalimaging/model/NativeResult");
        // 예: (Ljava/util/List;IILjava/lang/String;)V -> payloads, usedK, status, message
        ctor_NativeResult = env->GetMethodID(
            cls_NativeResult, "<init>",
            "(Ljava/util/List;IILjava/lang/String;)V");

        return cls_ArrayList && ctor_ArrayList && mid_ArrayList_add &&
               cls_StagePayload && ctor_StagePayload &&
               cls_NativeResult && ctor_NativeResult;
    }

    jobject buildNativeResult(JNIEnv *env, const thermal::Result &res)
    {
        auto &c = cache();

        // 1) payloads: ArrayList<NativeStagePayload>
        jobject jList = env->NewObject(c.cls_ArrayList, c.ctor_ArrayList);

        const int n = (int)res.stages.size();
        for (int i = 0; i < n; ++i)
        {
            const auto &pl = res.stages[i];

            // cv::Mat -> Bitmap (ARGB_8888)
            jobject jBmp = bmp::matToBitmap(env, pl.rgba); // 구현돼 있다고 가정

            // NewObject로 StagePayload 생성
            // 시그니처가 "(Landroid/graphics/Bitmap;FIF)V" 라는 가정: (bitmap, percent, labelId, thresholdQ)
            jobject jPayload = env->NewObject(
                c.cls_StagePayload, c.ctor_StagePayload,
                jBmp,
                (jfloat)pl.mortarPermille,
                (jint)pl.labelId,
                (jfloat)pl.thresholdQ);

            env->CallBooleanMethod(jList, c.mid_ArrayList_add, jPayload);

            // 로컬 참조 정리
            env->DeleteLocalRef(jBmp);
            env->DeleteLocalRef(jPayload);
        }

        // 2) NativeResult 생성
        jstring jMessage = env->NewStringUTF(res.message.c_str());
        jobject jRes = env->NewObject(
            c.cls_NativeResult, c.ctor_NativeResult,
            jList,
            (jint)res.usedK,
            (jint)res.status,
            jMessage);

        env->DeleteLocalRef(jList);
        env->DeleteLocalRef(jMessage);
        return jRes;
    }

} // namespace jni
