# 检测工具链架构与功能说明

> 文档同步日期：2026-08-09。任务级输入、并行执行、PLC 指定任务入口、ToolResult 来源/耗时、18 类工具上下游能力和配方 version 5 已复核。

## 1. 架构边界

当前执行链按以下职责划分：

- `UI`：编辑工具参数、ROI、Fixture 和判定条件，展示执行结果。
- `Core`：维护图像、ROI、工具链、配方、帧导航、结果发布、Fixture 和执行调度。
- `HardwareRuntimeService`：把相机/PLC 事件转换为 Core 执行请求；PLC Trigger 只选择一个任务，不绕过预检、判定或结果聚合。
- `Algorithm`：实现无 UI 依赖的 `ITool`、卡尺、拟合、识别和图像处理算法。
- `VisionContext`：向算法提供本次执行的图像快照、ROI、模板和统一结果上下文。
- `ToolResult`：统一输出状态、区域、检测框、线、文本、测量值和调试图像。

`LegacyAppState.h` 和 `UIStateBridge.h` 已移除。工具链执行不再通过 UI 全局变量传递 ROI、工具实例或图像。

## 2. 图像和文件夹导入

- 文件对话框支持 JPG/JPEG、PNG、BMP、TIF/TIFF 和 WebP。
- 使用 `OFN_NOCHANGEDIR`，选择文件后不会改变进程工作目录。
- 文件夹默认递归扫描子目录，并跳过目录循环。
- 空目录、不支持格式和解码失败通过 Core 错误状态在 UI 中提示。
- `FrameNavigation` 在 Core 中维护图片列表、当前索引和导航请求。

正式任务还可以各自绑定单图或递归图片文件夹。`ToolController` 在每轮开始时为每个任务准备图像，文件夹每轮推进一张；同一任务内工具共用本轮输入。任务选择相机优先时先使用新相机帧，失败再回退任务图片或公共图片。完整规则见 `TASK_GROUPS.md`。

## 3. 工具结果和判定

`ToolResultStatus`：

- `Pass`：算法执行成功且满足启用的判定、公差和质量门限。
- `Fail`：算法执行成功，但结果数量、分数、面积、文本、公差或质量门限不合格。
- `Error`：输入、模型、ROI 或算法执行无效。

所有工具共用以下实例配置：

- 工具标签和结果标签显示开关。
- 固定 ROI、上游第 N 个结果、上游全部结果，或从同一/不同上游各选择一个结果组成双输入 ROI。
- 结果不存在时跳过或判定失败。
- 最少/最多结果数、最低分数、面积范围和文本条件。
- `stopOnFailure`：批量执行遇到 `Fail` 或 `Error` 后停止。
- Fixture 定位坐标系。

日志优先显示 `工具名[标签]`，并记录失败停止的工具序号和原因。

工具卡片统一显示中文状态、运行消息，以及区域、检测框、线段、文本、测量和处理图的数量；展开“输出详情”可查看坐标、分数、文本或测量值。任务结果总览会同时汇总全部非空结果通道，不再只显示第一个命中的结果类型。

结果 ROI 和 Fixture 的来源选择共用静态能力表，只展示能产生区域、检测框、线段或文本框的前置工具；同一规则也用于运行前检查。结果 ROI 未启用时，旧配方中残留的来源索引/ID 不构成依赖。

结果 ROI 默认把检测框、区域、文本框和线段转换为包围矩形。工业测量的“点点距离”是专用适配：检测框、区域和文本框自动转换为中心点，线段保留原始两端点；选择“上游全部结果”时使用当前筛选、排序后的前两个中心点。选择“选择两个结果”时，可为 A、B 分别指定上游工具，再从实时结果下拉框选择具体结果，既支持同一上游的不同结果，也支持两个不同上游；A、B 都转换为中心点后测量。下拉项显示类型、标签/文字、中心坐标或线段端点和分数，并随上游重新执行及筛选/排序自动刷新。若输入不足，界面和工具都会明确说明原因。界面中的“查找 ROI 筛选”只控制检测结果与本工具查找区域的空间关系，不代表下游输出点类型。

工业测量成功后，图像区和任务结果总览的线段标签都优先从统一结果的 `value` 测量项格式化数值与单位，例如“工业测量 270.000 mm”。即使工具提示字符串为空，测量值也不会从叠加标签中消失。

## 4. 二维码和条码

`type=14` 显示为“二维码/条码识别”。ZXing-cpp 后端支持：

- QR Code
- Code128
- EAN-13 / EAN-8
- Data Matrix
- PDF417

`qrFormatMask` 控制码制过滤，`qrFilterDuplicates` 按“码制 + 解码内容”过滤重复结果。OpenCV 后端只处理 QR；自动模式优先使用 ZXing，失败时回退 OpenCV。

