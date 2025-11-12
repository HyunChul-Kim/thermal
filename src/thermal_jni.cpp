#include <jni.h>
#include <optional>
#include <vector>
#include <cstring>
#include <string>
#include <cstdio>

#include "thermal/core.hpp"
#include <opencv2/imgproc.hpp>

#ifndef THERMAL_JNI_CLASS_BIN
#define THERMAL_JNI_CLASS_BIN "com/chahoo/daewoo/barobar/z_hhlee_test/dll/ThermalJNI"
#endif

// ------------------- 공통 유틸 -------------------
static jstring makeJString(JNIEnv *env, const std::string &s)
{
    return env->NewStringUTF(s.c_str());
}

// jintArray 2개(xs, ys)를 thermal::Polygon으로 안전 변환
static std::optional<thermal::Polygon>
toPolygon(JNIEnv *env, jintArray xsArr, jintArray ysArr)
{
    if (!xsArr || !ysArr)
        return std::nullopt;

    jsize nx = env->GetArrayLength(xsArr);
    jsize ny = env->GetArrayLength(ysArr);
    if (nx <= 0 || ny <= 0 || nx != ny)
        return std::nullopt;

    std::vector<jint> xs_j(nx), ys_j(ny);
    env->GetIntArrayRegion(xsArr, 0, nx, xs_j.data());
    env->GetIntArrayRegion(ysArr, 0, ny, ys_j.data());

    thermal::Polygon poly;
    poly.xs.resize(nx);
    poly.ys.resize(ny);
    for (jsize i = 0; i < nx; ++i)
    {
        poly.xs[i] = static_cast<int>(xs_j[i]);
        poly.ys[i] = static_cast<int>(ys_j[i]);
    }
    return poly;
}

jclass findClassGlobal(JNIEnv *env, const char *name)
{
    jclass local = env->FindClass(name);
    if (!local)
        return nullptr;
    jclass global = (jclass)env->NewGlobalRef(local);
    env->DeleteLocalRef(local);
    return global;
}

// --------- 대상 클래스(binary name) & 시그니처 동적 생성 ---------
static std::string getTargetBinFromProperty(JNIEnv *env)
{
    jclass sys = env->FindClass("java/lang/System");
    jmethodID mid = env->GetStaticMethodID(sys, "getProperty",
                                           "(Ljava/lang/String;)Ljava/lang/String;");
    jstring key = env->NewStringUTF("thermal.jni.class"); // 예: "com.example.thermal.ThermalJNI"
    jobject val = env->CallStaticObjectMethod(sys, mid, key);
    env->DeleteLocalRef(key);

    std::string bin; // "com/example/thermal/ThermalJNI"
    if (val)
    {
        const char *cname = env->GetStringUTFChars((jstring)val, nullptr);
        bin = cname;
        env->ReleaseStringUTFChars((jstring)val, cname);
        env->DeleteLocalRef(val);
    }
    if (bin.empty())
        bin = THERMAL_JNI_CLASS_BIN;
    for (auto &ch : bin)
        if (ch == '.')
            ch = '/';
    return bin;
}

static std::string buildSigFor(const std::string &targetBin)
{
    // (ByteBuffer;III;[I[I;Z;II;ZI;I;Z;ByteBuffer;L<target>$Result;)I
    std::string s = "(Ljava/nio/ByteBuffer;IIIL[I[IZIIZIIZLjava/nio/ByteBuffer;L";
    s += targetBin;
    s += "$Result;)I";
    return s;
}

