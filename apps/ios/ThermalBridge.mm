// ThermalBridge.mm
#import "ThermalBridge.h"
#import <CoreGraphics/CoreGraphics.h>
#import "thermal/core.hpp"

using namespace thermal;

@implementation TRStagePayload @end
@implementation TRResult @end
@implementation TRParams @end

static cv::Mat UIImageToMatRGBA(UIImage *img) {
    CGImageRef imageRef = img.CGImage;
    const size_t width  = CGImageGetWidth(imageRef);
    const size_t height = CGImageGetHeight(imageRef);

    cv::Mat mat((int)height, (int)width, CV_8UC4);
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(mat.data, width, height, 8, mat.step[0],
                                             colorSpace, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGContextDrawImage(ctx, CGRectMake(0,0,width,height), imageRef);
    CGContextRelease(ctx);
    CGColorSpaceRelease(colorSpace);
    return mat;
}

static UIImage* MatToUIImage(const cv::Mat &matRGBA) {
    NSCAssert(matRGBA.type() == CV_8UC4, @"Must be CV_8UC4");
    const int width  = matRGBA.cols;
    const int height = matRGBA.rows;

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate((void*)matRGBA.data, width, height, 8, matRGBA.step[0],
                                             colorSpace, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGImageRef imageRef = CGBitmapContextCreateImage(ctx);
    UIImage *img = [UIImage imageWithCGImage:imageRef scale:[UIScreen mainScreen].scale orientation:UIImageOrientationUp];
    CGImageRelease(imageRef);
    CGContextRelease(ctx);
    CGColorSpaceRelease(colorSpace);
    return img;
}

@implementation ThermalBridge

+ (TRResult *)processImage:(UIImage *)image
                      roiX:(NSArray<NSNumber*> * _Nullable)roiX
                      roiY:(NSArray<NSNumber*> * _Nullable)roiY
                    params:(TRParams *)params
{
    cv::Mat rgba = UIImageToMatRGBA(image);

    std::optional<Polygon> poly = std::nullopt;
    if (roiX && roiY && roiX.count == roiY.count && roiX.count > 2) {
        Polygon P;
        P.xs.reserve(roiX.count);
        P.ys.reserve(roiY.count);
        for (NSUInteger i=0; i<roiX.count; ++i) {
            P.xs.push_back(roiX[i].intValue);
            P.ys.push_back(roiY[i].intValue);
        }
        poly = std::move(P);
    }

    Params p;
    p.regionSize   = params.regionSize;
    p.compactness  = params.compactness;
    p.doBilateral  = params.doBilateral;
    p.drawEdges    = params.drawEdges;
    p.mrfLambda    = params.mrfLambda;
    p.maxK         = params.maxK;
    p.renderMaxK   = params.renderMaxK;
    p.stageIdx     = params.stageIdx;
    p.stageSteps   = params.stageSteps;
    p.refineMode   = params.refineMode;
    p.refineSteps  = params.refineSteps;

    Result R = segmentTempGroups(rgba, poly, p, /*needLabelIds=*/false);

    TRResult *out = [TRResult new];
    out.usedK  = R.usedK;
    out.status = R.status;
    out.message = [NSString stringWithUTF8String:R.message.c_str()];

    NSMutableArray<TRStagePayload*> *stages = [NSMutableArray arrayWithCapacity:R.stages.size()];
    for (const auto &pl : R.stages) {
        TRStagePayload *sp = [TRStagePayload new];
        sp.image = MatToUIImage(pl.rgba);
        sp.mortarPermille = pl.mortarPermille;
        sp.labelId = pl.labelId;
        sp.thresholdQ = pl.thresholdQ;
        [stages addObject:sp];
    }
    out.stages = stages;
    return out;
}
@end
