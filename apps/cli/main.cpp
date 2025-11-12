#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <optional>
#include <cstring>
#include <cctype>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "thermal/core.hpp"

// --------- 작은 유틸들 ----------
static bool ieq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i=0;i<a.size();++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}
static bool parseInt(const std::string& s, int& out){
    try { size_t p=0; long v=std::stol(s,&p,10); if(p!=s.size()) return false; out=(int)v; return true; }
    catch(...) { return false; }
}
static bool parseFloat(const std::string& s, float& out){
    try { size_t p=0; float v=std::stof(s,&p); if(p!=s.size()) return false; out=v; return true; }
    catch(...) { return false; }
}
static bool parseBool(const std::string& s, bool& out){
    if(ieq(s,"1")||ieq(s,"true")||ieq(s,"on")||ieq(s,"yes")) { out=true; return true; }
    if(ieq(s,"0")||ieq(s,"false")||ieq(s,"off")||ieq(s,"no")) { out=false; return true; }
    return false;
}
// --roi "x1,y1;x2,y2;...;xN,yN"
static std::optional<thermal::Polygon> parseRoi(const std::string& s) {
    if (s.empty()) return std::nullopt;
    std::vector<int> xs, ys;
    std::stringstream ss(s);
    std::string token;
    while (std::getline(ss, token, ';')) {
        if (token.empty()) continue;
        auto comma = token.find(',');
        if (comma == std::string::npos) return std::nullopt;
        int x=0,y=0;
        if (!parseInt(token.substr(0, comma), x)) return std::nullopt;
        if (!parseInt(token.substr(comma+1), y)) return std::nullopt;
        xs.push_back(x); ys.push_back(y);
    }
    if (xs.size() < 3 || xs.size()!=ys.size()) return std::nullopt;
    thermal::Polygon poly; poly.xs=std::move(xs); poly.ys=std::move(ys);
    return poly;
}

