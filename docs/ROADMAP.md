# 开发路线

> 当前版本: 2026-07-19 | 已覆盖 type 0-16（type 12 为原图特殊工具）| 后续按“稳定性 -> 架构收尾 -> 工业能力 -> 工具链体验 -> 发布”推进

---

## 第一阶段 ✅ 已完成

| # | 功能 | 实现 |
|---|------|------|
| 1 | 图片浏览 + 缩放平移 | ImageViewer |
| 2 | ROI 管理 | 交互式创建/拖拽/删除 |
| 3 | 图像处理管线 | 灰度/模糊/二值化/Canny |
| 4 | 模板匹配 | 多实例 + 旋转 + NMS |
| 5 | YOLO 检测 | ONNX Runtime 推理 + 实时 |
| 6 | 视频播放 | 文件 + 摄像头 + 音频同步 |
| 7 | 配方系统 | JSON 保存/加载工具实例 |
| 8 | 轮廓分析 | 凸包/圆度/多边形近似 |
| 9 | 形状匹配 | Hu矩/ShapeContext/Hausdorff |
| 10 | 形态学工具 | 7 种运算 |
| 11 | 颜色分析 | 多色域 + 直方图 |
| 12 | 直线检测 | HoughLinesP + 角度过滤 |
| 13 | 多点找色 | 参考图点击取色 + 容差匹配 + 绿/红点反馈 |
| 14 | 日志系统 | 线程安全 + 颜色分级 |
| 15 | 主题切换 | 夜间/白天 + 持久化 |
| 16 | 原图工具 | type=12，批量/单步链路中恢复本轮原图 |
| 17 | 工具链输入 | 每实例可选上一步原图、上一步处理图、原图工具输出 |
| 18 | 添加工具图标 | 添加工具弹窗支持 PNG 图标，失败时回退内置图标 |
| 19 | OpenCV 5.0 YOLO 实验 | type=11，用于和 ONNX Runtime YOLO 对比测试 |
| 20 | 结果导出 | JSON 结果、PNG 结果截图、Markdown 运行报告 |
| 21 | OCR 文字识别 | type=13，PP-OCRv6 tiny + NCNN，支持 ROI、置信度、文本框叠加 |

## 第二阶段 ✅ 架构升级（优先级排序）

| # | 任务 | 说明 |
|---|------|------|
| ① | **ITool 接口** | ✅ 已完成 — type 0-11、13 已接入 ITool；type 12 原图为工具链重置特殊工具 |
| ② | **ToolResult 统一** | ✅ 已完成 — `ToolResult { measurements, regions, detections, lines, texts, debugImage }` 替代各工具独立结构 |
| ③ | **ROI 升级** | ✅ 已完成 — `ROI_TYPE_RECT/POINT/LINE/CIRCLE/POLYGON` 5 种几何类型 + 按类型可视化 |
| ④ | **Recipe 版本化** | ✅ 已完成 — `"version": 1` |
| ⑤ | **VisionContext** | ✅ 已完成 — `struct VisionContext { image, rois, frozenTemplate, unifiedResults }` 统一算法上下文，视图变换由 `ImageViewState` 管理 |
| ⑥ | **ToolExecutor/ToolController 拆分** | ✅ 已完成 — 执行/调度逻辑已从 ToolsWindow 拆到 Core，UI 文件仍保留参数面板代码 |

## 第三阶段 ✅ 稳定平台基线

| # | 功能 | 状态 |
|---|------|------|
| 1 | 结果导出 | ✅ 已完成 — 导出 `ToolResult` 的 detections、regions、lines、texts、measurements 到 JSON |
| 2 | 配方加载后自动执行 | ✅ 案例配方可解析相对资源路径，并由回归测试加载图片后执行工具链 |
| 3 | 运行报告 | ✅ 已完成 — 全部执行后可生成 Markdown 报告，包含工具名、耗时、结果数量、OK/FAIL |

## 第四阶段 进行中 — 工业常用工具

| # | 工具 | 说明 |
|---|------|------|
| 1 | 尺寸测量 | ✅ 已有点到点距离、线段长度、圆直径、角度、卡尺、拟合和标定；继续完善现场向导 |
| 2 | Blob 分析增强 | ✅ 已增加圆度、长宽比、方向、轮廓、质心和筛选条件；继续补完整公差判定 |
| 3 | 二维码/条码识别 | ✅ 已接入 ZXing-cpp 码制过滤和重复过滤；继续补稳定性和性能测试 |
| 4 | 图像差分 | ✅ type 16，参考图、`absdiff + threshold + morphology`、差异区域和差异高亮 |
| 5 | OCR 识别 | ✅ 已接入 — PP-OCRv6 tiny + NCNN；后续可继续优化模型、字典和性能 |

## 第五阶段 进行中 — 执行链路升级

| # | 任务 | 说明 |
|---|------|------|
| 1 | OK/NG 状态 | ✅ `ToolResultStatus { Pass, Fail, Error }` 和判定原因已统一 |
| 2 | 失败停止 | ✅ 支持配方中配置失败停止，并记录停止工具 |
| 3 | 工具启用/禁用 | ✅ 工具实例支持跳过执行但保留参数，结果标记为 skipped Pass |
| 4 | 复制工具实例 | ✅ Core 复制 API 生成新 toolId，复制参数并清空运行时状态；公共卡片提供入口 |
| 5 | 工具分组/折叠 | 大量工具时便于管理流程 |
| 6 | 运行前检查 | ✅ 统一检查图片、绑定 ROI、模板、参考图、YOLO/OCR 模型，以及上游/循环依赖 |

## 第六阶段 🔲 工程发布

| # | 任务 | 说明 |
|---|------|------|
| 1 | 制作 `runtime.zip` | 包含 OpenCV、ONNX Runtime、DirectML、NCNN 等运行时 DLL/lib |
| 2 | GitHub Release | 仓库保留源码，Release 附件放运行时包 |
| 3 | 构建文档更新 | `docs/BUILD.md` 增加“下载 runtime.zip 解压到 redist/” |

## 第七阶段 🔲 平台化

| # | 任务 | 说明 |
|---|------|------|
| 1 | 节点式流程编辑器 | 等工具链稳定后再做可视化流程 |
| 2 | 插件系统 | 可选脚本/插件扩展，当前主线不依赖 Python |
| 3 | 工业相机 SDK | 接入工业相机采集 |
| 4 | 工业通讯 | Modbus TCP、PLC、OPC UA、MQTT |

---

## 后续开发执行顺序

| 优先级 | 阶段 | 先做任务 |
|---|------|------|
| P0 | 稳定性 | 原图删除、输入源一致性、完整案例回归；✅ 已完成基线 |
| P1 | 架构收尾 | ✅ 稳定 toolId、ImageViewer/Core 边界、ROIEditorState、TemplateMatch 去全局和 RecipeManager 组合 DTO 已完成；继续收窄 ToolsWindow 可写状态访问 |
| P2 | 工业能力 | Blob 增强 -> 图像差分 -> 标定向导 -> Fixture 可视化 -> SPC/批次统计 |
| P3 | 工具链体验 | ✅ 启用/禁用、复制、分组筛选、依赖可视化/循环校验和批量公共参数已完成 |
| P4 | 工程发布与设备 | CI/runtime.zip -> GitHub Release -> 相机 -> PLC/Modbus/OPC UA |

---

## 任务执行与更新规范

- 执行具体开发任务时，使用 `docs/TASK_EXECUTION.md`。
- 后续迭代项目、同步工程文件和更新文档时，使用 `docs/PROJECT_UPDATE_GUIDE.md`。
