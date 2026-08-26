# 运行时文件目录

> 文档同步日期：2026-08-25。完整依赖、复制规则、Bootstrap 诊断和发布检查分别见 [构建基线](../docs/BUILD.md) 与 [发布说明](../docs/RELEASE.md)；字体和工具 PNG 属于 `assets/`，不放在本目录。

本目录在本地保存 OpenCV、ONNX Runtime、DirectML、NCNN、open62541 及必要的 MSVC 运行库/链接库。主程序 PostBuild 从这里复制运行所需 DLL；缺少必要文件时，编译可能成功但程序会在启动或加载对应工具时失败。

体积较大的 `*.dll` 和 `*.lib` 不通过普通 Git 提交。可从团队运行包或 GitHub Release 恢复，也可按仓库策略使用 Git LFS 管理。不要混放 OpenCV 4.x 与当前 OpenCV 5.0 DLL，也不要从未知来源下载同名二进制。

发布前至少核对：

- `opencv_world500.dll` 与视频后端 DLL；
- `onnxruntime*.dll`、`DirectML.dll`；
- `ncnn.dll` 及构建所需 `ncnn.lib`；
- open62541 静态库版本与 `third_party/open62541/` 源码、许可证一致；
- DLL 架构均为 x64，并与 Release CRT 配置匹配。
