# 项目文档索引

> 文档同步日期：2026-07-28。这里用于区分当前使用说明、开发规范、状态快照和历史设计稿。

## 建议阅读顺序

1. [根目录 README](../README.md)：项目能力、工具列表和快速入口。
2. [任务分组与执行](TASK_GROUPS.md)：任务管理、独立输入、相机优先、单步、并行和结果总览。
3. [硬件接入](HARDWARE_INTEGRATION.md)：工业相机、16 任务 PLC Trigger、Busy/Done/ACK 时序和模拟器。
4. [代码结构](CODE_STRUCTURE.md)：目录职责、核心状态和实际执行链路。
5. [模块关系](MODULE_RELATIONSHIP.md)：输入、执行、结果发布和显示关系。
6. [算法说明](ALGORITHMS.md)：type 0-17 工具、统一接口和扩展方法。
7. [构建基线](BUILD.md) 与 [发布说明](RELEASE.md)：编译、测试和打包。

## 当前使用说明

| 文档 | 用途 |
| --- | --- |
| [TASK_GROUPS.md](TASK_GROUPS.md) | 面向用户和开发者的任务分组完整说明 |
| [ALGORITHMS.md](ALGORITHMS.md) | 当前 type 0～17 工具能力、输入输出和扩展契约 |
| [HARDWARE_INTEGRATION.md](HARDWARE_INTEGRATION.md) | 相机、TCP、Modbus、PLC 和 OPC UA 接入 |
| [PLC 模拟器](../tools/plc_simulator/README.md) | 无真实 PLC 时测试独立任务拍照和整套握手 |
| [VIDEO_AUDIO.md](VIDEO_AUDIO.md) | 视频与音频播放模块及当前接入边界 |
| [OPENCV5_EXPERIMENT.md](OPENCV5_EXPERIMENT.md) | OpenCV 5 DNN 实验工具和 helper |
| [recipe_examples/README.md](recipe_examples/README.md) | 可运行案例配方说明 |

## 开发与维护

| 文档 | 用途 |
| --- | --- |
| [CODE_STRUCTURE.md](CODE_STRUCTURE.md) | 当前目录、模块和数据流 |
| [CODE_ANALYSIS.md](CODE_ANALYSIS.md) | 入口、UI、图像、ROI、渲染和算法代码解析 |
| [CODE_LAYOUT_2026.md](CODE_LAYOUT_2026.md) | 新代码应放置的位置和分层约束 |
| [INSPECTION_PIPELINE_2026.md](INSPECTION_PIPELINE_2026.md) | 判定、测量、标定、Fixture 和导出 |
| [TASK_EXECUTION.md](TASK_EXECUTION.md) | 开发任务模板、type 分配和验收清单 |
| [PROJECT_UPDATE_GUIDE.md](PROJECT_UPDATE_GUIDE.md) | 代码、工程、配方和文档同步规则 |
| [ROADMAP.md](ROADMAP.md) | 已完成能力和后续方向 |
| [IMGUI_API.md](IMGUI_API.md) | Dear ImGui API 本地参考手册 |
| [PERFORMANCE_REVIEW.md](PERFORMANCE_REVIEW.md) | 早期性能审查快照及当前架构对照入口 |

## 构建与发布

| 文档 | 用途 |
| --- | --- |
| [BUILD.md](BUILD.md) | VS2022、依赖、构建和回归命令 |
| [RELEASE.md](RELEASE.md) | CI、运行包制作和发布验收 |
| [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) | 第三方组件声明 |

## 状态快照与历史资料

- [STATUS_2026-07-28.md](STATUS_2026-07-28.md)：PLC 单槽收敛、ACK 超时复位、Trigger 映射同步及本轮干净构建与回归快照。
- [STATUS_2026-07-27.md](STATUS_2026-07-27.md)：Modbus TCP IO 映射、工业握手和 PLC 触发指定任务拍照的实现与验证快照。
- [STATUS_2026-07-26.md](STATUS_2026-07-26.md)：任务分组、独立图片/文件夹、相机优先和多任务执行收尾快照。
- [STATUS_2026-07-25.md](STATUS_2026-07-25.md) 与 [STATUS_2026-07-19.md](STATUS_2026-07-19.md)：早期合并和平台基线快照。
- [DEVELOPMENT_PLAN_2026-07-19.md](DEVELOPMENT_PLAN_2026-07-19.md)：当时的 P0～P4 计划与完成情况，后续事实以当前说明为准。
- `superpowers/plans/` 和 `superpowers/specs/` 记录特定功能当时的实施计划与设计决定，保留原始 API、路径和验收语义，不做追溯式改写。
- [PERFORMANCE_REVIEW.md](PERFORMANCE_REVIEW.md) 是早期性能审查，其中出现的旧全局变量属于历史代码示例；当前实现以 `ImageState`、`ROIState` 和 `VisionContext` 为准。

事实优先级为：当前源码与回归测试 → 本页“当前使用说明/开发与维护” → 最新日期状态快照 → 历史计划和设计稿。历史资料中的路径、函数名或状态与当前代码冲突时，不据此回退代码。

## 仓库内其他 Markdown

| 位置 | 性质与维护方式 |
| --- | --- |
| [根目录第三方声明](../THIRD_PARTY_NOTICES.md) | 发布所需许可证摘要，依赖版本变化时同步 |
| [redist/README.md](../redist/README.md) | 本地运行时二进制恢复与核对规则 |
| [models/ppocrv6/README.md](../models/ppocrv6/README.md) | PP-OCRv6 NCNN 模型文件清单 |
| [third_party/open62541/README.md](../third_party/open62541/README.md) | 上游版本、提交、编译参数和许可证；核心参数保留原文 |
| `superpowers/specs/*.md` | 已完成或阶段性设计记录，只补历史状态提示 |
| `superpowers/plans/*.md` | 已执行的实施计划，只补历史状态提示 |

## 文档维护规则

- 新增内容和面向用户的当前说明优先使用中文，代码标识、协议名、命令和第三方许可证名称保持原文。
- 新工具需要同步算法表、type 表、工程文件和回归说明。
- 修改任务、配方、输入或结果链路时，至少检查 `README.md`、`TASK_GROUPS.md`、`CODE_STRUCTURE.md`、`MODULE_RELATIONSHIP.md` 和 `PROJECT_UPDATE_GUIDE.md`。
- 修改 PLC 触发、IO 地址或相机回退时，必须同时检查 `HARDWARE_INTEGRATION.md`、PLC 模拟器说明、构建专项命令和最新状态快照。
- 修改后运行旧术语搜索、Markdown 围栏检查和 `git diff --check`。
