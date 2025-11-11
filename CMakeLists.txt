cmake_minimum_required(VERSION 3.22)
project(thermal_all LANGUAGES C CXX)

# ------------------------------------------------------------
# 공통 설정
# ------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)

# 옵션 토글
option(THERMAL_ENABLE_CLI            "Build CLI (non-Android)" ON)
option(THERMAL_ENABLE_ANDROID_BRIDGE "Build Android JNI bridge (.so)" ON)

# 데스크톱(OpenCV) 링크 전략: 기본 = 동적
option(THERMAL_DESKTOP_OPENCV_STATIC "Bundle static OpenCV into the .so/dll (desktop)" OFF)

# 버전 (패키지 구성용)
set(THERMAL_CORE_VERSION 1.0.0)

# ------------------------------------------------------------
# 공통: thermal_core 라이브러리 타입
# ------------------------------------------------------------
if(ANDROID)
  set(THERMAL_CORE_LIB_TYPE STATIC)   # Android: 정적 core + 별도 JNI .so
else()
  set(THERMAL_CORE_LIB_TYPE SHARED)   # Desktop/macOS/iOS: 동적 .so/.dylib
endif()

add_library(thermal_core ${THERMAL_CORE_LIB_TYPE}
  src/core.cpp
)

if (WIN32)
  target_sources(thermal_core PRIVATE src/win_exports.cpp)
endif()

