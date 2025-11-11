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
option(THERMAL_ENABLE_JNI            "Build Desktop JVM JNI bridge (thermal_jni)" ON)

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
  set(THERMAL_CORE_LIB_TYPE SHARED)   # Desktop/macOS/iOS: 동적 .so/.dylib/.dll
endif()

add_library(thermal_core ${THERMAL_CORE_LIB_TYPE}
  src/core.cpp
)

if (WIN32)
  # 윈도우 심볼/코드페이지 편의
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
      apps/android/jni/JniEntry.cpp
      apps/android/jni/JniCache.cpp
      apps/android/jni/BitmapUtils.cpp
      apps/android/jni/JniConfig.cpp
    )

    target_include_directories(thermal_android PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/apps/android/jni/include
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

  if(NOT DEFINED OPENCV_IOS_ROOT)
    message(FATAL_ERROR "Set -DOPENCV_IOS_ROOT to your OpenCV iOS install root")
  endif()

  # 후보: (1) ${ROOT}/opencv2.framework, (2) ${ROOT}/lib/opencv2.framework
  set(_FW_CANDIDATES
    "${OPENCV_IOS_ROOT}/opencv2.framework"
    "${OPENCV_IOS_ROOT}/lib/opencv2.framework"
  )

  set(_FW_ROOT "")
  foreach(_fw IN LISTS _FW_CANDIDATES)
    if(EXISTS "${_fw}/Headers/opencv2/core.hpp" AND EXISTS "${_fw}/opencv2")
      set(_FW_ROOT "${_fw}")
      break()
    endif()
  endforeach()

  if(NOT _FW_ROOT)
    message(FATAL_ERROR
      "OpenCV framework not found.\n"
      "OPENCV_IOS_ROOT='${OPENCV_IOS_ROOT}'\n"
      "Tried:\n  ${OPENCV_IOS_ROOT}/opencv2.framework\n  ${OPENCV_IOS_ROOT}/lib/opencv2.framework\n"
      "Make sure OPENCV_IOS_ROOT is the directory that *contains* opencv2.framework."
    )
  endif()

  set(_FW_HEADERS "${_FW_ROOT}/Headers")
  set(_FW_BIN     "${_FW_ROOT}/opencv2")

  target_include_directories(thermal_core PRIVATE "${_FW_HEADERS}")
  target_link_libraries(thermal_core PRIVATE
    "${_FW_BIN}"
    "-framework Accelerate"
    "-framework CoreGraphics"
    "-framework UIKit"
    "-framework CoreVideo"
    "-framework AVFoundation"
    "-framework QuartzCore"
    "-framework CoreMedia"
  )

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
    find_package(OpenCV QUIET COMPONENTS core imgproc imgcodecs)
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

  # =========================
  # 데스크톱 JVM JNI 브릿지
  # =========================
  option(THERMAL_ENABLE_JNI "Build desktop JNI bridge (.dll/.so) for Java backends" ON)
  if(THERMAL_ENABLE_JNI)
    # 1) JDK 탐색: ENV 대신 CMake의 FindJNI로 안전하게
    find_package(JNI REQUIRED)

    # 2) JNI 브릿지 타겟
    #    ※ 소스 경로가 다르면 아래 경로를 네 실제 파일로 바꿔줘요.
    add_library(thermal_jni SHARED
      src/thermal_jni.cpp
    )
    target_link_libraries(thermal_jni PRIVATE thermal_core)

    # 3) 인클루드 경로: jni.h + OpenCV 헤더 + 우리 public 헤더
    #    (thermal_core가 OpenCV 헤더를 PUBLIC로 노출하더라도,
    #     여기서 OpenCV_INCLUDE_DIRS를 명시해 주면 IDE/빌드 모두 안정적)
    target_include_directories(thermal_jni PRIVATE
      ${JNI_INCLUDE_DIRS}
      ${OpenCV_INCLUDE_DIRS}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    # 4) 링크: 코어 + OpenCV
    target_link_libraries(thermal_jni PRIVATE
      thermal_core
      ${OpenCV_LIBS}
    )

    # 5) Windows 품질-of-life 옵션
    if(WIN32)
      add_compile_options(
        "$<$<C_COMPILER_ID:MSVC>:/utf-8>"
        "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>"
      )
      foreach(cfg IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
        set_target_properties(thermal_jni PROPERTIES
          RUNTIME_OUTPUT_DIRECTORY_${cfg} ${CMAKE_BINARY_DIR}/${cfg}
          LIBRARY_OUTPUT_DIRECTORY_${cfg} ${CMAKE_BINARY_DIR}/${cfg}
          ARCHIVE_OUTPUT_DIRECTORY_${cfg} ${CMAKE_BINARY_DIR}/${cfg}
        )
      endforeach()
    endif()

    # 6) Linux: 배포 편의 RUNPATH
    if(UNIX AND NOT APPLE)
      set_target_properties(thermal_jni PROPERTIES
        INSTALL_RPATH "\$ORIGIN:\$ORIGIN/../lib"
      )
      target_link_libraries(thermal_jni PRIVATE Threads::Threads)
    endif()

    # 7) (선택) 설치 규칙
    install(TARGETS thermal_jni
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
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
  if(THERMAL_ENABLE_JNI)
    install(TARGETS thermal_jni
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
  endif()

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
