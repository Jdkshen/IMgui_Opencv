# 几何绘制性能与结果 ROI 序号选择设计

> 历史设计：记录 2026-07-12 几何绘制、性能与结果 ROI 选择方案；当前实现边界见 `../../INSPECTION_PIPELINE_2026.md`。


## 目标

1. 几何绘制继续把像素写入处理图，供后续工具使用，同时减少整图复制。
2. 所有能输出多个 `regions`、`detections` 或 `texts` 的工具，统一支持选择第 N 个结果或全部结果作为后续工具 ROI。
3. 保持旧配方兼容，不改变已有“结果1个/结果多个”的含义。

## 非目标

- 不改成仅 ImGui 叠加，因为后续算法需要读取绘制后的像素。
- 不引入 DX12 GPU 合成或 GPU 回读。
- 不重写各算法对多个 ROI 的内部处理方式；“全部”继续表示把全部 ROI 注入 `VisionContext::rois`。

## 几何绘制性能

当前几何工具执行一次大约 30ms，其中绘制约 3-4ms，主要时间来自准备和发布阶段的多次整图复制。

### 输入阶段

将工具类型 16 加入 `ToolCanUseSharedInput()` 的只读工具列表。`GeometryDrawTool::Execute()` 不修改 `ctx.image`，而是在自己的输出图上绘制，因此可以安全共享输入矩阵头。

### 输出阶段

为 `ImageState` 增加接管处理图所有权的接口：

```cpp
void AdoptProcessedImage(cv::Mat&& image, bool prepareDisplayUpload);
```

`ToolExecutor` 在 `result.updatesImage == true` 时把 `result.debugImage` 移交给 `ImageState`，避免先复制到 `s_current`、再复制到 `gContext.image`。`gContext.image` 使用 `s_current` 的共享只读视图；需要可写输入的旧工具仍在执行前自行克隆。

显示所需的 BGR/BGRA 到 RGBA 转换继续保留，因为 DX12 纹理上传仍需要该数据。运行模式不显示预览时继续跳过 RGBA 准备。

## 结果 ROI 序号

### 数据结构

在 `ToolInstance` 和 `RecipeToolInstance` 增加：

```cpp
int roiSourceResultIndex = 0;
```

该值使用零基序号，仅在 `roiSourceMode == 1` 时生效。

- `roiSourceMode == 0`：不使用前置结果 ROI。
- `roiSourceMode == 1`：使用 `roiSourceResultIndex` 指定的单个结果。
- `roiSourceMode == 2`：使用全部结果。

结果的统一编号顺序保持现有转换顺序：先 `regions`，再 `detections`，最后 `texts`。几何工具只产生 `regions`，顺序与几何图形列表中有效区域的顺序一致。直线、箭头、十字和文字不产生区域编号。

### UI

当前“结果 ROI”的三项组合框改为“不使用/使用”。启用后依次显示来源工具和“结果序号”组合框：

```text
第1个
第2个
第3个
...
全部
```

可选数量优先取来源工具上一次执行结果的可转换 ROI 数量。来源尚未执行时，至少显示当前已保存序号和“全部”，使配方加载后不会把已保存序号重置为第1个。

选择“第 N 个”时写入 `roiSourceMode = 1` 和 `roiSourceResultIndex = N - 1`；选择“全部”时写入 `roiSourceMode = 2`。

### 无效序号

如果指定的第 N 个结果不存在，后续工具不回退到第1个，也不执行全图；它应跳过并记录清晰日志：

```text
前置结果第 N 个 ROI 不存在，已跳过
```

这样可以避免检测结果数量变化后工具悄悄跑错区域。

### 配方兼容

- 新配方保存 `roiSourceResultIndex`。
- 旧配方没有该字段时默认值为 0。
- 旧 `roiSourceMode == 1` 继续表示第1个结果。
- 旧 `roiSourceMode == 2` 继续表示全部结果。

复制、粘贴、上移和下移工具时，该字段随 `ToolInstance` 一起复制，并继续使用现有的来源工具索引重映射逻辑。

## 测试

1. `ImageState` 接管右值图像后保持数据指针，不做整图复制。
2. 几何工具仍能把绘制像素传给后续工具，普通模式和运行模式行为不变。
3. 多区域结果可准确选择第1、第2和第3个 ROI。
4. “全部”模式保留全部 ROI。
5. 越界序号会跳过，不回退到其他 ROI 或全图。
6. `roiSourceResultIndex` 配方保存/加载往返一致。
7. 旧配方缺少新字段时仍默认选择第1个结果。

## 验证

- `RegressionTests` Release x64 全部通过。
- `Windows_imgui` Debug x64 和 Release x64 均为 0 错误。
- 对同一张大图比较几何工具日志，准备和发布耗时应明显下降；实际数值依赖图片尺寸和纹理上传速度，不用固定毫秒阈值做自动测试。
