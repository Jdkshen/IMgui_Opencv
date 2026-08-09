> 文档同步日期：2026-08-02。此目录用于 OCR 工具的 PP-OCRv6 NCNN 默认模型；本轮界面/资源整理未改变模型文件，算法入口和缺失模型行为见 [视觉算法说明](../../docs/ALGORITHMS.md)。

模型来源为 Avafly/PaddleOCR-ncnn-CPP v0.3.0 的 PP-OCRv6 NCNN 资产。

默认 tiny 模型文件名：

- PP_OCRv6_tiny_det.ncnn.param
- PP_OCRv6_tiny_det.ncnn.bin
- PP_OCRv6_tiny_rec.ncnn.param
- PP_OCRv6_tiny_rec.ncnn.bin
- ppocr_keys_v6_tiny.txt

JS 配置使用 `version: "v6", type: "tiny"`。`.param`、`.bin` 和字典必须成套部署；缺任一文件时 OCR 工具应报告模型资源错误，而不是把空识别当成 Pass。
