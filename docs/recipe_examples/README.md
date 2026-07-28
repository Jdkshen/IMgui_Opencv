# 案例配方说明

> 文档同步日期：2026-07-27。案例文件目前保留 recipe version 2，用于验证当前 version 4 加载器的向后兼容；生产任务分组、独立输入和 PLC 映射应在另存的新配方中配置。


所有案例都使用相对路径。加载时从配方目录或其仓库祖先目录解析资源；运行时副本位于可执行文件旁的 `recipes/`。旧版本案例正常加载后，再次保存会按当前 version 4 格式写出。

| 配方 | 图片 | 覆盖内容 |
| --- | --- | --- |
| `case_qr_clean.recipe` | `assets/images/qr_tests/qr_test.png` | 单个清晰 QR、ZXing-cpp、ROI 和通过判定 |
| `case_qr_multi.recipe` | `assets/images/qr_tests/qr_extreme_multi_mixed.png` | 多个 QR、旋转、透视和重复过滤 |
| `case_ocr.recipe` | `assets/images/ocr_product_sample.jpg` | PP-OCRv6 tiny 文字检测与识别 |
| `case_measurement.recipe` | `assets/images/qr_tests/qr_test.png` | 点距、线角度、圆直径和直线拟合 |
| `case_pipeline.recipe` | `assets/images/qr_tests/qr_extreme_multi_mixed.png` | 边缘、阈值、轮廓、形态学、颜色和直线工具 |
| `case_template_shape.recipe` | `assets/images/qr_tests/qr_test.png` | 使用 `12345_tpl0.png`、`12345_tpl1.png` 的模板/形状匹配 |

从应用“配方”菜单加载案例。建议先使用 `case_qr_clean.recipe` 验证图片加载、ROI 绑定、二维码、结果标签和判定。`case_measurement.recipe` 有意放置多个测量实例，便于通过标签区分结果。

`all_tools_test.recipe` 用于较宽的工具兼容检查；涉及 YOLO/OCR 的工具仍需要本机存在对应模型和运行时。任务分组、独立文件夹和相机优先由专项回归动态构造，避免案例绑定开发机绝对路径。
