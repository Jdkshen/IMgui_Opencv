# 视觉算法与工具说明

> 文档同步日期：2026-08-09。本文以当前 type 0-17、`VisionContext`、`ITool`、`ToolResult` 和任务执行链为准；18 类工具的结果能力、结果 ROI、Fixture 和统一显示已按当前代码复核。

## 1. 统一执行契约

除 type 12“原图”外，当前 17 个视觉工具都实现 `ITool`：

```cpp
class ITool {
public:
    virtual ~ITool() = default;
    virtual const char* GetName() const = 0;
    virtual int GetType() const = 0;
    virtual ToolResult Execute(VisionContext& ctx) = 0;
    virtual void DrawUI() = 0;
    virtual nlohmann::json Save() const = 0;
    virtual void Load(const nlohmann::json& json) = 0;
};
```

实际调用链：

```text
任务输入 / 当前公共图片
    → ToolController 选择执行范围与顺序
    → ToolExecutor 准备图像快照、ROI、Fixture 和工具参数
    → ITool::Execute(VisionContext&)
    → ToolJudgement 计算 Pass / Fail / Error
    → ResultPublisher 或 ToolExecutor::PublishDetached
    → gContext.unifiedResults
    → ResultOverlayState
    → ImageViewer::DrawUnifiedResults
```

任务并行时，每个任务使用独立的 `ToolInstance` 和 `VisionContext` 快照；任务线程不直接修改 ImGui、公共图像或公共结果容器。

## 2. VisionContext

`Core/VisionContext.h` 是算法执行的输入边界：

| 字段 | 用途 |
| --- | --- |
| `image` / `originalImage` | 当前工具输入与本轮原图快照 |
| `immutableImageOwner` / `immutableOriginalOwner` | 并行任务共享的只读图像所有权 |
| `frame` | 单图、序列、视频或相机的统一帧包 |
| `imageVersion` / `width` / `height` | 图像版本和尺寸 |
| `stopToken` | 后台执行取消 |
| `rois` / `selectedROI` | 本次工具执行的 ROI 快照 |
| `frozenTemplate` | 兼容模板输入 |
| `unifiedResults` | 当前统一叠加结果 |

算法只读取 `ctx.image` 和 `ctx.rois`，不读取 UI 状态。视图缩放和平移由 `ImageViewState` 管理，不属于算法上下文。

## 3. ToolResult

`Algorithm/ToolResult.h` 同时承载执行状态、性能数据和可视化结果：

| 分组 | 字段 |
| --- | --- |
| 来源 | `toolName`、`sourceToolIndex`、`sourceToolId` |
| 状态 | `success`、`skipped`、`message`、`status`、`statusReason` |
| 耗时 | `prepareMs`、`executeMs`、`publishMs`、`wallMs`、后端预处理/推理/后处理耗时 |
| 测量 | `measurements` |
| 区域 | `regions`：轮廓、bbox、中心、面积、分数、角度、宽高、圆度和长宽比 |
| 检测 | `detections`：框、类别、分数和标签 |
| 线段 | `lines`：端点、长度和角度 |
| 文本 | `texts`：文字、矩形框和置信度 |
| 图像 | `debugImage`：可选处理结果图 |

`ToolResultStatus` 的含义：

- `Pass`：执行成功且满足判定条件。
- `Fail`：算法执行完成，但数量、分数、面积、文字、公差或质量门限不合格。
- `Error`：输入、模型、ROI、依赖或算法执行无效。

## 4. 工具 type 分配