static void printUsage() {
    std::cerr <<
R"(usage:
  thermal_cli <input_image> <output_path> [OPTIONS]

outputs:
  - stages가 1개면: <output_path> 1개 저장
  - stages가 여러 개면: <stem>_stage_01<ext>, <stem>_stage_02<ext>, ...

options (안 주면 core.hpp의 기본값 사용):
  --steps <int>           # p.stageSteps (기본 6)
  --maxK <int>            # p.maxK (기본 5, 2..7 권장)
  --stageIdx <int>        # p.stageIdx (기본 1; 2차 처리 시작 인덱스 같은 용도)
  --refine <bool>         # p.refineMode (true/false)
  --refineSteps <int>     # p.refineSteps
  --bilateral <bool>      # p.doBilateral
  --drawEdges <bool>      # p.drawEdges (슈퍼픽셀 엣지 보이기)
  --regionSize <int>      # p.regionSize
  --compactness <int>     # p.compactness
  --mrfLambda <float>     # p.mrfLambda
  --needLabelIds <bool>   # 결과에 labelIds 채워달라고 요청
  --roi "x1,y1;x2,y2;...;xN,yN"   # 폴리곤 ROI

examples:
  thermal_cli in.png out.png --steps 6 --maxK 5 --refine true --refineSteps 5 --stageIdx 1
  thermal_cli in.png out.png --roi "100,120; 500,130; 520,420; 110,430" --drawEdges true
)";
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        printUsage();
        return 1;
    }

    const std::string inPath  = argv[1];
    const std::string outPath = argv[2];

    // 기본 Params (core.hpp 기본과 동일)
    thermal::Params p{};
    bool needLabelIds = false;
    std::optional<thermal::Polygon> roi;

    // 간단한 argv 파서
    for (int i=3; i<argc; ++i) {
        std::string k = argv[i];
        auto needVal = [&](const char* name)->std::string {
            if (i+1 >= argc) { std::cerr << "missing value for " << name << "\n"; std::exit(2); }
            return argv[++i];
        };
        if (k=="--steps") {
            int v; if(!parseInt(needVal(k.c_str()), v)) { std::cerr<<"invalid --steps\n"; return 2; }
            p.stageSteps = v;
        } else if (k=="--maxK") {
            int v; if(!parseInt(needVal(k.c_str()), v)) { std::cerr<<"invalid --maxK\n"; return 2; }
            p.maxK = v;
        } else if (k=="--stageIdx") {
            int v; if(!parseInt(needVal(k.c_str()), v)) { std::cerr<<"invalid --stageIdx\n"; return 2; }
            p.stageIdx = v;
        } else if (k=="--refine") {
            bool v; if(!parseBool(needVal(k.c_str()), v)) { std::cerr<<"invalid --refine\n"; return 2; }
            p.refineMode = v;
        } else if (k=="--refineSteps") {
            int v; if(!parseInt(needVal(k.c_str()), v)) { std::cerr<<"invalid --refineSteps\n"; return 2; }
            p.refineSteps = v;
        } else if (k=="--bilateral") {
            bool v; if(!parseBool(needVal(k.c_str()), v)) { std::cerr<<"invalid --bilateral\n"; return 2; }
            p.doBilateral = v;
        } else if (k=="--drawEdges") {
            bool v; if(!parseBool(needVal(k.c_str()), v)) { std::cerr<<"invalid --drawEdges\n"; return 2; }
            p.drawEdges = v;
        } else if (k=="--regionSize") {
            int v; if(!parseInt(needVal(k.c_str()), v)) { std::cerr<<"invalid --regionSize\n"; return 2; }
            p.regionSize = v;
        } else if (k=="--compactness") {
            int v; if(!parseInt(needVal(k.c_str()), v)) { std::cerr<<"invalid --compactness\n"; return 2; }
            p.compactness = v;
        } else if (k=="--mrfLambda") {
            float v; if(!parseFloat(needVal(k.c_str()), v)) { std::cerr<<"invalid --mrfLambda\n"; return 2; }
            p.mrfLambda = v;
        } else if (k=="--needLabelIds") {
            bool v; if(!parseBool(needVal(k.c_str()), v)) { std::cerr<<"invalid --needLabelIds\n"; return 2; }
            needLabelIds = v;
        } else if (k=="--roi") {
            auto v = needVal(k.c_str());
            roi = parseRoi(v);
            if (!roi) { std::cerr << "invalid --roi format\n"; return 2; }
        } else if (k=="--help" || k=="-h" || k=="/?") {
            printUsage();
            return 0;
        } else {
            std::cerr << "unknown option: " << k << "\n";
            printUsage();
            return 2;
        }
    }

    // 1) 이미지 로드 & RGBA로 보정
    cv::Mat img = cv::imread(inPath, cv::IMREAD_UNCHANGED);
    if (img.empty()) {
        std::cerr << "load fail: " << inPath << "\n";
        return 3;
    }
    if (img.type() != CV_8UC4) {
        // BGR/BGRA -> RGBA
        if (img.channels() == 3) {
            cv::cvtColor(img, img, cv::COLOR_BGR2RGBA);
        } else if (img.channels() == 4) {
            cv::cvtColor(img, img, cv::COLOR_BGRA2RGBA);
        } else if (img.channels() == 1) {
            cv::cvtColor(img, img, cv::COLOR_GRAY2RGBA);
        } else {
            std::cerr << "unsupported channels: " << img.channels() << "\n";
            return 3;
        }
    }

    // 2) 코어 호출
    auto R = thermal::segmentTempGroups(img, roi, p, /*needLabelIds=*/needLabelIds);
    if (R.status != 0) {
        std::cerr << "segment failed: status=" << R.status << " message=" << R.message << "\n";
        return 4;
    }
    if (R.stages.empty()) {
        std::cerr << "no stages returned\n";
        return 5;
    }

    // 3) 출력 파일명 생성
    std::string stem = outPath, ext = ".png";
    if (auto pos = outPath.find_last_of('.'); pos != std::string::npos) {
        stem = outPath.substr(0, pos);
        ext  = outPath.substr(pos);
    }

    // 4) 저장
    if (R.stages.size() == 1) {
        if (!cv::imwrite(outPath, R.stages[0].rgba)) {
            std::cerr << "write fail: " << outPath << "\n";
            return 6;
        }
        std::cout << "wrote: " << outPath
                  << "  (mortarPermille=" << R.stages[0].mortarPermille
                  << ", labelId=" << R.stages[0].labelId
                  << ", q=" << R.stages[0].thresholdQ << ")\n";
    } else {
        for (size_t i = 0; i < R.stages.size(); ++i) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "_stage_%02zu", i + 1);
            const std::string path = stem + buf + ext;
            if (!cv::imwrite(path, R.stages[i].rgba)) {
                std::cerr << "write fail: " << path << "\n";
                return 6;
            }
            std::cout << "wrote: " << path
                      << "  (mortarPermille=" << R.stages[i].mortarPermille
                      << ", labelId=" << R.stages[i].labelId
                      << ", q=" << R.stages[i].thresholdQ << ")\n";
        }
    }

    // 5) 요약 로그
    std::cout << "[usedK=" << R.usedK << "] status=" << R.status
              << " message=\"" << R.message << "\"";
    if (needLabelIds && !R.labelIds.empty()) {
        std::cout << " labelIds=";
        for (size_t i=0;i<R.labelIds.size();++i) {
            if (i) std::cout << ',';
            std::cout << R.labelIds[i];
        }
    }
    std::cout << "\n";

    return 0;
}
