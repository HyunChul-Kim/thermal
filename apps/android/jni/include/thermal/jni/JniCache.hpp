#pragma once
#include <jni.h>
#include "thermal/core.hpp"

namespace jni {

struct Cache {
  jclass cls_ArrayList = nullptr;
  jmethodID ctor_ArrayList = nullptr;
  jmethodID mid_ArrayList_add = nullptr;

  jclass cls_Bitmap = nullptr; // 필요시

  jclass cls_StagePayload = nullptr; // com.chul.thermalimaging.model.NativeStagePayload
  jmethodID ctor_StagePayload = nullptr; // (Landroid/graphics/Bitmap;FILjava/lang/String;I)V 등 시그니처는 실제와 맞추기

  jclass cls_NativeResult = nullptr; // com.chul.thermalimaging.model.NativeResult
  jmethodID ctor_NativeResult = nullptr; // (Ljava/util/List;IILjava/lang/String;)V  등 실제와 맞추기

  // 초기화
  bool init(JNIEnv* env);
};

// 전역 캐시 접근
Cache& cache();

// thermal::Result -> NativeResult (NewObject)
jobject buildNativeResult(JNIEnv* env, const thermal::Result& res);

} // namespace jni
