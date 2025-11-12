#include "thermal/jni/JniConfig.hpp"
#include "thermal/jni/BitmapUtils.hpp"
#include <mutex>

namespace JniConfig
{

    // -----------------------
    // 클래스 경로 보관/설정
    // -----------------------
    static std::string gTargetClassPath = JNI_ENTRY_CLASS; // 기본값(컴파일 타임 매크로)
    static std::mutex gCfgMutex;

    void setTargetClassPath(const char *path)
    {
        std::lock_guard<std::mutex> lk(gCfgMutex);
        if (path && *path)
            gTargetClassPath = path;
    }

    const char *targetClassPath()
    {
        std::lock_guard<std::mutex> lk(gCfgMutex);
        return gTargetClassPath.c_str();
    }

    // -----------------------
    // 클래스 로딩 (GlobalRef)
    // -----------------------
    jclass findClassGlobal(JNIEnv *env, const char *name)
    {
        jclass local = env->FindClass(name);
        if (!local)
            return nullptr;
        jclass global = (jclass)env->NewGlobalRef(local);
        env->DeleteLocalRef(local);
        return global;
    }

    // -----------------------
    // 결과 빌더 (예시 시그니처)
    // -----------------------
    static constexpr const char *kResultClass = "com/chul/thermalimaging/model/NativeResult";
    static constexpr const char *kPayloadClass = "com/chul/thermalimaging/model/NativeStagePayload";
    // NativeResult(List<NativeStagePayload> payloads, int usedK, int status, String message)
    // NativeStagePayload(Bitmap bmp, float mortarPermille, int labelId, float thresholdQ)

    jobject makeErrorResult(JNIEnv *env, int status, const char *msg)
    {
        jclass clsResult = env->FindClass(kResultClass);
        jclass clsList = env->FindClass("java/util/ArrayList");
        jmethodID ctorL = env->GetMethodID(clsList, "<init>", "()V");
        jobject empty = env->NewObject(clsList, ctorL);

        jmethodID ctorR = env->GetMethodID(clsResult, "<init>", "(Ljava/util/List;IILjava/lang/String;)V");
        jstring jmsg = env->NewStringUTF(msg ? msg : "");
        jobject obj = env->NewObject(clsResult, ctorR, empty, 0, status, jmsg);
        env->DeleteLocalRef(jmsg);
        return obj;
    }

    jobject makeResult(JNIEnv *env, const thermal::Result &R)
    {
        jclass clsList = env->FindClass("java/util/ArrayList");
        jmethodID ctorL = env->GetMethodID(clsList, "<init>", "()V");
        jmethodID add = env->GetMethodID(clsList, "add", "(Ljava/lang/Object;)Z");
        jobject list = env->NewObject(clsList, ctorL);

        jclass clsPayload = env->FindClass(kPayloadClass);
        jmethodID ctorP = env->GetMethodID(clsPayload, "<init>", "(Landroid/graphics/Bitmap;FIF)V");

        for (const auto &st : R.stages)
        {
            jobject bmp = bmp::matToBitmap(env, st.rgba);
            jobject pl = env->NewObject(clsPayload, ctorP, bmp, st.mortarPermille, st.labelId, st.thresholdQ);
            env->CallBooleanMethod(list, add, pl);
            env->DeleteLocalRef(pl);
            env->DeleteLocalRef(bmp);
        }

        jclass clsResult = env->FindClass(kResultClass);
        jmethodID ctorR = env->GetMethodID(clsResult, "<init>", "(Ljava/util/List;IILjava/lang/String;)V");
        jstring jmsg = env->NewStringUTF(R.message.c_str());
        jobject obj = env->NewObject(clsResult, ctorR, list, R.usedK, R.status, jmsg);
        env->DeleteLocalRef(jmsg);
        return obj;
    }

} // namespace JniConfig
