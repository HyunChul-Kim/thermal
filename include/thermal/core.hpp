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
        bool drawEdges = false;     // SuperPixels Edge visibility
        float mrfLambda = 0.4f;
        int maxK = 5;               // 2..7
        int renderMaxK = 5;         // kept for parity
        int stageIdx = 1;           // base index in stageSteps (for 2nd process)
        int stageSteps = 6;         // stage's step
        bool refineMode = false;    // enable for 2nd process mode
        int refineSteps = 5;        // 2nd stage's step
    };

    struct Payload {
        cv::Mat rgba;                   // result(RGBA) for this stage (CV_8UC4, same size as input)
        float mortarPermille = 0.f;     // mortar ratio for this stage
        int labelId          = -1;      
        float thresholdQ     = 0.f;     // threshold for this stage
    };


    struct Result
    {
        std::vector<Payload> stages;    // payload by stages
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
