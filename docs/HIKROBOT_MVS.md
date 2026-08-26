# 海康机器人 MVS 相机接入

> 文档复核日期：2026-08-25。16 槽按需连接不改变 MVS 连接、像素格式或运行库部署规则；打包机未发现 MVS Runtime 时，目标机仍须单独安装。

项目通过 `HikrobotMvsCameraAdapter` 对接海康机器人 MVS C API。SDK 在运行时动态加载，未安装 MVS 的电脑仍可正常启动应用，只会在连接该相机时显示缺少运行库的错误。

## 使用方法

1. 在目标电脑安装与程序位数一致的海康机器人 **MVS x64 Runtime/SDK**。
2. 关闭 MVS 客户端中的相机连接；同一台相机通常不能同时被两个程序独占打开。
3. 在“设备连接 → 工业相机”中选择采集后端 **海康机器人 MVS**。
4. 相机地址填写 GigE 相机 IP，例如 `192.168.20.22`。也可以填写序列号、MVS 用户名称或枚举序号 `0`。
5. 点击“连接相机”。连接后可使用连续采集、曝光、增益、软件触发和 Line1/Line2 硬件触发。

MVS 相机的手动曝光值单位是微秒（us），与 OpenCV/DirectShow 的曝光刻度不同。

## 运行库查找顺序

程序依次检查：

- 程序目录下的 `MvCameraControl.dll`；
- `MVCAM_COMMON_RUNENV`、`MVS_RUNTIME` 或 `MVSDK_PATH` 环境变量；
- MVS 的标准 `Common Files\\MVS\\Runtime\\Win64_x64` 安装目录；
- Windows `PATH`。

执行 `scripts/package_runtime.ps1` 时，如果打包电脑已经安装 MVS x64 Runtime，脚本会自动把该运行目录中的 DLL 带入发布包；没有安装时不会阻止普通版本打包。

## 常见连接错误

- `MVS runtime was not found`：目标机没有安装 x64 MVS Runtime，发布目录中也没有完整运行库。
- `MVS did not find a GigE or USB camera`：检查网卡、相机供电、MVS 驱动、防火墙以及电脑与相机是否在同一网段。
- `camera ... was not found`：IP/序列号填写错误；错误信息会同时列出 SDK 枚举到的设备。
- `close the MVS viewer first`：MVS 客户端或其他程序正在独占相机，请先断开。
- 连续丢帧：在 MVS 中确认网卡巨帧、包大小和网卡节能设置；适配器连接 GigE 相机时会尝试设置 SDK 建议的最佳包大小。

厂商运行库由海康机器人提供并受其许可协议约束，仓库不直接提交这些二进制文件。

## 像素格式真机验收

先在 MVS 客户端中把相机依次设为 `Mono10`、`Mono12`、`Mono16`和相机支持的 Bayer 格式，每次设置后运行：

```powershell
CameraDiagnostics.exe --camera-pixel-format hikrobot <IP或序列号> 10 camera_pixel_format_validation.json
```

验收报告会记录 PFNC、位深、Bayer 状态、MVS 转换路径和显示帧尺寸。每种格式都应有至少一帧 `displayFrameValid=true`。