| type | 工具 | 实现 | 主要输出 |
| ---: | --- | --- | --- |
| 0 | 边缘检测 | `EdgeTool` | `debugImage` |
| 1 | 模板匹配 | `TemplateMatchingTool` | `regions`、`measurements` |
| 2 | Blob 分析 | `BlobTool` | `regions`、`measurements` |
| 3 | 阈值调试 | `ThresholdITool` | `debugImage` |
| 4 | YOLO 检测 | `YOLOTool` / ONNX Runtime | `detections` |
| 5 | 轮廓分析 | `ContourTool` | `regions`、`measurements` |
| 6 | 形状匹配 | `ShapeTool` | `regions` |
| 7 | 直线检测 | `LineTool` | `lines` |
| 8 | 形态学 | `MorphologyITool` | `debugImage` |
| 9 | 颜色分析 | `ColorAnalyzerITool` | `measurements`、`debugImage` |
| 10 | 多点找色 | `MultiColorFinder` | `regions` |
| 11 | YOLO OpenCV 5.0 | `OpenCVYoloITool` | `detections`、后端耗时 |
| 12 | 原图 | `ToolController` 特殊节点 | 恢复本轮原图 |
| 13 | 文字识别 | `OCRTool` / PP-OCRv6 tiny + NCNN | `texts`、`measurements` |
| 14 | 二维码/条码识别 | `QRCodeTool` | `texts`、`regions`、`measurements` |
| 15 | 工业测量 | `MeasurementTool` / `CaliperOperators` | `measurements`、`lines`、`regions` |
| 16 | 图像差分 | `DifferenceTool` | `regions`、`measurements`、`debugImage` |
| 17 | 几何绘制 | `GeometryDrawTool` | `regions`、`lines`、`texts`、`measurements`、`debugImage` |

新增工具从 type 18 开始。

“添加工具”目录按使用目的归类，type 编号和配方兼容性不受分类调整影响：

| 界面分类 | 工具 |
| --- | --- |
| 输入与预处理 | 原图、阈值调试、形态学、边缘检测 |
| 定位与识别 | 模板匹配、YOLO 检测、形状匹配、文字识别、二维码/条码识别 |
| 区域与几何 | Blob 分析、轮廓分析、直线检测、几何绘制 |
| 分析与测量 | 颜色分析、多点找色、图像差分、工业测量 |
| 实验工具 | YOLO OpenCV 5.0 |

目录支持按工具名称或用途说明搜索。常用的前两类默认展开，其他分类保持可见并可按需展开，避免 18 个工具同时铺开造成长列表难以浏览。

## 5. 基础处理工具

### 5.1 边缘检测（type 0）

处理流程：按需转灰度，然后使用 `cv::Canny`。主要参数为低阈值、高阈值和灰度开关。输出图可作为后续工具的“上一步处理图”。

### 5.2 阈值调试（type 3）

```text
ctx.image
    → 可选灰度
    → 可选 GaussianBlur
    → 可选 threshold
    → 可选 Canny
    → ToolResult.debugImage
```

参数保存在 `ToolInstance::threshold`，不再由散落的全局参数控制。

### 5.3 形态学（type 8）

支持腐蚀、膨胀、开、闭、梯度、顶帽和黑帽；可配置核大小、核形状、迭代次数和灰度处理。核心 API 是 `cv::getStructuringElement` 与 `cv::morphologyEx`。

### 5.4 原图（type 12）

原图不是 `ITool`。`ToolController` 在执行到该节点时恢复本轮任务输入，用于阻断前面处理图继续向后累积。

## 6. 检测与识别工具

### 6.1 模板匹配（type 1）

`TemplateMatchingTool` 保存每实例模板、搜索 ROI、源图/模板预处理、旋转范围、阈值、最大结果数和 NMS 参数。

```text
模板与源图预处理
    → 按角度旋转模板
    → cv::matchTemplate
    → 候选分数过滤
    → NMS 去重
    → ToolResult.regions（含中心、分数和角度）
```

`TemplateMatchingTool` 是工具链唯一的模板匹配实现，模板资产由 `ToolAssetService` 管理。

### 6.2 YOLO（type 4 / type 11）

- type 4 使用 ONNX Runtime，支持类别文件、置信度、NMS、ROI 和 DirectML。
- type 11 使用 OpenCV 5 DNN，作为实验对比后端，并记录后端预处理、推理和后处理耗时。
- 两者都将最终框转换为 `ToolResult::Detection`，显示层不直接依赖后端结构。

### 6.3 Blob 与轮廓（type 2 / type 5）

- Blob：二值化、连通域/轮廓提取、面积、中心、圆度、长宽比和方向筛选。
- 轮廓：灰度、模糊、固定/OTSU/自适应阈值、轮廓层级、凸性和多边形近似。
- 两者都使用 `regions` 输出几何区域，并可通过统一判定规则约束数量、面积和分数。

### 6.4 形状匹配（type 6）

先用模板相关性定位候选，再提取轮廓进行 Hu 矩、ShapeContext 或 Hausdorff 比对。定位角度和区域中心可作为后续 Fixture 来源。

