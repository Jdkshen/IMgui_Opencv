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
- [7. 多点找色 (MultiColorFinder)](#7-多点找色-multicolorfinder)

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

## 4. VisionContext — 统一视觉上下文

所有算法通过 `VisionContext` 获取输入和共享状态（定义在 `Core/VisionContext.h/.cpp`）：

```cpp
struct VisionContext {
    cv::Mat image;                  // 当前处理图像 (BGR，曾为 gImage)
    cv::Mat originalImage;          // 原始图像备份
    cv::Mat frozenTemplate;         // 冻结模板（形状匹配等）

    std::vector<ROI> rois;          // ROI 列表
    int selectedROI = -1;           // 当前选中 ROI

    std::vector<ToolResult> unifiedResults; // 统一工具输出

    int imageVersion = 0;           // 图像版本号
    float zoom = 1.0f;              // 视图缩放
    ImVec2 pan, canvasSize, imageScreenPos; // 视图变换

    // 便捷方法
    bool HasROI() const;
    cv::Rect GetActiveROIRect() const;
    float GetActiveROIRadius() const;
    const std::vector<ImVec2>& GetActiveROIPolygon() const;
};

// 全局单例
extern VisionContext gContext;
```

### 图像数据流

```
文件/摄像头
  │
  ├─ cv::imread() / cv::VideoCapture::read()
  │
  └─→ gImage (BGR/BGRA) ──同步──→ gContext.image
        │
        ├─ 算法处理 → gPendingUpload (RGBA) → GPU 纹理
        │
        └─ ITool::Execute(gContext) → ToolResult
              └─ gContext.unifiedResults → DrawUnifiedResults()
```

### 向后兼容

当前 type 0-11、13 工具统一通过 `ITool + VisionContext + ToolResult` 执行，并通过 `gContext.unifiedResults` 发布结果；旧的 `g_UnifiedResults` 影子状态已删除。部分工具仍会在图像处理链路中更新 `gImage`、`gPendingUpload`、`gNeedUpload` 等状态，后续新增工具仍应优先走统一接口。

---

## 5. ITool 接口 + ToolResult 统一输出

### ToolResult 结构（`Algorithm/ToolResult.h`）

```cpp
struct ToolResult {
    std::string toolName;
    bool success = true;
    std::string message;

    struct Measurement { std::string name; double value = 0; std::string unit; };
    std::vector<Measurement> measurements;

    struct Region {
        std::vector<cv::Point> contour;
        cv::Rect bbox;
        float area = 0;
        float score = 0;
        std::string label;
    };
    std::vector<Region> regions;        // 轮廓/Blob/形状匹配区域

    struct Detection {
        cv::Rect box;
        int classId = -1;
        float score = 0;
        std::string label;
    };
    std::vector<Detection> detections;  // YOLO/分类检测框

    struct Line { cv::Point p1, p2; float length = 0; float angle = 0; };
    std::vector<Line> lines;            // 直线检测

    struct TextItem { std::string text; cv::Rect box; float confidence = 0; };
    std::vector<TextItem> texts;        // OCR 文本框、文本内容和置信度

    cv::Mat debugImage;                 // 可选调试图像
};
```

### ITool 接口（`Algorithm/ITool.h`）

```cpp
class ITool {
public:
    virtual const char* GetName() const = 0;
    virtual int GetType() const = 0;
    virtual ToolResult Execute(VisionContext& ctx) = 0;  // 核心
    virtual void DrawUI() = 0;
    virtual nlohmann::json Save() const = 0;
    virtual void Load(const nlohmann::json& j) = 0;

    static std::unique_ptr<ITool> Create(int type); // 工厂
};
```

### 已接入工具

| type | 类名 | 文件 |
|------|------|------|
| 4 | `YOLOTool` | `Algorithm/YOLOTool.h/.cpp` |
| 5 | `ContourTool` | `Algorithm/ShapeTools.h/.cpp` |
| 6 | `ShapeTool` | `Algorithm/ShapeTools.h/.cpp` |
| 7 | `LineTool` | `Algorithm/ShapeTools.h/.cpp` |
| 10 | `MultiColorFinder` | `Algorithm/MultiColorFinder.h/.cpp` |

### 工具注册

```cpp
// Algorithm/ITool.cpp — 静态初始化自动注册
ToolRegistry::Register(0, []() -> std::unique_ptr<ITool> { return std::make_unique<EdgeTool>(); });
ToolRegistry::Register(2, []() -> std::unique_ptr<ITool> { return std::make_unique<BlobTool>(); });
ToolRegistry::Register(3, []() -> std::unique_ptr<ITool> { return std::make_unique<ThresholdITool>(); });
ToolRegistry::Register(4, []() -> std::unique_ptr<ITool> { return std::make_unique<YOLOTool>(); });
ToolRegistry::Register(5, []() -> std::unique_ptr<ITool> { return std::make_unique<ContourTool>(); });
ToolRegistry::Register(6, []() -> std::unique_ptr<ITool> { return std::make_unique<ShapeTool>(); });
ToolRegistry::Register(7, []() -> std::unique_ptr<ITool> { return std::make_unique<LineTool>(); });
ToolRegistry::Register(8, []() -> std::unique_ptr<ITool> { return std::make_unique<MorphologyITool>(); });
ToolRegistry::Register(9, []() -> std::unique_ptr<ITool> { return std::make_unique<ColorAnalyzerITool>(); });
ToolRegistry::Register(10, []() -> std::unique_ptr<ITool> { return std::make_unique<MultiColorFinder>(); });

// Core/ToolExecutor.cpp — 运行期补充注册
ToolRegistry::Register(1, []() -> std::unique_ptr<ITool> { return std::make_unique<TemplateMatchITool>(); });
ToolRegistry::Register(11, []() -> std::unique_ptr<ITool> { return std::make_unique<OpenCVYoloITool>(); });
```

---

## 6. 添加新算法的步骤

建议新增工具优先实现 `ITool`，再通过 `ToolExecutor::RunViaITool()` 走统一执行和 `ToolResult` 输出。下面以添加一个新的检测工具为例。

### Step 1: 创建算法文件

```cpp
// Algorithm/MyDetectorTool.h
#pragma once
#include "ITool.h"

class MyDetectorTool : public ITool {
public:
    const char* GetName() const override { return "我的检测"; }
    int GetType() const override { return 13; }
    ToolResult Execute(VisionContext& ctx) override;
    void DrawUI() override {}
    nlohmann::json Save() const override { return {}; }
    void Load(const nlohmann::json&) override {}
};
```

### Step 2: 实现算法

```cpp
// Algorithm/MyDetectorTool.cpp
#include "MyDetectorTool.h"
#include "../Core/VisionContext.h"
#include <opencv2/imgproc.hpp>

ToolResult MyDetectorTool::Execute(VisionContext& ctx) {
    ToolResult result;
    result.toolName = GetName();
    if (ctx.image.empty()) {
        result.success = false;
        result.message = "请先加载图片";
        return result;
    }

    cv::Mat gray, binary;
    if (ctx.image.channels() > 1)
        cv::cvtColor(ctx.image, gray, cv::COLOR_BGR2GRAY);
    else
        gray = ctx.image;

    cv::threshold(gray, binary, 128, 255, cv::THRESH_BINARY);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    for (auto& c : contours) {
        ToolResult::Region region;
        region.contour = c;
        region.bbox = cv::boundingRect(c);
        region.area = (float)cv::contourArea(c);
        result.regions.push_back(std::move(region));
    }

    return result;
}
```

### Step 3: 注册工具和 UI

```cpp
// UI/ToolsWindow.cpp
{13, "我的检测", ToolCategory::Detection, ""},

// Algorithm/ITool.cpp
ToolRegistry::Register(13, []() -> std::unique_ptr<ITool> {
    return std::make_unique<MyDetectorTool>();
});
```

### Step 4: 接入执行和配方

1. 在 `ToolInstance` 添加新工具参数。
2. 在 `ToolExecutor::RunViaITool()` 中把 `ToolInstance` 参数同步到工具对象。
3. 在 `ToolExecutor::Execute()` 中把新 type 分发到 `RunViaITool()`；当前 0-11、13 已走统一 ITool，新增工具建议从 type 14 开始。
4. 在 `RecipeManager.h/.cpp` 增加保存和加载字段。
5. 在 `Windows_imgui.vcxproj` 和 `.vcxproj.filters` 中添加新文件。

### 结果输出模板

```cpp
ToolResult result;
result.regions.push_back(region);     // 轮廓/Blob/形状区域
result.detections.push_back(det);     // YOLO/分类检测框
result.lines.push_back(line);         // 直线结果
result.measurements.push_back(meas);  // 面积/长度/角度等测量值
result.debugImage = debug.clone();    // 可选调试图
return result;
```

---

## 7. 常见崩溃模式与防护

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

> 最后更新: 2026-06-28 | 编译: MSVC + OpenCV 5.0 + ONNX Runtime + NCNN | 13 个 ITool 工具 + type 12 原图特殊工具

---

## 7. 轮廓分析 (ContourDetector)

**文件**: `Algorithm/ContourDetector.h` / `Algorithm/ContourDetector.cpp`

### 功能
对图像进行二值化后查找所有轮廓，支持面积/周长/凸包/圆度/多边形近似等分析，可选 ROI 内的 matchShapes 轮廓比对。

### 核心 API

```cpp
namespace ContourDetector {
    struct ContourResult {
        std::vector<cv::Point> points;  // 轮廓点集
        cv::Rect bbox;                   // 边界框
        double area, perimeter;          // 面积、周长
        int vertices;                     // 多边形近似顶点数
        bool isConvex;                    // 是否凸包
        double circularity;              // 圆度 = 4π·A/P²
        double matchScore;                // matchShapes 得分
        int srcIdx;                       // 来源模板编号
        bool isTemplate;                  // 是否模板轮廓
    };

---

## 7. 多点找色 (MultiColorFinder)

**文件**: `Algorithm/MultiColorFinder.h/cpp` | **类型**: ITool type=10 | **UI**: ToolsWindow.cpp

### 功能

从主图截取参考图（对齐模板匹配的ROI捕获流程），在参考图上点击取色，在大图中搜索所有颜色点同时匹配的位置。支持容差调节、部分匹配回退（绿/红点反馈）。

### 核心结构

```cpp
struct ColorPoint { int x,y; int b,g,r; int tolerance; };
class MultiColorFinder : public ITool {
    cv::Mat refImage; int refAnchorX, refAnchorY;
    std::vector<ColorPoint> points;
    bool useROI; int maxResults; float minDist;
};
```

### 匹配算法

```
逐像素扫描: 快速路径 (遇不匹配立即break)
  ├─ 全部匹配 → 记录位置
  └─ 部分匹配 → 更新最佳(无vec分配)
无完全匹配 → 回退最佳部分匹配 (绿●+红●)
NMS去重 → regions[] = {bbox=参考图ROI, contour=色点坐标}
```

### 绘制反馈

| 场景 | 矩形框 | 圆点 |
|------|:------:|:----:|
| 完全匹配 | 绿色 | 绿● |
| 部分匹配 | 无 | 绿●+红● |

### UI 工作流

```
[添加ROI获取参考图] → 拖拽调整 → [确认捕获]
→ 参考图点击取色 → [统一容差] → [执行多点找色]

    struct Params {
        bool useGray=true; int blurSize=5;
        int threshMode=0;  // 0=OTSU 1=固定 2=自适应
        int threshValue=128, adaptiveBlock=11;
        bool invertBinary=false;
        int retrMode=0;    // RETR_EXTERNAL/LIST/TREE
        int approxMethod=1; // CHAIN_APPROX_NONE/SIMPLE/TC89_*
        double minArea=100, maxArea=1e9;
        bool filterConvex=false;
        float approxEpsilon=0.02f;
        int lineThickness=2, maxContours=500;
        bool showLabels=true, fillContours=false;
        bool matchROI=false;          // 启用ROI轮廓匹配
        double matchThreshold=0.1;     // matchShapes阈值
    };

    std::vector<ContourResult> Detect(const cv::Mat&, const Params&);
    cv::Mat DrawContours(cv::Mat&, const std::vector<ContourResult>&, const Params&);
}
```

### OpenCV 函数

| 步骤 | 函数 | 说明 |
|------|------|------|
| 灰度 | `cv::cvtColor(BGR→GRAY)` | 可选 |
| 模糊 | `cv::GaussianBlur()` | 可选 |
| 二值化 | `cv::threshold()` | OTSU/固定/自适应 |
| 找轮廓 | `cv::findContours()` | RETR + CHAIN_APPROX |
| 凸包 | `cv::convexHull()` | 可选过滤 |
| 多边形近似 | `cv::approxPolyDP()` | 顶点数 |
| 轮廓比对 | `cv::matchShapes()` | CONTOURS_MATCH_I1 |

---

## 8. 形状匹配 (ShapeMatcher)

**文件**: `Algorithm/ShapeMatcher.h` / `Algorithm/ShapeMatcher.cpp`

### 功能
两步匹配：① matchTemplate 灰度相关定位候选区 ② 在候选区内提取轮廓，用轮廓比对算法验证形状。支持模板预处理（灰度/二值化/模糊/反色），三种轮廓比对算法可选。

### 算法流程

```
模板ROI → [预处理] → 提取模板轮廓
目标图 → [matchTemplate] → NMS去重 → 候选区
候选区 → [阈值二值化] → 提取轮廓 → [轮廓比对模板] → 红绿着色
```

### 核心 API

```cpp
struct ShapeMatch {
    cv::Rect bbox;
    double score;       // matchTemplate 相关分 (0~1, 越高越好)
    double shapeScore;  // 轮廓比对分 (越低越像, 0=完美)
    std::vector<cv::Point> points;  // 匹配轮廓
    double area;
    bool isGreen;       // true=通过 false=不通过
};

