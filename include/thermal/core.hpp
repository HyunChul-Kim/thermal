#pragma once
#include <vector>
#include <optional>
#include <string>
#include <opencv2/core.hpp>

#define THERMAL_CORE_API_VERSION 0x00010000 // 1.0.0

namespace thermal
{

    struct Polygon
    {
        std::vector<int> xs;
        std::vector<int> ys; // image coords
    };

    struct Params
    {
        int regionSize = 30;
        int compactness = 12;
        bool doBilateral = false;
        bool drawEdges = false;     // 슈퍼 픽셀 Edge 노출 여부
        float mrfLambda = 0.4f;
        int maxK = 5;               // 2..7
        int renderMaxK = 5;         // kept for parity
        int stageIdx = 1;           // stageSteps 중 기준 index (2차 검출 시 사용)
        int stageSteps = 6;         // 구간 단위
        bool refineMode = false;    // 2차 검출 모드
        int refineSteps = 5;        // 2차 검출 구간 단위
    };

    struct Payload {
        cv::Mat rgba;                   // 이 스테이지의 RGBA 결과(CV_8UC4, input과 동일 크기)
        float mortarPermille = 0.f;     // 이 스테이지의 몰탈 비율
        int labelId          = -1;      // 스테이지 대표 라벨 하나가 있을 때만 사용
        float thresholdQ     = 0.f;     // 이 스테이지가 사용한 분위수 임계
    };


    struct Result
    {
        std::vector<Payload> stages;    // 스테이지별 검출 이미지
        std::vector<int> labelIds;      // ROI scanline order; optional
        int usedK = 0;                  // GMM K actually used
        int status = 0;                 // 0 ok; negative error
        std::string message;
    };

    // Pure C++ version of Java_com_chul_thermalimaging_util_ThermalNative_segmentTempGroups
    Result segmentTempGroups(
        const cv::Mat &inRgba, // CV_8UC4
        const std::optional<Polygon> &roi,
        const Params &p,
        bool needLabelIds = false);

} // namespace thermal
