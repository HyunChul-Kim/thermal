#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include "thermal/core.hpp"

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::cerr << "usage: thermal_cli <input_rgba_or_bgr> <output_path>\n";
        std::cerr << "       when multiple stages, files are written as <stem>_stage_##<ext>\n";
        return 1;
    }

    // 1) 입력 로드
    cv::Mat img = cv::imread(argv[1], cv::IMREAD_UNCHANGED);
    if (img.empty()) {
        std::cerr << "load fail: " << argv[1] << "\n";
        return 2;
    }
    // RGBA 보장
    if (img.type() != CV_8UC4) {
        cv::cvtColor(img, img, cv::COLOR_BGR2RGBA);
    }

    // 2) 파라미터 (필요시 CLI 인자로 확장)
    thermal::Params p;
    // p.stageSteps = 6;      // 필요시 기본값 조정
    // p.refineMode = false;  // …
    // p.refineSteps = 0;     // …
    // p.stageIdx = 1;        // …
    // p.refineK = 1;         // …
    // p.maxK = 5;            // …

    // ROI 없음으로 호출 (필요하면 CLI 인자 받아 polygon 생성)
    auto R = thermal::segmentTempGroups(img, std::nullopt, p, /*needLabelIds=*/false);
    if (R.status != 0) {
        std::cerr << "segment failed: " << R.message << "\n";
        return 3;
    }

    // 3) 출력 경로 파싱
    std::string out = argv[2];
    std::string stem = out, ext = ".png";
    if (auto pos = out.find_last_of('.'); pos != std::string::npos) {
        stem = out.substr(0, pos);
        ext  = out.substr(pos);
    }

    // 4) 스테이지별 저장 + 요약 로그
    const auto& stages = R.stages; // vector<Payload>
    if (stages.empty()) {
        std::cerr << "no stages returned\n";
        return 4;
    }

    if (stages.size() == 1) {
        cv::imwrite(out, stages[0].rgba);
        std::cout << "wrote: " << out
                  << "  (mortarPermille=" << stages[0].mortarPermille
                  << ", labelId=" << stages[0].labelId
                  << ", q=" << stages[0].thresholdQ << ")\n";
    } else {
        for (size_t i = 0; i < stages.size(); ++i) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "_stage_%02zu", i + 1);
            const std::string path = stem + buf + ext;
            cv::imwrite(path, stages[i].rgba);
            std::cout << "wrote: " << path
                      << "  (mortarPermille=" << stages[i].mortarPermille
                      << ", labelId=" << stages[i].labelId
                      << ", q=" << stages[i].thresholdQ << ")\n";
        }
    }

    // 전역 usedK/상태 메시지 요약
    std::cout << "[usedK=" << R.usedK << "] status=" << R.status
              << " message=\"" << R.message << "\"\n";

    return 0;
}