namespace ShapeMatcher {
    struct Params {
        int blurSize=5, tplRetrMode=0;
        double tplMinArea=30, minScore=0.5, minShapeScore=0.3;
        int lineThickness=2, maxResults=50;
        bool showLabels=true;
        int shapeMethod=0;  // 0=Hu矩 1=ShapeContext 2=Hausdorff
        // 模板预处理
        bool tplGray=false, tplBinary=false;
        int tplBinThresh=128;
        bool tplBlur=false; int tplBlurK=5;
        bool tplInvert=false;
    };
}
```

### 轮廓比对算法

| 算法 | 原理 | 特点 |
|------|------|------|
| **Hu矩** (method=0) | 7个不变矩 | 旋转/缩放/平移不变，快速 |
| **ShapeContext** (method=1) | 极坐标直方图 + Chi-Square | 抗局部变形，对形状敏感 |
| **Hausdorff** (method=2) | 点集最大最小距离 | 严格边缘匹配 |

### OpenCV 函数

| 步骤 | 函数 |
|------|------|
| 模板定位 | `cv::matchTemplate(TM_CCOEFF_NORMED)` |
| 去重 | 自定义 NMS |
| 轮廓提取 | `cv::findContours(RETR_EXTERNAL)` |
| 轮廓比对 | `cv::matchShapes(CONTOURS_MATCH_I2)` |
| 绘图 | `cv::rectangle` + `cv::drawContours` |

---

## 9. 直线检测 (LineDetector)

**文件**: `Algorithm/LineDetector.h` / `Algorithm/LineDetector.cpp`

### 功能
Canny 边缘 + 概率 Hough 直线检测，支持角度/长度过滤和 ROI 限定。

### 核心 API

```cpp
struct LineResult {
    cv::Point2f pt1, pt2;  // 线段两端点
    float length, angle;   // 长度和角度(°)
};

namespace LineDetector {
    struct Params {
        int cannyLow=50, cannyHigh=150;
        float minLineLength=100, maxLineGap=20;
        float minAngle=0, maxAngle=180;
        int lineThickness=2, maxLines=1;
        bool showLabels=true;
        cv::Rect roi;  // 可选ROI
    };
}
```

### OpenCV 函数

| 步骤 | 函数 | 说明 |
|------|------|------|
| Canny | `cv::Canny()` | 边缘提取 |
| Hough | `cv::HoughLinesP()` | 概率Hough |
| 角度过滤 | 自定义 atan2 | 只保留角度范围内的线 |
| 绘图 | `cv::line()` | 彩色线段 |