// ContextClassLoader로 안전하게 로드
static jclass loadWithContextCL(JNIEnv *env, const std::string &binName)
{
    jclass threadCls = env->FindClass("java/lang/Thread");
    jmethodID midCur = env->GetStaticMethodID(threadCls, "currentThread", "()Ljava/lang/Thread;");
    jobject curThread = env->CallStaticObjectMethod(threadCls, midCur);
    jmethodID midGetCL = env->GetMethodID(threadCls, "getContextClassLoader", "()Ljava/lang/ClassLoader;");
    jobject cl = env->CallObjectMethod(curThread, midGetCL);

    jclass clCls = env->FindClass("java/lang/ClassLoader");
    jmethodID midLoad = env->GetMethodID(clCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");

    // 점 표기로 넘겨야 함
    std::string dotted = binName;
    for (auto &ch : dotted)
        if (ch == '/')
            ch = '.';
    jstring jName = env->NewStringUTF(dotted.c_str());
    jobject k = env->CallObjectMethod(cl, midLoad, jName);
    env->DeleteLocalRef(jName);

    return (jclass)k; // nullptr이면 실패
}

// ------------------- 네이티브 본체 -------------------
static jint JNICALL
native_segmentRGBA(JNIEnv *env, jclass /*cls*/,
                   jobject inBuf, jint width, jint height, jint stride,
                   jintArray xsArr, jintArray ysArr,
                   jboolean doBilateral,
                   jint stageSteps, jint maxK,
                   jboolean refineMode, jint refineSteps,
                   jint stageIdx,
                   jboolean needLabelIds,
                   jobject outBuf, jobject outMetaObj)
{
    // 입력 버퍼 매핑
    auto *inPtr = static_cast<unsigned char *>(env->GetDirectBufferAddress(inBuf));
    jlong inCap = env->GetDirectBufferCapacity(inBuf);
    auto *outPtr = static_cast<unsigned char *>(env->GetDirectBufferAddress(outBuf));
    jlong outCap = env->GetDirectBufferCapacity(outBuf);
    const jlong need = (jlong)width * height * 4;
    if (!inPtr || !outPtr || inCap < need || outCap < need)
        return -100;

    cv::Mat rgba(height, width, CV_8UC4, inPtr, stride);

    // ROI
    std::optional<thermal::Polygon> roi = toPolygon(env, xsArr, ysArr);

    // Params (안드와 동일)
    thermal::Params p{};
    p.doBilateral = (doBilateral == JNI_TRUE);
    p.stageSteps = stageSteps;
    p.maxK = maxK;
    p.refineMode = (refineMode == JNI_TRUE);
    p.refineSteps = refineSteps;
    p.stageIdx = stageIdx;

    // 코어 호출
    const bool wantIds = (needLabelIds == JNI_TRUE);
    auto R = thermal::segmentTempGroups(rgba, roi, p, /*needLabelIds=*/wantIds);

    // --------- Result -> Java outMeta ---------
    jclass metaCls = env->GetObjectClass(outMetaObj);
    if (!metaCls)
        return -300;

    // 동적 Stage/Result 경로
    const std::string targetBin = getTargetBinFromProperty(env);
    const std::string stageBin = targetBin + "$Stage";
    const std::string stagesSig = "[L" + stageBin + ";";

    jfieldID fidStatus = env->GetFieldID(metaCls, "status", "I");
    jfieldID fidUsedK = env->GetFieldID(metaCls, "usedK", "I");
    jfieldID fidMsg = env->GetFieldID(metaCls, "message", "Ljava/lang/String;");
    jfieldID fidStages = env->GetFieldID(metaCls, "stages", stagesSig.c_str());
    jfieldID fidLblIds = env->GetFieldID(metaCls, "labelIds", "[I");
    if (!fidStatus || !fidUsedK || !fidMsg || !fidStages || !fidLblIds)
        return -301;

    env->SetIntField(outMetaObj, fidStatus, (jint)R.status);
    env->SetIntField(outMetaObj, fidUsedK, (jint)R.usedK);
    {
        jstring jmsg = env->NewStringUTF(R.message.c_str());
        env->SetObjectField(outMetaObj, fidMsg, jmsg);
        env->DeleteLocalRef(jmsg);
    }

    if (R.status != 0 || R.stages.empty())
    {
        env->SetObjectField(outMetaObj, fidStages, nullptr);
        env->SetObjectField(outMetaObj, fidLblIds, nullptr);
        return R.status ? (jint)R.status : (jint)-1;
    }

    // Stage[] 채우기
    jclass stageCls = env->FindClass(stageBin.c_str());
    if (!stageCls)
        return -302;
    jmethodID stageCtor = env->GetMethodID(stageCls, "<init>", "()V");
    jfieldID fPermille = env->GetFieldID(stageCls, "mortarPermille", "F");
    jfieldID fLabelId = env->GetFieldID(stageCls, "labelId", "I");
    jfieldID fQ = env->GetFieldID(stageCls, "thresholdQ", "F");
    if (!stageCtor || !fPermille || !fLabelId || !fQ)
        return -303;

    const jsize n = (jsize)R.stages.size();
    jobjectArray jStages = env->NewObjectArray(n, stageCls, nullptr);
    if (!jStages)
        return -304;

    for (jsize i = 0; i < n; ++i)
    {
        jobject st = env->NewObject(stageCls, stageCtor);
        env->SetFloatField(st, fPermille, (jfloat)R.stages[i].mortarPermille);
        env->SetIntField(st, fLabelId, (jint)R.stages[i].labelId);
        env->SetFloatField(st, fQ, (jfloat)R.stages[i].thresholdQ);
        env->SetObjectArrayElement(jStages, i, st);
        env->DeleteLocalRef(st);
    }
    env->SetObjectField(outMetaObj, fidStages, jStages);
    env->DeleteLocalRef(jStages);

    // labelIds
    if (wantIds && !R.labelIds.empty())
    {
        jsize m = (jsize)R.labelIds.size();
        jintArray jIds = env->NewIntArray(m);
        if (!jIds)
            return -305;
        std::vector<jint> tmp(m);
        for (jsize i = 0; i < m; ++i)
            tmp[i] = (jint)R.labelIds[i];
        env->SetIntArrayRegion(jIds, 0, m, tmp.data());
        env->SetObjectField(outMetaObj, fidLblIds, jIds);
        env->DeleteLocalRef(jIds);
    }
    else
    {
        env->SetObjectField(outMetaObj, fidLblIds, nullptr);
    }

    // 첫 스테이지 RGBA → outBuf
    const cv::Mat &out0 = R.stages.front().rgba;
    if (out0.cols != width || out0.rows != height || out0.type() != CV_8UC4)
        return -2;
    std::memcpy(outPtr, out0.data, (size_t)(width * height * 4));

    return 0;
}

// ------------------- JNI_OnLoad: 동적 등록 -------------------
extern "C"
{
    JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *)
    {
        JNIEnv *env = nullptr;
        if (vm->GetEnv((void **)&env, JNI_VERSION_1_8) != JNI_OK)
            return JNI_ERR;

        const std::string targetBin = getTargetBinFromProperty(env); // com/example/thermal/ThermalJNI
        jclass target = loadWithContextCL(env, targetBin);
        if (!target)
        {
            // 최후의 보루: 부트스트랩 로더로도 시도
            target = env->FindClass(targetBin.c_str());
            if (!target)
            {
                fprintf(stderr, "[thermal_jni] FindClass fail for %s\n", targetBin.c_str());
                return JNI_ERR;
            }
        }

        const std::string sig = buildSigFor(targetBin);

        // 시그니처 사전 검증: 실제 자바에 정확히 이 static native 선언이 있는지 확인
        jmethodID mid = env->GetStaticMethodID(target, "segmentRGBA", sig.c_str());
        if (!mid)
        {
            fprintf(stderr, "[thermal_jni] target is %s\n", targetBin.c_str());
            fprintf(stderr, "[thermal_jni] No exact method found: segmentRGBA %s\n", sig.c_str());
            return JNI_ERR;
        }

        JNINativeMethod m;
        m.name = (char *)"segmentRGBA";
        m.signature = (char *)sig.c_str();
        m.fnPtr = (void *)native_segmentRGBA;

        if (env->RegisterNatives(target, &m, 1) != 0)
        {
            fprintf(stderr, "[thermal_jni] RegisterNatives failed for %s\n", sig.c_str());
            return JNI_ERR;
        }

        fprintf(stderr, "[thermal_jni] Registered %s.segmentRGBA %s\n", targetBin.c_str(), sig.c_str());
        return JNI_VERSION_1_8;
    }
}