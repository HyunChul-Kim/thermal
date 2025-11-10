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

# 버전 (패키지 구성용)
set(THERMAL_CORE_VERSION 1.0.0)

# ------------------------------------------------------------
# 공통: thermal_core 정적 라이브러리 (플랫폼 공통)
# ------------------------------------------------------------
add_library(thermal_core STATIC
    src/core.cpp
)
target_include_directories(thermal_core PRIVATE ${OpenCV_INCLUDE_DIRS})
target_include_directories(thermal_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# ------------------------------------------------------------
# 안드로이드 분기 (단일 .so에 OpenCV 정적 링크)
# ------------------------------------------------------------
if(ANDROID)
    # Android NDK 툴체인 사용 전제 (상위에서 -DCMAKE_TOOLCHAIN_FILE 지정)
    # OpenCV(Android) 정적 패키지 위치 (네가 설치해둔 경로로 맞춰줘)
    # 예: C:/dist/opencv-android/arm64-v8a
    set(OPENCV_ANDROID_ROOT "C:/dist/opencv-android-slim/${ANDROID_ABI}" CACHE PATH "OpenCV Android install root for this ABI")
    if(NOT OPENCV_ANDROID_ROOT)
        message(FATAL_ERROR "Set -DOPENCV_ANDROID_ROOT=<...> to your OpenCV-android install root for ${ANDROID_ABI}")
    endif()

    target_include_directories(thermal_core PRIVATE
      ${OPENCV_ANDROID_ROOT}/sdk/native/jni/include
    )

    # thermal_core는 코어 계산만 담당 → Android에선 OpenCV를 JNI 브릿지 쪽에 링크해서 한 덩어리로 만든다
    # (즉, 앱 쪽에서 별도의 OpenCV 설정이 필요 없도록)
    add_library(thermal_android SHARED
        apps/jni/JniEntry.cpp
        apps/jni/JniCache.cpp
        apps/jni/BitmapUtils.cpp
        apps/jni/JniConfig.cpp
    )

    # include path
    target_include_directories(thermal_android PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/jni/include
        ${OPENCV_ANDROID_ROOT}/sdk/native/jni/include
    )

    # 안드 전용 시스템 라이브러리
    target_link_libraries(thermal_android PRIVATE log android jnigraphics)

    # thermal_core 정적 lib을 같이 묶는다
    target_link_libraries(thermal_android PRIVATE thermal_core)

    # --- OpenCV 정적 라이브러리 선택 ---
    # A) world로 빌드했다면 (용량 크지만 편함)
    option(OPENCV_ANDROID_USE_WORLD "Link with libopencv_world.a" OFF)

    if(OPENCV_ANDROID_USE_WORLD)
        set(OPENCV_WORLD_A
            ${OPENCV_ANDROID_ROOT}/sdk/native/staticlibs/${ANDROID_ABI}/libopencv_world.a
        )
        # world가 의존하는 3rdparty들도 필요할 수 있음
        # 필요 시 아래에 추가: (네 빌드 산출에 맞춰 조정)
        set(OPENCV_3RDPARTY
            ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libcpufeatures.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libIlmImf.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libittnotify.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libkleidicv.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libkleidicv_hal.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libkleidicv_thread.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/liblibjpeg-turbo.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/liblibopenjp2.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/liblibpng.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/liblibtiff.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/liblibwebp.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libtegra_hal.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libzlib.a
            # 필요 시 protobuf/IlmImf 등 추가
        )
        # 정적 링크 시 심볼 누락 방지용 whole-archive
        target_link_options(thermal_android PRIVATE
            "-Wl,--whole-archive" ${OPENCV_WORLD_A} ${OPENCV_3RDPARTY} "-Wl,--no-whole-archive"
        )
    else()
        # B) 필요한 모듈만 (용량 최적화)
        # 네가 빌드한 리스트에 맞춰 아래를 조정(core,imgproc,imgcodecs,ximgproc 등)
        set(OPENCV_STATIC_LIBS
            ${OPENCV_ANDROID_ROOT}/sdk/native/staticlibs/${ANDROID_ABI}/libopencv_calib3d.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/staticlibs/${ANDROID_ABI}/libopencv_core.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/staticlibs/${ANDROID_ABI}/libopencv_features2d.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/staticlibs/${ANDROID_ABI}/libopencv_flann.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/staticlibs/${ANDROID_ABI}/libopencv_imgcodecs.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/staticlibs/${ANDROID_ABI}/libopencv_imgproc.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/staticlibs/${ANDROID_ABI}/libopencv_video.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/staticlibs/${ANDROID_ABI}/libopencv_ximgproc.a
        )
        set(OPENCV_3RDPARTY
            ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libcpufeatures.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libIlmImf.a
            # x86_64 start
            ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libipphal.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libippicv.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libippiw.a
            # x86_64 end
            ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libittnotify.a
            # ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libkleidicv.a
            # ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libkleidicv_hal.a
            # ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libkleidicv_thread.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/liblibjpeg-turbo.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/liblibopenjp2.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/liblibpng.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/liblibtiff.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/liblibwebp.a
            # ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libtegra_hal.a
            ${OPENCV_ANDROID_ROOT}/sdk/native/3rdparty/libs/${ANDROID_ABI}/libzlib.a
        )
        target_link_options(thermal_android PRIVATE
            "-Wl,--whole-archive" ${OPENCV_STATIC_LIBS} ${OPENCV_3RDPARTY} "-Wl,--no-whole-archive"
        )
    endif()

    # (선택) C++ 런타임 정적 포함(파일 커짐) — 미포함이면 앱에 libc++_shared.so 동봉
    # target_link_options(thermal_android PRIVATE -static-libstdc++)

    # Android는 보통 install 불필요(앱으로 복사)라서 별도 install 스킵

# ------------------- iOS (device/simulator) -------------------
elseif(APPLE AND IOS) 
     message(STATUS "Configuring for iOS")

    # 외부에서 넘겨줄 것:
    # -DOPENCV_IOS_ROOT=<.../_dist/opencv-ios-slim>
    if(NOT DEFINED OPENCV_IOS_ROOT)
        message(FATAL_ERROR "Set -DOPENCV_IOS_ROOT to your OpenCV iOS install root")
    endif()

    # 슬라이스별(iphoneos / iphonesimulator) 빌드에서
    # CMAKE_OSX_SYSROOT, CMAKE_OSX_ARCHITECTURES, CMAKE_OSX_DEPLOYMENT_TARGET을
    # cmake 호출 시 지정한다. 여기서는 링크만 구성.
    function(link_opencv_slim tgt sysroot)
        if(${sysroot} STREQUAL "iphoneos")
            set(OPCV_LIBDIR "${OPENCV_IOS_ROOT}/iphoneos/sdk/native/staticlibs/${CMAKE_OSX_ARCHITECTURES}")
            set(OPCV_3P_DIR "${OPENCV_IOS_ROOT}/iphoneos/sdk/native/3rdparty/libs/${CMAKE_OSX_ARCHITECTURES}")
            set(OPCV_INC    "${OPENCV_IOS_ROOT}/iphoneos/sdk/native/include")
        else()
            set(OPCV_LIBDIR "${OPENCV_IOS_ROOT}/iphonesimulator/sdk/native/staticlibs/${CMAKE_OSX_ARCHITECTURES}")
            set(OPCV_3P_DIR "${OPENCV_IOS_ROOT}/iphonesimulator/sdk/native/3rdparty/libs/${CMAKE_OSX_ARCHITECTURES}")
            set(OPCV_INC    "${OPENCV_IOS_ROOT}/iphonesimulator/sdk/native/include")
        endif()

        target_include_directories(${tgt} PRIVATE ${OPCV_INC})

        # 네가 빌드한 모듈에 맞게 조정
        set(OPCV_LIBS
            ${OPCV_LIBDIR}/libopencv_core.a
            ${OPCV_LIBDIR}/libopencv_imgproc.a
            ${OPCV_LIBDIR}/libopencv_imgcodecs.a
            ${OPCV_LIBDIR}/libopencv_ximgproc.a
        )

        # 필요 3rd party
        set(OPCV_3P
            ${OPCV_3P_DIR}/liblibjpeg-turbo.a
            ${OPCV_3P_DIR}/liblibpng.a
            ${OPCV_3P_DIR}/liblibtiff.a
            ${OPCV_3P_DIR}/liblibwebp.a
            ${OPCV_3P_DIR}/libzlib.a
        )

        # 정적 링크 시 whole-archive로 심볼 누락 방지
        target_link_options(${tgt} PRIVATE
            "-Wl,--whole-archive" ${OPCV_LIBS} ${OPCV_3P} "-Wl,--no-whole-archive"
        )

        # iOS 기본 시스템 프레임워크 (이미지 디코드 경로 따라 다를 수 있음)
        target_link_libraries(${tgt} PRIVATE
            "-framework CoreGraphics"
            "-framework Accelerate"
            "-framework ImageIO"
            "-framework CoreFoundation"
        )
    endfunction()

    # 여기선 thermal_core 에 직접 링크(= 완전 번들)
    link_opencv_slim(thermal_core ${CMAKE_OSX_SYSROOT})
# ------------------------------------------------------------
# 데스크톱(Windows/Linux/macOS) 분기
# ------------------------------------------------------------
else()
    # OpenCV는 시스템에서 찾음
    # 필요한 모듈만 명시
    find_package(OpenCV REQUIRED core imgproc imgcodecs ximgproc)

    target_link_libraries(thermal_core PUBLIC ${OpenCV_LIBS})

    # (선택) CLI 앱
    if(THERMAL_ENABLE_CLI)
        add_subdirectory(apps/cli)
        # apps/cli/CMakeLists.txt 내부:
        # add_executable(thermal_cli apps/cli/main.cpp)
        # target_link_libraries(thermal_cli PRIVATE thermal_core)
    endif()

    # -------- install/export (데스크톱용) --------
    install(TARGETS thermal_core
        EXPORT thermal_coreTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/thermal
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(EXPORT thermal_coreTargets
        NAMESPACE thermal::
        FILE thermal_coreTargets.cmake
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
