# 案例配方说明

> 文档同步日期：2026-08-09。当前保存器写出 recipe version 5；加载器继续兼容旧版配方。所有案例均使用相对路径，发布包会携带配方所需的图片和模型。

## 多任务中文完整案例

用户可直接使用 [task_series/README.md](task_series/README.md) 中的 7 套案例。图片统一放在 `task_series/images/`，由 7 套配方通过相对路径共用，避免重复文件：

| 任务数 | 目录 | 内容范围 |
| ---: | --- | --- |
| 2 | `task_series/02_tasks/` | 原图、边缘 |
| 4 | `task_series/04_tasks/` | 增加阈值、二维码 |
| 6 | `task_series/06_tasks/` | 增加 Blob、轮廓 |
| 8 | `task_series/08_tasks/` | 增加形态学、直线 |
| 10 | `task_series/10_tasks/` | 增加颜色、中文 OCR |
| 12 | `task_series/12_tasks/` | 增加复杂二维码、两点测量 |
| 16 | `task_series/16_tasks/` | 增加角度测量和参数对照 |

整套 `task_series/` 包含：

- 一个 version 5 的 `.recipe` 配方；
- 一份逐任务中文说明和预期结果；
- 一份由所有配方共用的测试图片目录；
- 每个任务恰好绑定一个工具，任务数与工具数一致。

处理类工具没有绑定 ROI 时执行整图。测量案例会在工具内部明确保存点或线 ROI，不会误用画布上临时绘制的 ROI。

## 单功能与兼容性案例

| 配方 | 图片 | 覆盖内容 |
| --- | --- | --- |
| `case_qr_clean.recipe` | `assets/images/qr_tests/qr_test.png` | 单个清晰二维码、ROI 和通过判定 |
| `case_qr_multi.recipe` | `assets/images/qr_tests/qr_extreme_multi_mixed.png` | 多二维码、旋转、透视和重复过滤 |
| `case_ocr.recipe` | `assets/images/ocr_product_sample.jpg` | PP-OCRv6 tiny 中文文字检测与识别 |
| `case_measurement.recipe` | `assets/images/qr_tests/qr_test.png` | 点距、线角度、圆直径和直线拟合 |
| `case_pipeline.recipe` | `assets/images/qr_tests/qr_extreme_multi_mixed.png` | 边缘、阈值、轮廓、形态学、颜色和直线 |
| `case_template_shape.recipe` | `assets/images/qr_tests/qr_test.png` | 模板匹配、形状匹配、结果 ROI 和 Fixture |
| `all_tools_test.recipe` | 多个仓库资源 | 旧版字段与完整工具兼容检查 |

## 加载方法

1. 启动程序，选择菜单「文件(F) → 打开配方...」。
2. 打开目标 `.recipe` 文件。
3. 多任务案例可先选择一个任务执行，再使用「全部执行」验证完整流程。
4. 如果要复制案例到别处，请复制整个 `task_series` 目录，不要只复制配方文件。

旧版案例正常加载后，再次保存会按当前 version 5 格式写出。YOLO 案例仍需本机存在对应模型和运行时；本次新增的多任务案例不依赖外部 YOLO 模型。
