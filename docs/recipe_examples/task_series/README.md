# 多任务中文案例

> 这里提供 2、4、6、8、10、12、16 任务共 7 套完整案例。每套目录都有 version 5 配方和中文逐项说明，测试图片统一放在本目录的 `images/`，不重复存储。

| 任务数 | 配方 | 中文说明 | 适合学习 |
| ---: | --- | --- | --- |
| 2 | [02_tasks.recipe](02_tasks/02_tasks.recipe) | [README](02_tasks/README.md) | 原图、边缘 |
| 4 | [04_tasks.recipe](04_tasks/04_tasks.recipe) | [README](04_tasks/README.md) | 加入阈值、二维码 |
| 6 | [06_tasks.recipe](06_tasks/06_tasks.recipe) | [README](06_tasks/README.md) | 加入 Blob、轮廓 |
| 8 | [08_tasks.recipe](08_tasks/08_tasks.recipe) | [README](08_tasks/README.md) | 加入形态学、直线 |
| 10 | [10_tasks.recipe](10_tasks/10_tasks.recipe) | [README](10_tasks/README.md) | 加入颜色、中文 OCR |
| 12 | [12_tasks.recipe](12_tasks/12_tasks.recipe) | [README](12_tasks/README.md) | 加入复杂二维码、两点测量 |
| 16 | [16_tasks.recipe](16_tasks/16_tasks.recipe) | [README](16_tasks/README.md) | 完整覆盖与参数对照 |

从 2 任务案例开始逐级加载最容易理解。所有处理工具遵循「未绑定 ROI 就执行整图」；测量任务使用配方内明确绑定的 ROI。移动案例时请整体复制 `task_series` 目录。

## 共用测试图片

![零件缺陷](images/零件缺陷.jpg)

![清晰二维码](images/清晰二维码.png)

![复杂二维码](images/复杂二维码.png)

![中文文字](images/中文文字.jpg)
