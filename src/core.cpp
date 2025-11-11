#ifdef _WIN32
#ifndef THERMAL_BUILD_DLL
#define THERMAL_BUILD_DLL 1
#endif
#endif

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

    // helpers moved from JNI file (platform‑free)
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

    // Half-step window width (in quantiles)
    // The step spacing is 1/(Nsteps+1) for Nsteps.
    // If "half-step", then half: 0.5/(Nsteps+1)
    static inline float halfStepWidthQ(int Nsteps)
    {
        return 0.5f / (float)(std::max(1, Nsteps) + 1);
    }

    THERMAL_API Result segmentTempGroups(
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

            // Conversion and ROI
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

                // If ROI is empty/abnormal, fallback to the entire ROI (if you want to return it as an error like before, return an error instead of the block below)
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

            // tMap calculation (Lab -> L/chroma based score)
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

            // ROI pixel count (permille denominator)
            const int roiPixelsTotal = cv::countNonZero(roiMask);
            if (roiPixelsTotal <= 0) {
                R.status = -6;
                R.message = "Too few pixels in ROI";
                return R;
            }

            // Generate a list of thresholds: branching methods based on refineMode
            std::vector<float> thresholds;
            if (!p.refineMode) {
                // 1st: Equalize the entire range (absolute quantiles) - Sidx=1..N, q=Sidx/(N+1)
                const int N = std::max(1, p.stageSteps);
                thresholds.reserve(N);
                for (int Sidx = 1; Sidx <= N; ++Sidx) {
                    float qAbs = (float)Sidx / (float)(N + 1);
                    thresholds.push_back(qAbs);
                }
            } else {
                // 2nd: Divide the selection stage Sidx-centered "half-step" window into refineSteps
                const int N  = std::max(1, p.stageSteps);
                const int RS = std::max(1, p.refineSteps);
                const int Sidx  = std::clamp(p.stageIdx, 1, N);
                const float qCenter = (float)Sidx / (float)(N + 1);
                const float hw      = halfStepWidthQ(N); // Half-step width
                const int RS_lo = (RS + 1) / 2;   // Lower half (rounded up)
                const int RS_hi = RS - RS_lo;     // top half
                const float rLo = qCenter - hw;
                const float rHi = qCenter + hw;
                float sL = std::max(1.0f, float(Sidx) - 0.4f);
                float sR = std::min(float(N), float(Sidx) + 0.4f);

                thresholds.clear();
                thresholds.reserve(RS);

                for (int k = 0; k < RS; ++k) {
                    float t = float(k) / float(RS - 1);      // 0..1
                    float sFrac = sL * (1.f - t) + sR * t;   // successive stage values (e.g. 2.6, 2.7, ...)
                    float q = sFrac / float(N + 1);          // Convert to normalized coordinates
                    thresholds.push_back(std::clamp(q, 0.f, 1.f));
                }
            }

            // Single loop: produces one result for each threshold.
            cv::Mat baseRgba = inRgba.clone();
            for (float T : thresholds) {
                // Binary mask of ROI size: judged by tMap only (ROI gating prohibited)
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

                // morphology: If it is too sharp, it is recommended to temporarily disable it.
                if (p.doBilateral) {
                    cv::morphologyEx(stageMaskRoi, stageMaskRoi, cv::MORPH_OPEN,
                                    cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3,3)));
                }

                // Apply ROI limits
                cv::bitwise_and(stageMaskRoi, roiMask, stageMaskRoi);

                // calculate permille by stages
                const int selInRoi = cv::countNonZero(stageMaskRoi);
                const int unselInRoi = std::max(0, roiPixelsTotal - selInRoi);
                const double ratio = (roiPixelsTotal > 0) ? static_cast<double>(unselInRoi) / static_cast<double>(roiPixelsTotal) : 0.0;
                const float permilleF = static_cast<float>(std::round(ratio * 100000.0) / 100.0);
                payload.mortarPermille = permilleF;

                // Mapping with full frame mask
                cv::Mat selMask(H, W, CV_8UC1, cv::Scalar(0));
                stageMaskRoi.copyTo(selMask(roiRect));

                // Compositing: Only selected pixels pass through the original, the rest are black.
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