## 5. 工业测量工具

`type=15` 已从 ROI 几何近似升级为卡尺和拟合算子编排。

| 模式编号 | 算子 | ROI 输入 | 主要输出 |
|---:|---|---|---|
| 0 | 点点距离 | 两个点、一条线，或至少两个上游区域结果 | 距离；上游区域自动取中心点 |
| 1 | 边缘对/宽度卡尺 | 矩形或四边形 | 多卡尺平均宽度 |
| 2 | 线线角度 | 两条线 | 0 到 90 度夹角 |
| 3 | 圆拟合/直径 | 圆 | 拟合圆心、半径和直径 |
| 4 | 边缘点卡尺 | 矩形或四边形 | 亚像素边缘坐标和强度 |
| 5 | 直线拟合 | 矩形或四边形 | 拟合直线和角度 |
| 6 | 点线距离 | 一个点和一条线 | 点到无限直线距离 |
| 7 | 线线距离 | 两条线 | 平行线距离；非平行无限直线距离为 0 |

旧配方中的模式编号 0 到 3 保持兼容。模式 1 不再返回矩形短边，而是从图像灰度边缘计算真实宽度；模式 3 不再直接返回圆 ROI 直径，而是拟合实际边缘点。

需要两个几何输入的模式 0、2、6、7 会显示“选择两个结果”。点点距离把 A、B 转为中心点；线线角度和线线距离要求 A、B 上游都能输出线段；点线距离把 A 转为中心点并要求 B 输出线段。来源下拉和运行前校验使用相同类型规则。

### 5.1 卡尺边缘提取

`Algorithm/CaliperOperators` 的处理流程：

1. 沿 ROI 法线按亚像素位置进行双线性灰度采样。
2. 沿卡尺切向做投影平均，降低纹理和噪声影响。
3. 对一维灰度曲线进行 Gaussian 平滑。
4. 计算中心差分梯度并按“任意、暗到明、明到暗”过滤极性。
5. 对梯度峰值做二次曲线插值，得到亚像素边缘位置。
6. 边缘对模式寻找方向相反且间距合格的两个峰值。

有向卡尺区域使用中心、切向、法线、跨度和搜索长度表达。Fixture 旋转后的矩形会转换为四边形，卡尺仍沿旋转后的真实法线采样，不会退化为轴对齐包围盒。

### 5.2 多卡尺拟合

- 直线：多个卡尺提取边缘点，支持最小二乘和 RANSAC，再用 `fitLine` 精拟合内点。
- 圆：沿圆周布置径向卡尺，支持最小二乘和 RANSAC，再用代数最小二乘精拟合内点。
- 圆 RANSAC 在小样本下枚举组合；大样本限制为确定性的 2048 个假设，避免卡尺数量增加后出现组合爆炸。
- `fitInlierThreshold` 使用像素单位，决定 RANSAC 内点范围。

### 5.3 质量指标

卡尺和拟合结果统一写入 `ToolResult.measurements`：

| 字段 | 含义 | 单位 |
|---|---|---|
| `totalCalipers` | 配置/实际布置的卡尺数 | count |
| `validCalipers` | 找到有效边缘或进入最终拟合的卡尺数 | count |
| `edgeStrength` | 有效边缘平均梯度强度 | gray/px |
| `fitResidual` | 拟合残差 RMS | px |
| `standardDeviation` | 残差或宽度标准差 | px |
| `maxError` | 最大绝对误差 | px |
| `confidence` | 有效率、边缘强度和稳定性组合可信度 | 0 到 1 |

`minimumValidCalipers` 和 `minimumConfidence` 可直接把测量结果判定为 `Fail`。边缘点模式是单卡尺算子，因此有效卡尺门限固定按 1 判断。

### 5.4 公差

公差范围：

```text
[nominal - toleranceMinus, nominal + tolerancePlus]
```

超出范围时返回 `Fail`，并在 `statusReason` 中写入“测量值超出公差范围”。标称值、上下限和实际值会进入 JSON 与 Markdown 报告。

## 6. 完整标定

`Core/CalibrationModel` 的坐标转换顺序：

1. 可选镜头畸变校正。
2. 可选 3x3 像素到世界单应矩阵。
3. 未使用单应矩阵时，应用独立的 `scaleX`、`scaleY`。
4. 比例路径应用像素原点和世界原点偏移；单应矩阵路径的世界原点已包含在矩阵平移项中。

支持参数：

- X/Y 独立毫米比例。
- 像素原点和世界原点。
- 3x3 透视/单应矩阵。
- 相机内参 `fx/fy/cx/cy`。
- 畸变参数 `k1/k2/p1/p2/k3`。

