#include "thermal/core.hpp"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/ximgproc.hpp>
#include <queue>
#include <random>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace thermal
{

    // ===== helpers moved from JNI file (platform‑free) =====
    static cv::Rect makePolygonMask(const std::optional<Polygon> &roi, int W, int H, cv::Mat &maskOut)
    {
        maskOut = cv::Mat(H, W, CV_8U, cv::Scalar(0));
        std::vector<cv::Point> poly;
        if (roi && roi->xs.size() >= 3 && roi->xs.size() == roi->ys.size())
        {
            poly.reserve(roi->xs.size());
            for (size_t i = 0; i < roi->xs.size(); ++i)
            {
                int x = std::clamp(roi->xs[i], 0, W - 1);
                int y = std::clamp(roi->ys[i], 0, H - 1);
                poly.emplace_back(x, y);
            }
        }
        else
        {
            poly = {{0, 0}, {W - 1, 0}, {W - 1, H - 1}, {0, H - 1}};
        }
        const cv::Point *pts[1] = {poly.data()};
        int npts[1] = {(int)poly.size()};
        cv::fillPoly(maskOut, pts, npts, 1, cv::Scalar(255), cv::LINE_AA);
        cv::Rect roiRect = cv::boundingRect(poly);
        roiRect &= cv::Rect(0, 0, W, H);
        return roiRect;
    }

    static inline float quantileAt(const std::vector<float> &sortedVals, float q)
    {
        if (sortedVals.empty())
            return 0.f;
        q = std::clamp(q, 0.f, 1.f);
        int n = (int)sortedVals.size();
        if (n == 1)
            return sortedVals[0];
        float idx = q * (n - 1);
        int i = (int)std::floor(idx);
        int j = std::min(i + 1, n - 1);
        float t = idx - (float)i;
        return (1.f - t) * sortedVals[i] + t * sortedVals[j];
    }

    // 반-스텝 윈도우의 절반 폭(분위수 단위)
    // Nsteps 기준으로 스텝 간 간격은 1/(Nsteps+1).
    // "반-스텝"이면 그 반: 0.5/(Nsteps+1)
    static inline float halfStepWidthQ(int Nsteps)
    {
        return 0.5f / (float)(std::max(1, Nsteps) + 1);
    }

    Result segmentTempGroups(
        const cv::Mat &inRgba,
        const std::optional<Polygon> &roi,
        const Params &p,
        bool needLabelIds)
    {
        Result R;
        R.status = 0;
        try
        {
            if (inRgba.empty() || inRgba.type() != CV_8UC4)
            {
                R.status = -1;
                R.message = "Input must be CV_8UC4 RGBA";
                return R;
            }
            const int W = inRgba.cols, H = inRgba.rows;
            R.stages.clear();

            // --- 변환 및 ROI ---
            cv::Mat bgr;
            cv::cvtColor(inRgba, bgr, cv::COLOR_RGBA2BGR);

            cv::Mat maskFull(H, W, CV_8UC1, cv::Scalar(255));
            cv::Rect roiRect(0, 0, W, H);
            if (roi && !roi->xs.empty() && roi->xs.size() == roi->ys.size())
            {
                maskFull.setTo(0);
                std::vector<cv::Point> pts;
                pts.reserve(roi->xs.size());
                for (size_t i = 0; i < roi->xs.size(); ++i)
                {
                    int px = std::clamp(roi->xs[i], 0, W - 1);
                    int py = std::clamp(roi->ys[i], 0, H - 1);
                    pts.emplace_back(px, py);
                }
                cv::fillPoly(maskFull, std::vector<std::vector<cv::Point>>{pts}, cv::Scalar(255));
                roiRect = cv::boundingRect(pts);

                // 빈/이상 ROI면 전체로 fallback (기존처럼 에러로 돌리고 싶으면 아래 블록 대신 에러 리턴)
                if (roiRect.width <= 0 || roiRect.height <= 0)
                {
                    maskFull.setTo(255);
                    roiRect = cv::Rect(0, 0, W, H);
                }
            }

            cv::Mat roiBGR = bgr(roiRect).clone();
            cv::Mat roiMask = maskFull(roiRect).clone();

            if (p.doBilateral && roiBGR.type() == CV_8UC3)
            {
                cv::Mat tmp;
                cv::bilateralFilter(roiBGR, tmp, 5, 15, 3);
                roiBGR = tmp;
            }

            cv::Mat roiBGR32f;
            roiBGR.convertTo(roiBGR32f, CV_32F, 1.0 / 255.0);
            cv::Mat roiLab;
            cv::cvtColor(roiBGR32f, roiLab, cv::COLOR_BGR2Lab);

            // --- tMap 계산 (Lab -> L/chroma 기반 스코어) ---
            const float W_L = 0.80f, W_W = 0.20f, CHROMA_NORM = 110.f;
            cv::Mat tMap(roiRect.size(), CV_32F, cv::Scalar(0));
            for (int y = 0; y < roiRect.height; ++y)
            {
                const uchar *Mp = roiMask.ptr<uchar>(y);
                const cv::Vec3f *Lp = roiLab.ptr<cv::Vec3f>(y);
                float *Tp = tMap.ptr<float>(y);
                for (int x = 0; x < roiRect.width; ++x)
                {
                    if (!Mp[x])
                    {
                        Tp[x] = 0.f;
                        continue;
                    }
                    auto lab = Lp[x];
                    float L = std::clamp(lab[0] / 100.f, 0.f, 1.f);
                    float a = lab[1], b = lab[2];
                    float C = std::sqrt(a * a + b * b);
                    float whiten = 1.f - std::clamp(C / CHROMA_NORM, 0.f, 1.f);
                    float s = W_L * L + W_W * whiten;
                    Tp[x] = s;
                }
            }
            // LUT via empirical CDF
            std::vector<float> allS;
            allS.reserve(roiRect.width * roiRect.height);
            for (int y = 0; y < roiRect.height; ++y)
            {
                const uchar *Mp = roiMask.ptr<uchar>(y);
                const float *Sp = tMap.ptr<float>(y);
                for (int x = 0; x < roiRect.width; ++x) {
                    if (Mp[x]) allS.push_back(Sp[x]);
                }
            }
            if (allS.size() < 100)
            {
                R.status = -6;
                R.message = "Too few pixels in ROI";
                return R;
            }
            std::sort(allS.begin(), allS.end());
            std::vector<float> pk(256), tk(256);
            for (int i = 0; i < 256; i++)
            {
                float q = (float)i / 255.f;
                int id = std::clamp((int)std::round(q * (int(allS.size()) - 1)), 0, (int)allS.size() - 1);
                pk[i] = allS[id];
                tk[i] = q;
            }
            auto lerp1D = [&](float x)
            { 
                if(x<=pk.front()) return tk.front(); 
                if(x>=pk.back()) return tk.back(); 
                auto it=std::upper_bound(pk.begin(),pk.end(),x); 
                int j=(int)std::distance(pk.begin(),it); 
                int i=j-1; 
                float t=(x-pk[i])/(pk[j]-pk[i]+1e-12f); 
                return tk[i]*(1.f-t)+tk[j]*t; 
            };
            for (int y = 0; y < roiRect.height; ++y)
            {
                const uchar *Mp = roiMask.ptr<uchar>(y);
                float *Sp = tMap.ptr<float>(y);
                for (int x = 0; x < roiRect.width; ++x) {
                    if (Mp[x]) Sp[x] = std::clamp(lerp1D(Sp[x]), 0.f, 1.f);
                }
            }

            // === ROI 픽셀 총수 (permille 분모)
            const int roiPixelsTotal = cv::countNonZero(roiMask);
            if (roiPixelsTotal <= 0) {
                R.status = -6;
                R.message = "Too few pixels in ROI";
                return R;
            }

            // --- 임계값 목록 thresholds 생성: refineMode 여부에 따라 방식 분기 ---
            std::vector<float> thresholds;
            if (!p.refineMode) {
                // 1차: 전 범위 등분 (절대 분위수) — Sidx=1..N, q=Sidx/(N+1)
                const int N = std::max(1, p.stageSteps);
                thresholds.reserve(N);
                for (int Sidx = 1; Sidx <= N; ++Sidx) {
                    float qAbs = (float)Sidx / (float)(N + 1);
                    thresholds.push_back(qAbs);
                }
            } else {
                // 2차: 선택 스테이지 Sidx 중심 "반-스텝" 윈도우를 refineSteps로 등분
                const int N  = std::max(1, p.stageSteps);
                const int RS = std::max(1, p.refineSteps);
                const int Sidx  = std::clamp(p.stageIdx, 1, N);
                const float qCenter = (float)Sidx / (float)(N + 1);
                const float hw      = halfStepWidthQ(N); // 반-스텝 폭
                const int RS_lo = (RS + 1) / 2;   // 하위 절반 (반올림 상향)
                const int RS_hi = RS - RS_lo;     // 상위 절반
                const float rLo = qCenter - hw;
                const float rHi = qCenter + hw;
                float sL = std::max(1.0f, float(Sidx) - 0.4f);
                float sR = std::min(float(N), float(Sidx) + 0.4f);

                thresholds.clear();
                thresholds.reserve(RS);

                for (int k = 0; k < RS; ++k) {
                    float t = float(k) / float(RS - 1);      // 0..1
                    float sFrac = sL * (1.f - t) + sR * t;   // 연속 스테이지 값 (예: 2.6, 2.7, ...)
                    float q = sFrac / float(N + 1);          // 정규화 좌표로 변환
                    thresholds.push_back(std::clamp(q, 0.f, 1.f));
                }
            }

            // --- 단일 루프: 각 임계마다 결과 한 장 생성 ---
            cv::Mat baseRgba = inRgba.clone();
            for (float T : thresholds) {
                // (1) ROI 크기의 이진 마스크: tMap만으로 판정 (ROI 게이팅 금지)
                thermal::Payload payload;
                payload.thresholdQ = T;

                cv::Mat stageMaskRoi(roiRect.size(), CV_8UC1, cv::Scalar(0));
                for (int y = 0; y < roiRect.height; ++y) {
                    const float *Sp = tMap.ptr<float>(y);
                    uchar *Dp       = stageMaskRoi.ptr<uchar>(y);
                    for (int x = 0; x < roiRect.width; ++x) {
                        Dp[x] = (Sp[x] >= T) ? 255 : 0;
                    }
                }

                // (선택) morphology: 너무 깎이면 잠시 비활성화 권장
                if (p.doBilateral) {
                    cv::morphologyEx(stageMaskRoi, stageMaskRoi, cv::MORPH_OPEN,
                                    cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3,3)));
                }

                // (2) ROI 제한 적용
                cv::bitwise_and(stageMaskRoi, roiMask, stageMaskRoi);

                // === 스테이지별 permille 계산 ===
                const int selInRoi = cv::countNonZero(stageMaskRoi);
                const int unselInRoi = std::max(0, roiPixelsTotal - selInRoi);
                const double ratio = (roiPixelsTotal > 0) ? static_cast<double>(unselInRoi) / static_cast<double>(roiPixelsTotal) : 0.0;
                const float permilleF = static_cast<float>(std::round(ratio * 100000.0) / 100.0);
                payload.mortarPermille = permilleF;

                // (3) 전체 프레임 마스크로 맵핑
                cv::Mat selMask(H, W, CV_8UC1, cv::Scalar(0));
                stageMaskRoi.copyTo(selMask(roiRect));

                // (4) 합성: 선택 픽셀만 원본 통과, 나머지는 블랙
                payload.rgba = cv::Mat(H, W, CV_8UC4, cv::Scalar(0,0,0,255));
                baseRgba.copyTo(payload.rgba, selMask);

                R.stages.emplace_back(std::move(payload));
            }

            R.usedK = std::max(1, std::min(p.maxK, 5));
            return R;
        }
        catch (const cv::Exception &e)
        {
            R.status = -100;
            R.message = e.what();
            return R;
        }
    }
}