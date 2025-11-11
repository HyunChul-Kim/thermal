#include <iostream>
#include <optional>
#include <string>
#include <vector>
#include <sstream>
#include <cstdlib>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include "thermal/core.hpp"

// ---- helpers ----------------------------------------------------
static bool parseInt(const std::string &s, int &out)
{
    try
    {
        out = std::stoi(s);
        return true;
    }
    catch (...)
    {
        return false;
    }
}
static bool parseFloat(const std::string &s, float &out)
{
    try
    {
        out = std::stof(s);
        return true;
    }
    catch (...)
    {
        return false;
    }
}
static bool parseBool(const std::string &s, bool &out)
{
    auto v = s;
    for (auto &c : v)
        c = (char)std::tolower((unsigned char)c);
    if (v == "1" || v == "true" || v == "yes" || v == "on")
    {
        out = true;
        return true;
    }
    if (v == "0" || v == "false" || v == "no" || v == "off")
    {
        out = false;
        return true;
    }
    return false;
}
// ROI 문자열을 thermal::Polygon으로 파싱
// 형식: "x1,y1;x2,y2;..."; 예) --roi "12,34;56,78;90,12"
static std::optional<thermal::Polygon> parseRoiPolygon(const std::string &s)
{
    thermal::Polygon poly;
    std::stringstream ss(s);
    std::string token;
    while (std::getline(ss, token, ';'))
    {
        if (token.empty())
            continue;
        std::stringstream pairss(token);
        std::string xs, ys;
        if (!std::getline(pairss, xs, ','))
            return std::nullopt;
        if (!std::getline(pairss, ys, ','))
            return std::nullopt;
        int xi, yi;
        if (!parseInt(xs, xi) || !parseInt(ys, yi))
            return std::nullopt;
        poly.xs.push_back(xi);
        poly.ys.push_back(yi);
    }
    if (poly.xs.size() != poly.ys.size() || poly.xs.size() < 3)
        return std::nullopt;
    return poly;
}