OpenCV 5 的点去畸变 API 来自 `opencv2/geometry/3d.hpp`。所有距离和角度先把构成几何量的点转换到世界坐标后再计算，避免非等比例或透视标定下直接乘单一比例产生误差。

`measureMmPerPixel` 仅用于读取旧配方；新配置使用 `measurement.calibration*` 和 `CalibrationModel` 字段。

## 7. Fixture 定位坐标系

每个 `ToolInstance` 可选择能输出空间结果的前置工具作为定位源，包括区域、检测框、线段和文本框：

1. 先执行定位工具并得到区域中心和角度。
2. 在标准样本上点击“从当前定位结果记录参考位姿”。
3. 后续工具执行前，Core 计算当前位姿相对参考位姿的平移和旋转。
4. Core 统一变换该工具的点、线、圆、矩形和多边形 ROI。
5. UI 仍只保存和编辑参考 ROI，不参与运行时坐标变换。

矩形发生旋转后转换成四边形 ROI；圆在刚性 Fixture 下保持半径不变。Fixture 上游无结果时可配置跳过或 `Fail`。工具移动、删除和“原图置顶”时，Core/UI 会同步重映射 Fixture 和结果 ROI 的上游索引。

`ToolResult::Region.angle` 和 `ToolResult::Line.angle` 用于发布定位角度，结果 JSON 同时导出相关字段；检测框和文本框没有角度时按 0° 建立位姿。

## 8. 配方格式

当前保存器写出配方 version 5。加载器继续读取旧 version 1/2/3/4 字段；仓库保留 version 2 案例用于兼容性回归，并新增 version 5 的 2、4、6、8、10、12、16 任务完整案例。主要结构包括任务输入、稳定工具 ID、Fixture 和测量参数：

```json
{
  "version": 5,
  "taskGroups": [
    {
      "name": "任务01",
      "enabled": true,
      "imagePath": "images/sample.png",
      "imageFolderPath": "",
      "imageFolderIndex": -1,
      "imageFolderCount": 0,
      "cameraPreferred": false
    }
  ],
  "tools": [
    {
      "fixture": {
        "enabled": true,
        "sourceToolIndex": 1,
        "resultIndex": 0,
        "referenceX": 320.5,
        "referenceY": 240.0,
        "referenceAngle": 2.4,
        "failOnMissing": true
      },
      "measurement": {
        "mode": 5,
        "caliperCount": 24,
        "edgePolarity": 1,
        "subpixel": true,
        "fitMethod": 1,
        "fitInlierThreshold": 1.0,
        "minimumValidCalipers": 18,
        "minimumConfidence": 0.75,
        "calibrationEnabled": true,
        "scaleX": 0.0125,
        "scaleY": 0.0127,
        "homographyEnabled": false,
        "distortionEnabled": false
      }
    }
  ]
}
```

加载器仍读取旧版本顶层测量字段；缺少任务、稳定 ID、输入或测量字段时使用默认值。

## 9. 结果导出

JSON 结果包含：

- `sourceToolIndex`、`sourceToolId`、工具名、标签、状态和原因。
- prepare/execute/publish/wall 以及后端预处理/推理/后处理耗时。
- 区域的 bbox、面积、分数、角度、标签和轮廓。
- 检测框、线、文本和全部测量/质量字段。

Markdown 报告包含每个工具的耗时、状态、区域、线、文本和测量值。

## 10. 验证命令

```powershell
& 'D:\VisualStudio\Community\MSBuild\Current\Bin\MSBuild.exe' Windows_imgui.vcxproj /p:Configuration=Debug /p:Platform=x64 /m:1
& 'D:\VisualStudio\Community\MSBuild\Current\Bin\MSBuild.exe' Test\RegressionTests.vcxproj /p:Configuration=Debug /p:Platform=x64 /m:1
Test\x64\Debug\RegressionTests.exe --caliper-only
Test\x64\Debug\RegressionTests.exe --policy-only
Test\x64\Debug\RegressionTests.exe --qr-only
Test\x64\Debug\RegressionTests.exe --task-images-only
Test\x64\Debug\RegressionTests.exe --hardware-camera-only
Test\x64\Debug\RegressionTests.exe --plc-handshake-only
```

`--caliper-only` 覆盖边缘极性、亚像素位置、边缘对、RANSAC 直线/圆、XY 标定、单应矩阵、零畸变和 Fixture 刚性变换。

`--policy-only` 覆盖导入、统一判定、工业测量编排、结果 ROI、实例化模板匹配、工具重排和配方 v2 往返。

完整回归还会运行 OCR、YOLO、Blob、颜色、阈值、形态学、边缘、图像状态、任务输入和硬件回退测试。缺少可选模型时应验证明确的跳过/错误路径；发布验收使用完整依赖运行一次全套回归。
