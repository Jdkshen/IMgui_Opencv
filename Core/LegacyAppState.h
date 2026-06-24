#pragma once

#include <string>
#include <vector>

#include <d3d12.h>
#include <opencv2/core/mat.hpp>

#include "imgui/imgui.h"
#include "../Algorithm/YOLODetector.h"

extern cv::Mat gImage;
extern cv::Mat gOriginalImage;
extern cv::Mat gPendingUpload;
extern cv::Mat gThresholdMat;
extern bool gNeedUpload;
extern int gImageWidth;
extern int gImageHeight;
extern int g_ImageVersion;

extern cv::Mat g_FrozenTemplate;
extern float gTimeTotal;
extern float g_McfLastTimeMs;
extern int g_McfLastCount;

extern ID3D12Resource* gTexture;
extern std::vector<ID3D12Resource*> gPendingReleaseTextures;

extern std::vector<DetectedObject> g_YoloOverlays;
extern bool g_YoloShowOverlay;
extern float g_YoloDetailTotalMs;
extern float g_YoloDetailPreMs;
extern float g_YoloDetailInfMs;
extern float g_YoloDetailPostMs;

extern std::vector<std::string> gImageList;
extern int gCurrentImageIndex;