### 6.5 多点找色（type 10）

参考图保存锚点和多个颜色点。执行时逐候选位置检查颜色容差，完整匹配优先，找不到完整匹配时可保留最佳部分匹配反馈；结果经过距离去重后写入 `regions`。

### 6.6 OCR（type 13）

PP-OCRv6 tiny NCNN 流程包括文本检测、候选框过滤、文本识别和结果缓存。支持 ROI 扩边、最小置信度、候选上限、仅检测和快速模式。模型或字典缺失时按工具配置阻止执行或跳过。

### 6.7 二维码/条码（type 14）

支持 QR、Code128、EAN-13/EAN-8、Data Matrix 和 PDF417。自动模式优先使用 ZXing-cpp，必要时回退 OpenCV QR；支持多码、图像增强、码制过滤和按“码制 + 内容”去重。

### 6.8 图像差分（type 16）

对当前图与实例参考图执行模糊、`absdiff`、阈值和形态学过滤，按最小面积筛选差异区域。参考图属于工具实例资产，随配方旁路文件保存。

## 7. 几何与分析工具

### 7.1 直线检测（type 7）

`Canny → HoughLinesP → 长度/角度过滤`，支持 ROI 和最大结果数。输出端点、长度与角度。

### 7.2 工业测量（type 15）

支持点点距离、宽度卡尺、线线角度、圆拟合、边缘点、直线拟合、点线距离和线线距离。卡尺流程包含双线性采样、投影平均、Gaussian 平滑、梯度峰值、亚像素插值和 RANSAC/最小二乘拟合。

标定支持 X/Y 比例、单应矩阵、相机内参与畸变参数；公差、有效卡尺数和可信度可直接影响 Pass/Fail。详见 `INSPECTION_PIPELINE_2026.md`。

点点距离从结果 ROI 获取输入时执行几何适配：区域、检测框和文本框取中心点，线段保留端点；“上游全部结果”按筛选和排序后的前两个点测量。“选择两个结果”允许 A、B 使用同一上游的不同结果，或分别使用两个上游；结果通过描述性下拉框选择，不直接编辑数字序号。下拉内容与解析器使用同一套过滤和排序，并显示类型、标签/文字、中心坐标或端点及分数。默认的其他下游工具仍接收空间结果的包围矩形。工具卡片中的“查找 ROI 筛选”描述的是检测结果是否落在搜索 ROI 内，不是结果输出形状。

双结果模式同时用于线线角度、点线距离和线线距离。线输入槽只列出能力表中声明 `lines` 的上游；点线距离的 A 为结果中心点、B 为原线段。UI 过滤与 `ToolChainValidator` 的拒绝规则一致。

### 7.3 几何绘制（type 17）

把线、矩形、圆等几何图元作为工具实例数据保存，用于结果标注和配方复现，不替代检测算法。

### 7.4 颜色分析（type 9）

支持 BGR、HSV、Lab 和 YCbCr 色域、直方图分箱、ROI 统计和可选直方图预览；统计值写入 `measurements`。

## 8. ROI、上游结果与 Fixture

每个工具可以使用：

- 自身保存的搜索 ROI；
- 上游工具的第 N 个空间结果或全部空间结果转成运行 ROI；
- 上游空间结果建立 Fixture 坐标系；
- 上游结果缺失时跳过或判定失败。

ROI 输入遵循显式绑定规则：`ToolInstance.searchROIs` 为空时，普通执行、任务并行快照和实时 YOLO 都按整图运行，不读取 `ROIState` 中仅用于画布显示/编辑的 ROI。只有在工具卡片点击“添加 ROI”并确认绑定后，搜索 ROI 才进入 `VisionContext.rois`。结果 ROI 与 Fixture 生成的运行 ROI 不受此规则影响。工业测量需要明确的测量 ROI；未绑定时应预检失败或返回缺少 ROI，而不是借用画布 ROI。

工具依赖优先使用稳定的 `toolId/sourceToolId`，旧配方中的索引仍可兼容解析。检测到跨任务结果 ROI 或 Fixture 依赖时，“执行全部”的任务并行会自动回退顺序执行。

