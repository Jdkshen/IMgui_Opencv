# OpenCV 算法文档

> 本文档列出项目中所有 OpenCV 算法，包含用途、函数签名、处理流程、参数说明和扩展指南。

---

## 目录

- [1. 图像处理管线 (ThresholdTool)](#1-图像处理管线-thresholdtool)
- [2. 模板匹配 (TemplateMatch)](#2-模板匹配-templatematch)
- [3. YOLO 目标检测 (YOLODetector)](#3-yolo-目标检测-yolodetector)
- [4. 全局图像变量](#4-全局图像变量)
- [5. 添加新算法的步骤](#5-添加新算法的步骤)
- [6. 常见崩溃模式与防护](#6-常见崩溃模式与防护)

---

## 1. 图像处理管线 (ThresholdTool)

**文件**: `Algorithm/ThresholdTool.h` / `Algorithm/ThresholdTool.cpp`

### 功能
对 `gImage` 执行可配置的图像处理管线，结果通过 DX12 纹理渲染到 ImGui 窗口。

### 核心 API

```cpp
namespace ThresholdTool {
    void ShowThresholdWindow();  // 显示阈值调试窗口（UI）
    void ApplyProcess();         // 执行处理管线
}
```

### 处理管线流程

```
gImage (BGR/BGRA)
  │
  ├─[灰度转换]──→ cv::cvtColor(BGR/BGRA → GRAY)  (可选, gUseGray=on)
  │
  ├─[高斯模糊]──→ cv::GaussianBlur()              (可选, gPipe.enableBlur=on)
  │
  ├─[二值化]────→ cv::threshold()                 (可选, gPipe.enableThreshold=on)
  │  └─[Canny]──→ cv::Canny()                    (可选, gPipe.enableCanny=on)
  │
  └─[RGBA转换]──→ cv::cvtColor(GRAY/BGR/BGRA → RGBA)
                   └─ 输出到 gPendingUpload, 标记 gNeedUpload=true
```

### PipelineState 参数

```cpp
struct PipelineState {
    bool enableBlur       = false;  // 是否高斯模糊
    bool enableThreshold  = false;  // 是否二值化
    bool enableCanny      = false;  // 是否 Canny 边缘
    int  blurSize         = 5;      // 模糊核大小 (奇数, ≥3)
    int  threshold        = 128;    // 二值化阈值 (0-255)
    int  cannyLow         = 50;     // Canny 低阈值
    int  cannyHigh        = 150;    // Canny 高阈值
};
```

### OpenCV 函数使用

| 步骤 | 函数 | 说明 |
|------|------|------|
| 灰度 | `cv::cvtColor(src, dst, COLOR_BGR2GRAY)` | 3→1通道 |
| 灰度 | `cv::cvtColor(src, dst, COLOR_BGRA2GRAY)` | 4→1通道 |
| 模糊 | `cv::GaussianBlur(src, dst, Size(k,k), 0)` | k=blurSize*2+1 |
| 二值化 | `cv::threshold(src, dst, t, 255, THRESH_BINARY)` | t=阈值 |
| Canny | `cv::Canny(src, dst, low, high)` | 双阈值 |
| 输出 | `cv::cvtColor(result, rgba, COLOR_GRAY2RGBA)` | 灰度回RGBA |
| 输出 | `cv::cvtColor(result, rgba, COLOR_BGR2RGBA)` | BGR转RGBA |

---

## 2. 模板匹配 (TemplateMatch)

**文件**: `Algorithm/TemplateMatch.h` / `Algorithm/TemplateMatch.cpp`

### 功能
在 `gImage` 中搜索模板图像，支持旋转、预处理（灰度/二值化/边缘），结果用 NMS 去重后绘制蓝色矩形。

### 核心 API

```cpp
namespace TemplateMatch {
    void Run();                       // 同步执行匹配
    void RunAsync();                  // 异步执行（后台线程）
    void CheckAsyncResult();          // 每帧检查异步结果
    void ShowWindow();                // 调试窗口
    void ShowTemplateEditor();        // 模板编辑弹窗
    void DrawMatches(ImDrawList* dl); // 绘制匹配结果
    void Clear();                     // 清空结果
}
```

### 处理流程

```
1. 模板预处理 (cv::cvtColor + cv::threshold + cv::Canny)
2. 源图预处理 (同模板)
3. 循环旋转角度 [start, end, step]:
     cv::getRotationMatrix2D()  →  cv::warpAffine()
     cv::matchTemplate()        →  cv::minMaxLoc()
4. NMS去重: cv::dnn::NMSBoxes()
5. 绘制结果 → gMatchROIs
```

### 关键参数 (extern)

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `g_TMMatchThreshold` | float | 0.6f | 匹配分数阈值 |
| `g_TMMaxResults` | int | 10 | 最大结果数 |
| `g_TMEnableRotation` | bool | false | 启用旋转搜索 |
| `g_TMRotationStart` | int | -30 | 旋转起始角度 |
| `g_TMRotationEnd` | int | 30 | 旋转结束角度 |
| `g_TMRotationStep` | int | 5 | 旋转步长 |
| `g_NmsThreshold` | float | 0.4f | NMS 阈值 |
| `g_TplGray` | bool | false | 模板灰度化 |
| `g_TplBinary` | bool | false | 模板二值化 |
| `g_TplEdge` | bool | false | 模板边缘检测 |

### OpenCV 函数使用

| 步骤 | 函数 | 说明 |
|------|------|------|
| 灰度 | `cv::cvtColor(src, dst, COLOR_BGR2GRAY)` | 颜色→灰度 |
| 二值化 | `cv::threshold(src, dst, t, 255, THRESH_BINARY)` | |
| 边缘 | `cv::Canny(src, dst, low, high)` | |
| 旋转 | `cv::getRotationMatrix2D(center, angle, 1.0)` | 生成旋转矩阵 |
| 旋转 | `cv::warpAffine(src, dst, M, size)` | 应用旋转 |
| 匹配 | `cv::matchTemplate(img, tpl, result, TM_CCOEFF_NORMED)` | 模板匹配 |
| 极值 | `cv::minMaxLoc(result, &minV, &maxV, &minP, &maxP)` | 找最佳匹配 |
| NMS | `cv::dnn::NMSBoxes(boxes, scores, thresh, nmsThresh, indices)` | 去重 |

---

## 3. YOLO 目标检测 (YOLODetector)

**文件**: `Algorithm/YOLODetector.h` / `Algorithm/YOLODetector.cpp`

### 功能
使用 ONNX Runtime 加载 YOLO 模型，对 `gImage` 执行目标检测，支持 ROI 限定区域。

### 核心 API

```cpp
namespace YOLODetector {
    bool LoadModel(const std::string& onnxPath, const std::string& classesPath);
    bool IsLoaded();
    std::vector<DetectedObject> Detect(
        const cv::Mat& image,
        float confThreshold = 0.5f,
        float nmsThreshold  = 0.4f,
        cv::Rect roi        = cv::Rect()
    );
    void DrawDetections(cv::Mat& image, const std::vector<DetectedObject>& objects, bool drawLabel = true);
    void Unload();
}
```

### 结果结构体

```cpp
struct DetectedObject {
    cv::Rect box;           // 检测框 (x, y, w, h)
    int   classId;          // 类别ID
    float confidence;       // 置信度 [0,1]
    std::string className;  // 类别名称
};
```

### 处理流程

```
图像 → cv::dnn::blobFromImage() → ONNX Runtime 推理
  → 解析输出张量 [1, C, N] 或 [C, N]
  → 逐候选框计算置信度 → 过滤低于阈值的
  → cv::dnn::NMSBoxes() 去重
  → 返回 DetectedObject 列表
```

### 关键参数

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `confThreshold` | float | 0.5f | 置信度阈值 (0-1) |
| `nmsThreshold` | float | 0.4f | NMS 去重阈值 (0-1) |
| `roi` | cv::Rect | 全图 | 限定检测区域 |

### OpenCV / ORT 函数使用

| 步骤 | 函数 | 说明 |
|------|------|------|
| 预处理 | `cv::dnn::blobFromImage(img, 1/255, Size(640,640), Scalar(), true, false)` | 缩放+归一化+RGB |
| 推理 | `Ort::Session::Run()` | ONNX Runtime 前向推理 |
| 后处理 | 手动解析 float* 张量 | 提取 cx,cy,w,h,confidences |
| NMS | `cv::dnn::NMSBoxes(boxes, scores, thresh, nmsThresh, indices)` | 去重 |
| 绘制 | `cv::rectangle(img, box, color, thickness)` | 画框 |
| 绘制 | `cv::getTextSize(label, FONT_HERSHEY_SIMPLEX, ...)` | 文字尺寸 |
| 绘制 | `cv::putText(img, label, pos, FONT, scale, color, thick, LINE_AA)` | 画标签 |

---

## 4. 全局图像变量

所有算法共享这些全局变量（定义在 `ThresholdTool.cpp`, 声明在 `ThresholdTool.h`）：

```cpp
extern cv::Mat gImage;              // 当前处理图像 (BGR/BGRA，算法输入)
extern cv::Mat gOriginalImage;      // 原始图像备份
extern cv::Mat gPendingUpload;      // 待上传到 GPU 的 RGBA 图像
extern cv::Mat gThresholdMat;       // 阈值处理中间结果
extern bool    gNeedUpload;         // GPU 上传标志
```

### 图像数据流

```
文件/摄像头
  │
  ├─ cv::imread() / cv::VideoCapture::read()
  │
  └─→ gImage (BGR/BGRA)
        │
        ├─ 算法处理 → gPendingUpload (RGBA)
        │               └─ gNeedUpload = true
        │                   └─ UploadToDX12() → GPU 纹理
        │
        └─ ImGui::Image() 显示
```

### 图像格式约定

| channels | 格式 | OpenCV 常量 |
|----------|------|-------------|
| 1 | 灰度 | `CV_8UC1` |
| 3 | BGR | `CV_8UC3` |
| 4 | BGRA | `CV_8UC4` |

> ⚠️ **所有 RGBA 输出必须是 RGBA 顺序**（DX12 纹理格式是 `R8G8B8A8_UNORM`）

---

## 5. 添加新算法的步骤

以添加 "Blob分析" 为例：

### Step 1: 创建算法文件

```cpp
// Algorithm/BlobAnalysis.h
#pragma once
#include <opencv2/opencv.hpp>
#include <vector>

struct BlobResult {
    cv::Point center;
    double area;
    cv::Rect bbox;
};

namespace BlobAnalysis {
    void Process(const cv::Mat& image, std::vector<BlobResult>& out);
}
```

### Step 2: 实现算法

```cpp
// Algorithm/BlobAnalysis.cpp
#include "BlobAnalysis.h"

namespace BlobAnalysis {
    void Process(const cv::Mat& image, std::vector<BlobResult>& out) {
        if (image.empty()) return;  // ⚠️ 必须加空图守卫！

        cv::Mat gray, binary;
        if (image.channels() > 1)
            cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
        else gray = image;

        cv::threshold(gray, binary, 128, 255, cv::THRESH_BINARY);

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        for (auto& c : contours) {
            BlobResult r;
            r.area = cv::contourArea(c);
            r.bbox = cv::boundingRect(c);
            // ...
            out.push_back(r);
        }
    }
}
```

### Step 3: 在 ToolsWindow 注册

```cpp
// 1. 在 kToolNames 添加
const char* kToolNames[] = { "边缘检测", "模板匹配", "Blob分析", "阈值调试", "YOLO检测" };
// Blob分析 就是 type == 2, 已经在列表中

// 2. 在 ShowToolsWindow() 的 type==2 分支实现 UI
else if (type == 2) {
    auto& it = g_ToolInstances[inst];
    ImGui::SliderInt("最小面积", &blobMinArea, 1, 10000);
    if (ImGui::Button("执行Blob分析")) {
        if (gImage.empty()) {  // ⚠️ 必须加空图守卫！
            LogSystem::Add(LOG_WARN, "请先加载图片");
        } else {
            std::vector<BlobResult> results;
            BlobAnalysis::Process(gImage, results);
            // 结果处理...
        }
    }
}

// 3. 在状态机的工具执行部分添加
else if (t == 2) {
    std::vector<BlobResult> results;
    BlobAnalysis::Process(gImage, results);
    // ...
}
```

### Step 4: 更新 vcxproj

在 `Windows_imgui.vcxproj` 和 `.vcxproj.filters` 中添加新文件的 `ClInclude`/`ClCompile` 项。

### 结果输出模板

```cpp
// 算法产生图片结果 → 使用 gPendingUpload
cv::Mat rgba;
if (result.channels() == 1)      cv::cvtColor(result, rgba, cv::COLOR_GRAY2RGBA);
else if (result.channels() == 3) cv::cvtColor(result, rgba, cv::COLOR_BGR2RGBA);
else                              cv::cvtColor(result, rgba, cv::COLOR_BGRA2RGBA);
gPendingUpload = rgba;
gNeedUpload = true;

// 算法产生数据结果 → 使用 LogSystem
LogSystem::Add(LOG_INFO, "Blob分析: %zu 个区域", results.size());
```

---

## 6. 常见崩溃模式与防护

### ❌ 模式1：空图像 cvtColor

```cpp
// 崩溃！空 Mat 的 channels()==0，掉入 else → 崩溃
cv::Mat rgba;
if (img.channels() == 1)      cv::cvtColor(img, rgba, cv::COLOR_GRAY2RGBA);
else if (img.channels() == 3) cv::cvtColor(img, rgba, cv::COLOR_BGR2RGBA);
else                           cv::cvtColor(img, rgba, cv::COLOR_BGRA2RGBA);
```

```cpp
// ✅ 正确：先检查空图
if (img.empty()) return;
cv::Mat rgba;
if (img.channels() == 1)      cv::cvtColor(img, rgba, cv::COLOR_GRAY2RGBA);
else if (img.channels() == 3) cv::cvtColor(img, rgba, cv::COLOR_BGR2RGBA);
else                           cv::cvtColor(img, rgba, cv::COLOR_BGRA2RGBA);
```

### ❌ 模式2：ImGui Begin/End 之间的 try/catch

```cpp
// 崩溃！异常跳过 End()，ImGui 栈损坏 → MissingEndChild
try {
    ImGui::Begin("窗口");
    cv::cvtColor(emptyMat, ...);  // 抛出 cv::Exception
    ImGui::End();
} catch (...) {}
```

```cpp
// ✅ 正确：在 Begin 之前检查
if (gImage.empty()) { ShowWarning(); return; }
ImGui::Begin("窗口");
// ... OpenCV 操作
ImGui::End();
```

### ❌ 模式3：未检查 gImage.empty() 的执行按钮

```cpp
// ✅ 所有执行按钮必须加守卫
if (ImGui::Button("执行")) {
    if (gImage.empty()) {
        LogSystem::Add(LOG_WARN, "请先加载图片");
    } else {
        DoOpenCVWork();
    }
}
```

---

> 最后更新: 2026-05-30 | 编译: MSVC + OpenCV 4.12.0 + ONNX Runtime