static void print_usage()
{
    std::cerr <<
        R"(usage:
        thermal_cli <input_path> <output_path> [options]

        options:
        --roi "<x1,y1;x2,y2;...>"     ROI 다각형 (이미지 좌표). 예: --roi "10,10;200,10;200,100;10,100"
        --stage-steps <int>           단계 개수 (기본: 라이브러리 기본값)
        --stage-idx <int>             선택 스테이지 인덱스(1-based). 미설정 시 전체 저장
        --refine-mode <bool>          true/false
        --refine-steps <int>
        --refine-k <int>
        --max-k <int>
        --mrf-lambda <float>          필요 시 사용하는 파라미터가 있을 경우
        --bgr                         입력이 BGR이라고 가정(강제 RGBA 변환)
        --rgba                        입력이 이미 RGBA (기본 auto: IMREAD_UNCHANGED 후 타입 따라 변환)

        예)
        thermal_cli in.png out.png --roi "12,34;128,40;140,200;10,220" --stage-steps 6 --max-k 5
        thermal_cli in.jpg result.png --refine-mode true --refine-steps 2 --stage-idx 3
        )";
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        print_usage();
        return 1;
    }

    std::string inPath = argv[1];
    std::string outPath = argv[2];

    // ---- default params ----------------------------------------
    thermal::Params p; // 라이브러리의 기본값이 들어있다고 가정
    bool forceBgr = false;
    bool forceRgba = false;
    std::optional<thermal::Polygon> roiPoly;

    // ---- parse flags -------------------------------------------
    for (int i = 3; i < argc;)
    {
        std::string k = argv[i];

        auto need = [&](int n) -> bool
        {
            if (i + n >= argc)
            {
                std::cerr << "missing value for " << k << "\n";
                return false;
            }
            return true;
        };

        if (k == "--roi")
        {
            if (!need(1))
                return 1;
            auto poly = parseRoiPolygon(argv[i+1]);
            if (!poly) { std::cerr << "invalid --roi format\n"; return 1; }
            roiPoly = std::move(*poly);
            i += 2;
        }
        else if (k == "--stage-steps")
        {
            if (!need(1))
                return 1;
            if (!parseInt(argv[i + 1], p.stageSteps))
            {
                std::cerr << "invalid --stage-steps\n";
                return 1;
            }
            i += 2;
        }
        else if (k == "--stage-idx")
        {
            if (!need(1))
                return 1;
            if (!parseInt(argv[i + 1], p.stageIdx))
            {
                std::cerr << "invalid --stage-idx\n";
                return 1;
            }
            i += 2;
        }
        else if (k == "--refine-mode")
        {
            if (!need(1))
                return 1;
            if (!parseBool(argv[i + 1], p.refineMode))
            {
                std::cerr << "invalid --refine-mode\n";
                return 1;
            }
            i += 2;
        }
        else if (k == "--refine-steps")
        {
            if (!need(1))
                return 1;
            if (!parseInt(argv[i + 1], p.refineSteps))
            {
                std::cerr << "invalid --refine-steps\n";
                return 1;
            }
            i += 2;
        }
        else if (k == "--max-k")
        {
            if (!need(1))
                return 1;
            if (!parseInt(argv[i + 1], p.maxK))
            {
                std::cerr << "invalid --max-k\n";
                return 1;
            }
            i += 2;
        }
        else if (k == "--mrf-lambda")
        {
            if (!need(1))
                return 1;
            if (!parseFloat(argv[i + 1], p.mrfLambda))
            {
                std::cerr << "invalid --mrf-lambda\n";
                return 1;
            }
            i += 2;
        }
        else if (k == "--bgr")
        {
            forceBgr = true;
            forceRgba = false;
            i += 1;
        }
        else if (k == "--rgba")
        {
            forceRgba = true;
            forceBgr = false;
            i += 1;
        }
        else if (k == "-h" || k == "--help")
        {
            print_usage();
            return 0;
        }
        else
        {
            std::cerr << "unknown option: " << k << "\n";
            print_usage();
            return 1;
        }
    }

    // ---- load image --------------------------------------------
    int imreadFlag = cv::IMREAD_UNCHANGED;
    if (forceBgr)
        imreadFlag = cv::IMREAD_COLOR; // BGR
    if (forceRgba)
        imreadFlag = cv::IMREAD_UNCHANGED;

    cv::Mat img = cv::imread(inPath, imreadFlag);
    if (img.empty())
    {
        std::cerr << "load fail: " << inPath << "\n";
        return 2;
    }
    if (img.type() != CV_8UC4)
    {
        cv::cvtColor(img, img, cv::COLOR_BGR2RGBA);
    }

    // ---- run ---------------------------------------------------
    auto R = thermal::segmentTempGroups(
        img,
        roiPoly,
        p,
        /*needLabelIds=*/false);

    if (R.status != 0)
    {
        std::cerr << "segment failed: " << R.message << "\n";
        return 3;
    }

    // ---- output naming -----------------------------------------
    std::string stem = outPath, ext = ".png";
    if (auto pos = outPath.find_last_of('.'); pos != std::string::npos)
    {
        stem = outPath.substr(0, pos);
        ext = outPath.substr(pos);
    }

    const auto &stages = R.stages;
    if (stages.empty())
    {
        std::cerr << "no stages returned\n";
        return 4;
    }

    if (p.stageIdx > 0 && (size_t)p.stageIdx <= stages.size())
    {
        const auto &s = stages[(size_t)p.stageIdx - 1];
        cv::imwrite(outPath, s.rgba);
        std::cout << "wrote: " << outPath
                  << "  (mortarPermille=" << s.mortarPermille
                  << ", labelId=" << s.labelId
                  << ", q=" << s.thresholdQ << ")\n";
    }
    else if (stages.size() == 1)
    {
        cv::imwrite(outPath, stages[0].rgba);
        std::cout << "wrote: " << outPath
                  << "  (mortarPermille=" << stages[0].mortarPermille
                  << ", labelId=" << stages[0].labelId
                  << ", q=" << stages[0].thresholdQ << ")\n";
    }
    else
    {
        for (size_t i = 0; i < stages.size(); ++i)
        {
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

    std::cout << "[usedK=" << R.usedK << "] status=" << R.status
              << " message=\"" << R.message << "\"\n";
    return 0;
}