结果 ROI 与 Fixture 共用 `Core/ToolResultCapabilities.h` 的能力表。只有能输出 `detections`、`regions`、`texts` 或 `lines` 的前置工具会出现在来源下拉框中；边缘、阈值、形态学、颜色分析和原图等仅输出图像/测量值的工具不能作为空间来源。运行前校验使用同一能力表，旧配方若保存了不兼容来源会明确报错；结果 ROI 未启用时会忽略遗留的来源字段。

`Core/ResultROIResolver` 的默认输出几何是包围矩形；消费工具可以声明专用几何。当前工业测量模式 0 声明“区域取中心点、线段保留端点”，双结果模式则把 A、B 分别解析为一个中心点。普通执行、分离执行和任务并行快照共用同一规则；执行图和运行前校验同时登记 A、B 两条依赖。

工业测量的主测量值统一写入 `measurements[name="value"]`。叠加层从该结构化字段生成“工具名 + 三位小数 + 单位”的线段标签，避免只显示工具名或依赖自由文本 `message`。

空间结果解析顺序为检测框 → 区域 → 文本框 → 线段。文本使用文字框，线段使用两个端点的最小包围矩形；Fixture 的位姿解析覆盖区域、检测框、线段和文本框，其中无角度字段的检测框/文本框按 0° 处理。

## 9. 工具链图像传递

每个实例的 `inputSourceMode` 决定输入：

| 值 | 界面含义 | 行为 |
| ---: | --- | --- |
| 0 | 上一步原图 | 使用上一个工具执行前的图 |
| 1 | 上一步处理图 | 使用上一个工具产生的处理图 |
| 2 | 原图工具输出 | 使用最近一次原图节点恢复的图；没有节点时回退本轮输入 |

处理类工具的 `debugImage` 可成为后续链路图像。主线程显示图由 `ImageState` 管理，并由 `GraphicsBackend` 上传到当前 DX12/DX11 后端；算法不允许直接写 GPU 资源或图形后端状态。

## 10. 添加新工具

1. 从 type 18 开始分配唯一编号。
2. 在 `Algorithm/` 新增实现并注册到 `ToolRegistry`。
3. 在 `ToolInstance` 或拆分的 `ToolSettings` 中增加实例参数。
4. 在 `UI/ToolsWindow.cpp` 的 `g_ToolRegistry` 添加名称、分类和参数面板。
5. 在 `ToolExecutor::RunViaITool()` 同步实例参数。
6. 在 `ToolInstance::ToRecipeJson()` / `LoadRecipeJson()` 保存和加载参数；资源资产再补 `RecipeToolInstance`。
7. 更新 `.vcxproj` 与 `.vcxproj.filters`。
8. 添加空图、ROI、配方往返、判定和执行结果回归。
9. 更新 `README.md`、本文、`CODE_STRUCTURE.md`、`ROADMAP.md` 和 type 表。

最小实现：

```cpp
ToolResult MyTool::Execute(VisionContext& ctx) {
    ToolResult result;
    result.toolName = GetName();
    if (ctx.image.empty()) {
        result.success = false;
        result.status = ToolResultStatus::Error;
        result.message = "请先加载图片";
        return result;
    }

    // OpenCV 算法；周期性检查 ctx.IsCancellationRequested()
    return result;
}
```

## 11. 稳定性规则

- 调用 `cvtColor`、`resize`、`threshold`、模型推理前检查 `cv::Mat::empty()`。
- ROI 必须与图像边界求交，并确认宽高大于 0。
- 后台循环和耗时推理应检查 `ctx.IsCancellationRequested()`。
- ImGui 的 `Begin/End`、`BeginChild/EndChild` 必须成对，OpenCV 异常不能跳过结束调用。
- 算法返回 `ToolResult`，不要新增独立的全局结果容器。
- 模型、模板或参考图缺失时返回明确状态，不允许静默使用无效内存。
- 并行任务只使用快照，不从工作线程写 `ImageState`、ImGui 或 DX11/DX12 资源。

## 12. 验证

```powershell
MSBuild Windows_imgui.slnx /m /p:Configuration=Release /p:Platform=x64
Test\x64\Debug\RegressionTests.exe
Test\x64\Debug\RegressionTests.exe --policy-only
Test\x64\Debug\RegressionTests.exe --caliper-only
Test\x64\Debug\RegressionTests.exe --qr-only
```

完整回归还覆盖任务独立图片、文件夹轮换、相机回退、任务并行、配方兼容和结果判定。
