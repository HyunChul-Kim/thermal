// ThermalBridge.h
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

/// 스테이지 결과 1개
@interface TRStagePayload : NSObject
@property(nonatomic, strong) UIImage *image;       // RGBA 결과
@property(nonatomic, assign) float mortarPermille; // 소수%까지 표현하고 싶으면 %값으로 만들어도 됨
@property(nonatomic, assign) NSInteger labelId;
@property(nonatomic, assign) float thresholdQ;     // 0..1
@end

/// 전체 결과
@interface TRResult : NSObject
@property(nonatomic, strong) NSArray<TRStagePayload*> *stages;
@property(nonatomic, assign) NSInteger usedK;
@property(nonatomic, assign) NSInteger status;
@property(nonatomic, copy) NSString *message;
@end

/// 파라미터
@interface TRParams : NSObject
@property(nonatomic, assign) int regionSize;
@property(nonatomic, assign) int compactness;
@property(nonatomic, assign) BOOL doBilateral;
@property(nonatomic, assign) BOOL drawEdges;
@property(nonatomic, assign) float mrfLambda;
@property(nonatomic, assign) int maxK;
@property(nonatomic, assign) int renderMaxK;
@property(nonatomic, assign) int stageIdx;
@property(nonatomic, assign) int stageSteps;
@property(nonatomic, assign) BOOL refineMode;
@property(nonatomic, assign) int refineSteps;
@end

@interface ThermalBridge : NSObject
/// roiX/roiY: 같은 길이, image 좌표계
+ (TRResult *)processImage:(UIImage *)image
                      roiX:(NSArray<NSNumber*> * _Nullable)roiX
                      roiY:(NSArray<NSNumber*> * _Nullable)roiY
                    params:(TRParams *)params;
@end

NS_ASSUME_NONNULL_END
