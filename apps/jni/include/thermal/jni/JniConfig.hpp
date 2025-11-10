#pragma once
#include <jni.h>
#include <string>
#include "thermal/core.hpp"

namespace JniConfig
{

    // ----- 클래스 경로 관리 -----
    void setTargetClassPath(const char *path); // e.g. "com/chul/thermalimaging/util/ThermalNative"
    const char *targetClassPath();             // 현재 설정된 경로를 반환

    // ----- 클래스 로딩 -----
    jclass findClassGlobal(JNIEnv *env, const char *name); // NewGlobalRef로 글로벌 참조 반환

    // ----- 결과 빌더 -----
    jobject makeResult(JNIEnv *env, const thermal::Result &R);
    jobject makeErrorResult(JNIEnv *env, int status, const char *msg);

// (선택) 초기 기본 경로를 컴파일 타임 매크로로 주입할 수 있게 함
#ifndef JNI_ENTRY_CLASS
#define JNI_ENTRY_CLASS "com/chul/thermalimaging/util/ThermalNative"
#endif

} // namespace JniConfig
