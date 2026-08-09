# 华睿 iRAYPLE IMV 工业相机接入

> 文档复核日期：2026-08-02。本轮任务面板对齐不改变 IMV 连接、像素格式或运行库部署规则。

应用已接入华睿/iRAYPLE 普通 GigE、USB 工业相机 SDK（`IMV_*` 接口）。IP 相机应选择“华睿 iRAYPLE”后端，不要选择采集卡的 `IMV_FG_*` 接口。

## 连接方式

在“硬件”窗口中选择“华睿 iRAYPLE”，相机地址支持：

- IP：`192.168.20.22` 或 `ip:192.168.20.22`
- 枚举序号：`0`、`1` 等
- 用户自定义 ID：`camera-1` 或 `user:camera-1`
- CameraKey：`key:厂商:序列号`

连接前请确保电脑网卡和相机位于同一网段，并关闭 MV Viewer 中对该相机的控制连接。MV Viewer 可以保留打开，但同一台相机通常不能同时被两个程序以控制权限占用。

## 已支持功能

- 动态加载 `MVSDKmd.dll`，未安装 SDK 的电脑仍可正常启动应用
- GigE/USB 相机枚举、按 IP/序号/用户 ID/CameraKey 连接
- 连续取流、软件触发、Line1/Line2 硬件触发
- 自动曝光、手动曝光（微秒）、增益
- Mono8/BGR8/RGB8 直接处理，其他像素格式通过 `IMV_PixelConvert` 转为 BGR8
- 每帧强制配对 `IMV_GetFrame`/`IMV_ReleaseFrame`，避免 SDK 缓冲池耗尽
- 帧缓存清空、丢帧计数及现有自动重连流程

## 运行库部署

`scripts/package_runtime.ps1` 会自动查找以下位置，并把 `Runtime\\x64` 中的全部 DLL、CTI、DAT 和配置文件放到发布包程序目录：

- `IMV_RUNTIME`、`MV_VIEWER_HOME`、`MVSDK_PATH` 环境变量
- `F:\\MV Viewer\\Runtime\\x64`
- Program Files 下的 MV Viewer/iRAYPLE 常见安装目录

只复制 `MVSDKmd.dll` 不够；它依赖 GenApi、日志、图像转换、Producer CTI 和数据表文件。发布前还需确认所安装 SDK 的再分发许可。

此外，当前 `MVSDKmd.dll` 使用 Visual C++ 2013 构建。打包脚本会同时放入 x64 的 `msvcp120.dll` 和 `msvcr120.dll`，解决干净 Windows 10 企业版上因缺少 VC++ 2013 运行库而无响应的问题。

## SDK 与设备诊断

```powershell
CameraDiagnostics.exe --huaray-discovery-only
```

返回设备型号、序列号、IP、MAC、占用状态、SDK 版本与实际加载路径。界面的“扫描设备”和 GigE ForceIP 使用同一组官方 IMV API。
