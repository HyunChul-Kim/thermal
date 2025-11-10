# apps/jni/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

# 이 라이브러리는 안드로이드용 JNI 브릿지 .so
project(thermal_jni_bridge CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- 타겟 생성 ----
set(TARGET_NAME thermal_jni)

add_library(${TARGET_NAME} SHARED
    JniEntry.cpp
    JniCache.cpp
    JniConfig.cpp
    BitmapUtils.cpp
)

# ---- include 경로 ----
# thermal_core의 퍼블릭 헤더(thermal/core.hpp)와 JNI 헬퍼 헤더(thermal/jni/*.hpp)
set(THERMAL_CORE_INCLUDE "${CMAKE_SOURCE_DIR}/include")
set(JNI_HELPERS_INCLUDE  "${CMAKE_SOURCE_DIR}/apps/jni/include")

target_include_directories(${TARGET_NAME}
    PRIVATE
        ${THERMAL_CORE_INCLUDE}
        ${JNI_HELPERS_INCLUDE}
)

# ---- OpenCV 연결(유연 모드) ----
# 1) find_package(OpenCV ...) 우선 시도 (루트에서 이미 했으면 변수만 재사용됨)
#    - Android에서도 OpenCVConfig.cmake가 있다면 정상 인식됨
# 2) 실패하거나 직접 지정하고 싶으면
#    -DOPENCV_ANDROID_INCLUDE_DIR=...  -DOpenCV_WORLD_LIB=...  로 넘겨서 사용 가능
#    (world 정적 라이브러리를 링크하는 경량 방식)
find_package(OpenCV QUIET COMPONENTS core imgproc imgcodecs ximgproc)

if (OpenCV_FOUND)
    message(STATUS "[jni] OpenCV found: ${OpenCV_VERSION}")
    target_include_directories(${TARGET_NAME} PRIVATE ${OpenCV_INCLUDE_DIRS})
    # JNI 브릿지(.so)에만 OpenCV를 링크
    target_link_libraries(${TARGET_NAME} PRIVATE ${OpenCV_LIBS})
elseif (DEFINED OpenCV_WORLD_LIB AND EXISTS "${OpenCV_WORLD_LIB}")
    message(STATUS "[jni] Using IMPORTED OpenCV world: ${OpenCV_WORLD_LIB}")
    if (NOT DEFINED OPENCV_ANDROID_INCLUDE_DIR)
        message(FATAL_ERROR "Set OPENCV_ANDROID_INCLUDE_DIR alongside OpenCV_WORLD_LIB")
    endif()
    add_library(opencv_world STATIC IMPORTED)
    set_target_properties(opencv_world PROPERTIES
        IMPORTED_LOCATION "${OpenCV_WORLD_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENCV_ANDROID_INCLUDE_DIR}"
    )
    target_include_directories(${TARGET_NAME} PRIVATE "${OPENCV_ANDROID_INCLUDE_DIR}")
    target_link_libraries(${TARGET_NAME} PRIVATE opencv_world)
else()
    message(FATAL_ERROR "OpenCV not found. Provide OpenCV_DIR or OpenCV_WORLD_LIB/OPENCV_ANDROID_INCLUDE_DIR.")
endif()

# ---- thermal_core 링크 ----
# 같은 트리에서 thermal_core를 빌드 중이면 타겟으로 바로 링크
if (TARGET thermal_core)
    message(STATUS "[jni] Link in-tree target: thermal_core")
    target_link_libraries(${TARGET_NAME} PRIVATE thermal_core)
# 미리 빌드된 정적/동적 라이브러리를 IMPORTED로 링크하고 싶다면:
elseif (DEFINED THERMAL_CORE_IMPORTED_LIB AND EXISTS "${THERMAL_CORE_IMPORTED_LIB}")
    message(STATUS "[jni] Link IMPORTED thermal_core: ${THERMAL_CORE_IMPORTED_LIB}")
    add_library(thermal_core_imported STATIC IMPORTED)
    set_target_properties(thermal_core_imported PROPERTIES
        IMPORTED_LOCATION "${THERMAL_CORE_IMPORTED_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${THERMAL_CORE_INCLUDE}"  # 필요시 -DTHERMAL_CORE_INCLUDE="..."
    )
    target_link_libraries(${TARGET_NAME} PRIVATE thermal_core_imported)
else()
    message(FATAL_ERROR "thermal_core target not found. Build thermal_core in-tree or define THERMAL_CORE_IMPORTED_LIB.")
endif()

# ---- NDK 시스템 라이브러리 ----
if (ANDROID)
    target_link_libraries(${TARGET_NAME} PRIVATE
        log
        android
        jnigraphics
    )
endif()

# ---- JNI RegisterNatives용 클래스/시그니처 주입 ----
# 기본값은 ThermalNative / NativeResult. 앱 패키지가 바뀌면 -D로 재정의해서 재빌드.
#   - DJNI_ENTRY_CLASS="com/your/app/util/ThermalNative"
#   - DJNI_RESULT_SIGNATURE="Lcom/your/app/model/NativeResult;"
if (NOT DEFINED JNI_ENTRY_CLASS)
    set(JNI_ENTRY_CLASS "com/chul/thermalimaging/util/ThermalNative")
endif()
if (NOT DEFINED JNI_RESULT_SIGNATURE)
    set(JNI_RESULT_SIGNATURE "Lcom/chul/thermalimaging/model/NativeResult;")
endif()

target_compile_definitions(${TARGET_NAME} PRIVATE
    JNI_ENTRY_CLASS="${JNI_ENTRY_CLASS}"
    JNI_RESULT_SIGNATURE="${JNI_RESULT_SIGNATURE}"
)

# ---- 출력 이름 깔끔히 ----
set_target_properties(${TARGET_NAME} PROPERTIES
    OUTPUT_NAME "${TARGET_NAME}"
)

# ---- 성능/빌드 옵션(선택) ----
# PIC는 SHARED면 자동이지만 명시하고 싶다면:
set_target_properties(${TARGET_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 최적화(루트에서 CMAKE_BUILD_TYPE로 제어하는 게 보통)
# if (CMAKE_BUILD_TYPE MATCHES "Release")
#     target_compile_options(${TARGET_NAME} PRIVATE -O3)
# endif()