target_include_directories(thermal_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# SONAME (Linux 등)
if(UNIX AND NOT ANDROID AND NOT IOS)
  set_target_properties(thermal_core PROPERTIES
    VERSION   ${THERMAL_CORE_VERSION}
    SOVERSION 1
    OUTPUT_NAME thermal_core
  )
endif()

# ------------------------------------------------------------
# ANDROID: OpenCV 정적 번들 + JNI 브릿지 .so
# ------------------------------------------------------------
if(ANDROID)
  set(OPENCV_ANDROID_ROOT "" CACHE PATH "OpenCV Android install root for this ABI (…/sdk/native/* 가 들어있는 루트)")
  if(NOT OPENCV_ANDROID_ROOT)
    message(FATAL_ERROR "Set -DOPENCV_ANDROID_ROOT=<...> to your OpenCV-android install root for ${ANDROID_ABI}")
  endif()

  target_include_directories(thermal_core PRIVATE
    ${OPENCV_ANDROID_ROOT}/sdk/native/jni/include
  )

  if(THERMAL_ENABLE_ANDROID_BRIDGE)
    add_library(thermal_android SHARED
      apps/jni/JniEntry.cpp
      apps/jni/JniCache.cpp
      apps/jni/BitmapUtils.cpp
      apps/jni/JniConfig.cpp
    )

    target_include_directories(thermal_android PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/apps/jni/include
      ${OPENCV_ANDROID_ROOT}/sdk/native/jni/include
    )

    target_link_libraries(thermal_android PRIVATE log android jnigraphics)
    target_link_libraries(thermal_android PRIVATE thermal_core)

    option(OPENCV_ANDROID_USE_WORLD "Link with libopencv_world.a" OFF)

    if(OPENCV_ANDROID_USE_WORLD)
      set(OPENCV_WORLD_A
        ${OPENCV_ANDROID_ROOT}/sdk/native/staticlibs/${ANDROID_ABI}/libopencv_world.a
      )
      set(OPENCV_3RDPARTY
        ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libcpufeatures.a
        ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libIlmImf.a
        ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libittnotify.a
        ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/liblibjpeg-turbo.a
        ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/liblibopenjp2.a
        ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/liblibpng.a
        ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/liblibtiff.a
        ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/liblibwebp.a
        ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libzlib.a
      )
      target_link_options(thermal_android PRIVATE
        "-Wl,--whole-archive" ${OPENCV_WORLD_A} ${OPENCV_3RDPARTY} "-Wl,--no-whole-archive"
      )
    else()
      set(OPENCV_STATIC_LIBS
        ${OPENCV_ANDROID_ROOT}/sdk/native/staticlibs/${ANDROID_ABI}/libopencv_core.a
        ${OPENCV_ANDROID_ROOT}/sdk/native/staticlibs/${ANDROID_ABI}/libopencv_imgproc.a
        ${OPENCV_ANDROID_ROOT}/sdk/native/staticlibs/${ANDROID_ABI}/libopencv_imgcodecs.a
        ${OPENCV_ANDROID_ROOT}/sdk/native/staticlibs/${ANDROID_ABI}/libopencv_photo.a
        ${OPENCV_ANDROID_ROOT}/sdk/native/staticlibs/${ANDROID_ABI}/libopencv_ximgproc.a
      )
      set(OPENCV_3RDPARTY
        ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libcpufeatures.a
        ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libIlmImf.a
        ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libittnotify.a
        ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/liblibjpeg-turbo.a
        ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/liblibopenjp2.a
        ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/liblibpng.a
        ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/liblibtiff.a
        ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/liblibwebp.a
        ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libzlib.a
      )
      target_link_options(thermal_android PRIVATE
        "-Wl,--whole-archive" ${OPENCV_STATIC_LIBS} ${OPENCV_3RDPARTY} "-Wl,--no-whole-archive"
      )
    endif()
  endif()

# ------------------------------------------------------------
# iOS  (OpenCV.framework 사용)
# ------------------------------------------------------------
elseif(APPLE AND IOS)
  message(STATUS "Configuring for iOS")

  # 가장 확실한 방법: OpenCV_DIR을 opencv2.framework/Versions/A/Resources 로 전달
  # 대안: OPENCV_IOS_ROOT(opencv2.framework가 들어있는 폴더)만 주면 아래에서 유도
  if(DEFINED OPENCV_IOS_ROOT AND NOT DEFINED OpenCV_DIR)
    set(OpenCV_DIR "${OPENCV_IOS_ROOT}/opencv2.framework/Versions/A/Resources")
  endif()

  if(NOT DEFINED OpenCV_DIR)
    message(FATAL_ERROR "Set -DOpenCV_DIR to opencv2.framework/Versions/A/Resources (or set -DOPENCV_IOS_ROOT to the folder that contains opencv2.framework).")
  endif()

  # 필요한 모듈만 명시 (core,imgproc,imgcodecs,ximgproc 등)
  find_package(OpenCV REQUIRED core imgproc imgcodecs ximgproc)

  # 임포트 타겟으로만 연결 (수동 include/-F/-L 금지)
  target_link_libraries(thermal_core PRIVATE OpenCV::opencv2)

  # iOS 프레임워크 의존성(필요 시)
  target_link_libraries(thermal_core PRIVATE
    "-framework" Accelerate
    "-framework" CoreGraphics
    "-framework" QuartzCore
    "-framework" UIKit
  )

  # 디버그 로그
  message(STATUS "OpenCV_DIR = ${OpenCV_DIR}")
  message(STATUS "OpenCV_INCLUDE_DIRS = ${OpenCV_INCLUDE_DIRS}")

# ------------------------------------------------------------
# 데스크톱 (Linux/Windows/macOS) — 기본: 동적 OpenCV
# ------------------------------------------------------------
else()
  # --- 공통 보안/스레드 플래그 ---
  if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(thermal_core PRIVATE Threads::Threads)
    target_compile_options(thermal_core PRIVATE -fstack-protector-strong -D_FORTIFY_SOURCE=2)
    target_link_options(thermal_core PRIVATE -Wl,-z,relro,-z,now)
  endif()

  if(THERMAL_DESKTOP_OPENCV_STATIC)
    # 정적 번들 모드 (옵션으로 유지; 기본 OFF)
    set(OPENCV_DESKTOP_ROOT "/opt/opencv-4.12.0" CACHE PATH "OpenCV desktop install root (static)")
    set(OPCV_LIBDIR "${OPENCV_DESKTOP_ROOT}/lib")
    set(OPCV_INCDIR "${OPENCV_DESKTOP_ROOT}/include/opencv4")

    set(OPENCV_STATIC_LIBS
      ${OPCV_LIBDIR}/libopencv_core.a
      ${OPCV_LIBDIR}/libopencv_imgproc.a
      ${OPCV_LIBDIR}/libopencv_imgcodecs.a
      ${OPCV_LIBDIR}/libopencv_photo.a
      ${OPCV_LIBDIR}/libopencv_ximgproc.a
    )
    set(OPENCV_3RDPARTY
      ${OPCV_LIBDIR}/liblibjpeg-turbo.a
      ${OPCV_LIBDIR}/liblibpng.a
      ${OPCV_LIBDIR}/liblibtiff.a
      ${OPCV_LIBDIR}/liblibwebp.a
      ${OPCV_LIBDIR}/libzlib.a
    )

    target_include_directories(thermal_core PUBLIC ${OPCV_INCDIR})
    target_link_options(thermal_core PRIVATE
      "-Wl,--whole-archive" ${OPENCV_STATIC_LIBS} ${OPENCV_3RDPARTY} "-Wl,--no-whole-archive"
    )
  else()
    # 동적 링크 모드 (권장)
    find_package(OpenCV QUIET COMPONENTS core imgproc)
    if(OpenCV_FOUND)
      target_include_directories(thermal_core PUBLIC ${OpenCV_INCLUDE_DIRS})
      target_link_libraries(thermal_core PUBLIC ${OpenCV_LIBS})
    else()
      if(UNIX AND NOT APPLE)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(OPENCV REQUIRED opencv4)
        target_include_directories(thermal_core PUBLIC ${OPENCV_INCLUDE_DIRS})
        target_link_libraries(thermal_core PUBLIC ${OPENCV_LIBRARIES})
      else()
        message(FATAL_ERROR "OpenCV not found. On Windows/macOS, set -DOpenCV_DIR to the folder containing OpenCVConfig.cmake")
      endif()
    endif()
  endif()

  # --- Windows 전용 DLL 출력/내보내기 보정 ---
  if(WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_compile_definitions(thermal_core PRIVATE THERMAL_BUILD_DLL)

    add_compile_options(
      "$<$<C_COMPILER_ID:MSVC>:/utf-8>"
      "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>"
    )

    foreach(cfg IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
      set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} ${CMAKE_BINARY_DIR}/${cfg})
      set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg} ${CMAKE_BINARY_DIR}/${cfg})
      set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${cfg} ${CMAKE_BINARY_DIR}/${cfg})
    endforeach()
  endif()

  # --- Linux: 설치물 RPATH ($ORIGIN) 설정 ---
  if(UNIX AND NOT APPLE)
    set(CMAKE_SKIP_BUILD_RPATH FALSE)
    set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)
    set_target_properties(thermal_core PROPERTIES
      INSTALL_RPATH "\$ORIGIN:\$ORIGIN/../lib"
    )
  endif()

  # (선택) CLI
  if(THERMAL_ENABLE_CLI)
    add_subdirectory(apps/cli)
  endif()

  # install/export (데스크톱 공통)
  install(TARGETS thermal_core
    EXPORT  thermal_coreTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
  install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/thermal
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )
  install(EXPORT thermal_coreTargets
    NAMESPACE thermal::
    FILE     thermal_coreTargets.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/thermal_core
  )

  include(CMakePackageConfigHelpers)
  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/thermal_coreConfigVersion.cmake
    VERSION ${THERMAL_CORE_VERSION}
    COMPATIBILITY SameMajorVersion
  )
  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/thermal_coreConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/thermal_coreConfig.cmake
    @ONLY
  )
  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/thermal_coreConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/thermal_coreConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/thermal_core
  )
endif()
